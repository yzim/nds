#include "rdma_benchmark_wire.hh"

#include "nds/logging.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <barrier>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kMaxInFlight = 8192U;
constexpr std::uint32_t kMaxQps = 8U;
constexpr std::uint64_t kRandomOffsetSeed = UINT64_C(0x4e44534350554842);

struct Config {
    nds::server::ConnectionConfig connection;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint64_t mr_bytes{};
    std::uint32_t in_flight{64U};
    std::uint32_t qps{1U};
    std::uint32_t max_rd_atomic{16U};
    std::uint32_t post_batch{};
    std::uint32_t warmup{2U};
    std::uint32_t iterations{100U};
    bool random_addresses{};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Benchmark CPU-initiated RDMA Read/Write against NPU HBM."};
    app.add_option("--device", config.connection.backend.device_name)->required();
    app.add_option("--gid-index", config.connection.backend.gid_index)->required();
    app.add_option("--listen", config.connection.listen_address)->required();
    app.add_option("--ib-port", config.connection.backend.port);
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--mr-bytes", config.mr_bytes, "Registered MR size per endpoint; zero selects the minimum.");
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--qps", config.qps)->check(CLI::Range(1U, kMaxQps));
    app.add_option("--max-rd-atomic", config.max_rd_atomic)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--post-batch", config.post_batch,
                   "Maximum linked WRs per ibv_post_send; zero posts one list per completion window.")
        ->check(CLI::Range(0U, kMaxInFlight));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    app.add_flag("--random-addresses", config.random_addresses,
                 "Use unique reproducible offsets within the registered MRs.");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    config.connection.backend.send_queue_depth = config.in_flight;
    config.connection.backend.receive_queue_depth = 16U;
    config.connection.backend.max_rd_atomic = config.max_rd_atomic;
    if (config.post_batch == 0U)
        config.post_batch = config.in_flight;
    return config;
}

nds::Result<std::string> indexed_address(const std::string &base, std::uint32_t index) {
    const auto parsed = nds::parse_tcp_address(base);
    if (!parsed || parsed->port > std::numeric_limits<std::uint16_t>::max() - index)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark QP port range is invalid");
    return parsed->ipv4 + ":" + std::to_string(static_cast<std::uint32_t>(parsed->port) + index);
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    const std::uint64_t operation_count = static_cast<std::uint64_t>(config.warmup) + config.iterations;
    const std::uint64_t required_operations = config.random_addresses ? operation_count : config.in_flight;
    if (required_operations == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / required_operations)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    const std::size_t minimum = static_cast<std::size_t>(config.bytes) * required_operations;
    if (config.mr_bytes == 0U)
        return minimum;
    if (config.mr_bytes < minimum || config.mr_bytes > std::numeric_limits<std::size_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark MR size is invalid");
    return static_cast<std::size_t>(config.mr_bytes);
}

nds::Result<std::vector<std::uint64_t>> make_offsets(const Config &config, std::size_t memory_bytes,
                                                      std::uint32_t qp_index) {
    const std::uint64_t count = static_cast<std::uint64_t>(config.warmup) + config.iterations;
    std::vector<std::uint64_t> offsets(static_cast<std::size_t>(count));
    if (!config.random_addresses) {
        for (std::uint64_t index = 0U; index < count; ++index)
            offsets[static_cast<std::size_t>(index)] = (index % config.in_flight) * config.bytes;
        return offsets;
    }
    const std::uint64_t slots = memory_bytes / config.bytes;
    if (slots < count)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "MR does not contain enough unique request slots");
    std::vector<std::uint64_t> indices(static_cast<std::size_t>(slots));
    std::iota(indices.begin(), indices.end(), 0U);
    std::mt19937_64 generator(kRandomOffsetSeed + qp_index);
    std::shuffle(indices.begin(), indices.end(), generator);
    for (std::uint64_t index = 0U; index < count; ++index)
        offsets[static_cast<std::size_t>(index)] = indices[static_cast<std::size_t>(index)] * config.bytes;
    return offsets;
}

nds::Result<void> transfer_window(nds::server::Connection *connection, const nds::server::RegisteredRegion &local,
                                 const nds::benchmark::RemoteMemory &remote, const Config &config,
                                 std::span<const std::uint64_t> offsets) {
    if (config.operation == nds::benchmark::Operation::Read)
        return connection->read_window_offsets(local, remote.address, remote.remote_key, config.bytes, offsets,
                                               config.post_batch);
    return connection->write_window_offsets(local, remote.address, remote.remote_key, config.bytes, offsets,
                                             config.post_batch);
}

nds::Result<void> run_operations(nds::server::Connection *connection, const nds::server::RegisteredRegion &local,
                                 const nds::benchmark::RemoteMemory &remote, const Config &config,
                                 const std::vector<std::uint64_t> &offsets, std::uint64_t first_operation,
                                 std::uint32_t operations) {
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(config.in_flight, operations - completed);
        const auto window_offsets = std::span(offsets).subspan(first_operation + completed, window);
        if (const auto transferred = transfer_window(connection, local, remote, config, window_offsets); !transferred)
            return nds::unexpected(transferred.error());
        completed += window;
    }
    return {};
}

struct Worker {
    std::unique_ptr<nds::server::Connection> connection;
    std::array<std::byte, 1U> activation_buffer{};
    nds::server::RegisteredRegion activation;
    nds::server::RegisteredRegion local;
    nds::benchmark::RemoteMemory remote{};
    std::vector<std::byte> memory;
    std::vector<std::uint64_t> offsets;
};

void save_error(std::string *error_message, std::mutex *error_mutex, std::atomic_bool *failed,
                const std::string &message) {
    std::lock_guard lock(*error_mutex);
    if (error_message->empty()) {
        *error_message = message;
        failed->store(true);
    }
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-hbm-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("cpu-hbm-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto memory_bytes = total_bytes(*config);
    if (!memory_bytes) {
        NDS_LOG_ERROR("cpu-hbm-server", "invalid buffer size: {}", memory_bytes.error().message);
        return EXIT_FAILURE;
    }

    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(config->qps);
    for (std::uint32_t index = 0U; index < config->qps; ++index) {
        const auto listen_address = indexed_address(config->connection.listen_address, index);
        if (!listen_address) {
            NDS_LOG_ERROR("cpu-hbm-server", "invalid QP {} listen address: {}", index,
                          listen_address.error().message);
            return EXIT_FAILURE;
        }
        auto worker = std::make_unique<Worker>();
        worker->connection = std::make_unique<nds::server::Connection>();
        auto connection_config = config->connection;
        connection_config.listen_address = *listen_address;
        if (const auto opened = worker->connection->open(connection_config); !opened) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} connection open failed: {}", index, opened.error().message);
            return EXIT_FAILURE;
        }
        auto activation = worker->connection->prepare_receive(worker->activation_buffer.data(),
                                                              worker->activation_buffer.size());
        if (!activation || !worker->connection->activate()) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} verbs activation failed", index);
            return EXIT_FAILURE;
        }
        worker->activation = std::move(*activation);
        workers.push_back(std::move(worker));
    }

    const auto access = config->operation == nds::benchmark::Operation::Read ? nds::server::MemoryAccess::LocalWrite
                                                                                : nds::server::MemoryAccess::LocalRead;
    for (std::uint32_t index = 0U; index < config->qps; ++index) {
        auto &worker = *workers[index];
        std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
        if (const auto received = worker.connection->bootstrap()->receive_bytes(record.data(), record.size());
            !received) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} NPU memory bootstrap failed: {}", index,
                          received.error().message);
            return EXIT_FAILURE;
        }
        if (!nds::benchmark::deserialize_remote_memory(record, &worker.remote) ||
            worker.remote.operation != config->operation || worker.remote.length < *memory_bytes) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} returned incompatible HBM metadata", index);
            return EXIT_FAILURE;
        }
        worker.memory.assign(*memory_bytes, std::byte{0x5a});
        auto local = worker.connection->register_memory(worker.memory.data(), worker.memory.size(), access);
        if (!local) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} CPU buffer registration failed: {}", index,
                          local.error().message);
            return EXIT_FAILURE;
        }
        worker.local = std::move(*local);
        auto offsets = make_offsets(*config, *memory_bytes, index);
        if (!offsets) {
            NDS_LOG_ERROR("cpu-hbm-server", "QP {} address generation failed: {}", index, offsets.error().message);
            return EXIT_FAILURE;
        }
        worker.offsets = std::move(*offsets);
    }

    std::string error_message;
    std::mutex error_mutex;
    std::atomic_bool failed{false};
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point end{};
    std::barrier start_barrier(static_cast<std::ptrdiff_t>(config->qps + 1U), [&start] { start = std::chrono::steady_clock::now(); });
    std::barrier end_barrier(static_cast<std::ptrdiff_t>(config->qps + 1U), [&end] { end = std::chrono::steady_clock::now(); });
    std::vector<std::thread> threads;
    threads.reserve(config->qps);
    for (auto &worker : workers) {
        Worker *worker_ptr = worker.get();
        threads.emplace_back([worker_ptr, &config = *config, &error_message, &error_mutex, &failed, &start_barrier,
                              &end_barrier] {
            const auto warmup = run_operations(worker_ptr->connection.get(), worker_ptr->local, worker_ptr->remote,
                                                config, worker_ptr->offsets, 0U, config.warmup);
            if (!warmup) {
                save_error(&error_message, &error_mutex, &failed, "warmup failed: " + warmup.error().message);
            }
            start_barrier.arrive_and_wait();
            if (!failed.load()) {
                if (const auto measured = run_operations(worker_ptr->connection.get(), worker_ptr->local,
                                                         worker_ptr->remote,
                                                         config, worker_ptr->offsets, config.warmup, config.iterations);
                    !measured) {
                    save_error(&error_message, &error_mutex, &failed, "measurement failed: " + measured.error().message);
                }
            }
            end_barrier.arrive_and_wait();
        });
    }
    start_barrier.arrive_and_wait();
    end_barrier.arrive_and_wait();
    for (auto &thread : threads)
        thread.join();
    if (!error_message.empty()) {
        NDS_LOG_ERROR("cpu-hbm-server", "multi-QP benchmark failed: {}", error_message);
        return EXIT_FAILURE;
    }
    const auto elapsed = end - start;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double total_operations = static_cast<double>(config->iterations) * config->qps;
    const double ops_per_second = total_operations / seconds;
    const double gib_per_second = total_operations * static_cast<double>(config->bytes) /
                                  seconds / (1024.0 * 1024.0 * 1024.0);
    const std::uint8_t finished = 1U;
    for (auto &worker : workers) {
        if (const auto sent = worker->connection->bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
            NDS_LOG_ERROR("cpu-hbm-server", "completion handshake failed: {}", sent.error().message);
            return EXIT_FAILURE;
        }
    }
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\"cpu-initiated\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"mr_bytes\":" << *memory_bytes << ",\"address_policy\":\""
              << (config->random_addresses ? "random-unique" : "cyclic-contiguous") << "\""
              << ",\"in_flight\":" << config->in_flight << ",\"qps\":" << config->qps
              << ",\"post_batch\":" << config->post_batch
              << ",\"warmup\":" << config->warmup
              << ",\"iterations\":" << config->iterations << ",\"completion_policy\":\"final-wr-signaled\""
              << ",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
              << ",\"ops_per_second\":" << ops_per_second << ",\"gib_per_second\":" << gib_per_second << "}\n";
    return EXIT_SUCCESS;
}
