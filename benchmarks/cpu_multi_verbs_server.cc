#include "rdma_benchmark_wire.hh"

#include "backend.hh"
#include "nds/logging.hh"
#include "nds/tcp_bootstrap.hh"

#include <CLI/CLI.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kMaxQps = 16U;

struct Config {
    nds::server::BackendConfig backend;
    std::string listen_address;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint64_t mr_bytes{};
    std::uint32_t qps{1U};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Serve CPU DRAM MRs to the multi-QP NPU peer verbs benchmark."};
    app.add_option("--device", config.backend.device_name)->required();
    app.add_option("--gid-index", config.backend.gid_index)->required();
    app.add_option("--listen", config.listen_address)->required();
    app.add_option("--ib-port", config.backend.port);
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--mr-bytes", config.mr_bytes)->required()->check(CLI::Range(std::uint64_t{1U}, UINT64_MAX));
    app.add_option("--qps", config.qps)->check(CLI::Range(1U, kMaxQps));
    app.add_option("--send-queue-depth", config.backend.send_queue_depth)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--receive-queue-depth", config.backend.receive_queue_depth)
        ->default_val(128U)
        ->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--max-rd-atomic", config.backend.max_rd_atomic)->default_val(16U)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--max-dest-rd-atomic", config.backend.max_dest_rd_atomic)
        ->default_val(16U)
        ->check(CLI::Range(1U, UINT32_MAX));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    if (config.mr_bytes > std::numeric_limits<std::size_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "MR size exceeds address space");
    return config;
}

nds::Result<int> accept_control(const std::string &listen_address) {
    const auto parsed = nds::parse_tcp_address(listen_address);
    if (!parsed)
        return nds::unexpected(parsed.error());
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(parsed->port);
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        inet_pton(AF_INET, parsed->ipv4.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 1) != 0) {
        const int error = errno;
        if (listener >= 0)
            (void)close(listener);
        return nds::unexpected(nds::ErrorCode::kTransport, std::strerror(error));
    }
    const int peer = accept(listener, nullptr, nullptr);
    const int error = errno;
    (void)close(listener);
    if (peer < 0)
        return nds::unexpected(nds::ErrorCode::kTransport, std::strerror(error));
    return peer;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-multi-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("cpu-multi-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }

    std::vector<std::unique_ptr<nds::server::VerbsBackend>> backends;
    std::vector<nds::transport::QpInfo> local_qps;
    backends.reserve(config->qps);
    local_qps.reserve(config->qps);
    for (std::uint32_t index = 0U; index < config->qps; ++index) {
        auto backend = std::make_unique<nds::server::VerbsBackend>();
        if (const auto opened = backend->open(config->backend); !opened) {
            NDS_LOG_ERROR("cpu-multi-server", "QP {} open failed: {}", index, opened.error().message);
            return EXIT_FAILURE;
        }
        local_qps.push_back(backend->local_qp_info());
        backends.push_back(std::move(backend));
    }

    const auto peer_fd = accept_control(config->listen_address);
    if (!peer_fd) {
        NDS_LOG_ERROR("cpu-multi-server", "control accept failed: {}", peer_fd.error().message);
        return EXIT_FAILURE;
    }
    nds::TcpPeerExchange bootstrap;
    if (const auto adopted = bootstrap.adopt(*peer_fd); !adopted) {
        NDS_LOG_ERROR("cpu-multi-server", "control adoption failed: {}", adopted.error().message);
        (void)close(*peer_fd);
        return EXIT_FAILURE;
    }
    std::vector<nds::transport::QpInfo> peer_qps;
    if (config->qps == 1U) {
        const auto peer = bootstrap.exchange_as_server(local_qps.front());
        if (!peer) {
            NDS_LOG_ERROR("cpu-multi-server", "QP bootstrap failed: {}", peer.error().message);
            return EXIT_FAILURE;
        }
        peer_qps.push_back(*peer);
    } else {
        const auto peers = bootstrap.exchange_as_server(local_qps);
        if (!peers) {
            NDS_LOG_ERROR("cpu-multi-server", "QP bootstrap failed: {}", peers.error().message);
            return EXIT_FAILURE;
        }
        peer_qps = *peers;
    }
    for (std::size_t index = 0U; index < backends.size(); ++index) {
        if (const auto connected = backends[index]->connect(peer_qps[index]); !connected) {
            NDS_LOG_ERROR("cpu-multi-server", "QP {} connect failed: {}", index, connected.error().message);
            return EXIT_FAILURE;
        }
    }

    const auto access = config->operation == nds::benchmark::Operation::Read ? IBV_ACCESS_REMOTE_READ
                                                                               : IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    std::vector<std::vector<std::byte>> memory(config->qps);
    std::vector<nds::server::RegisteredRegion> regions;
    regions.reserve(config->qps);
    std::vector<std::uint8_t> records(config->qps * nds::benchmark::kMemoryRecordBytes);
    for (std::uint32_t index = 0U; index < config->qps; ++index) {
        memory[index].assign(static_cast<std::size_t>(config->mr_bytes), std::byte{0x5a});
        auto region = backends[index]->register_memory(memory[index].data(), memory[index].size(), access);
        if (!region) {
            NDS_LOG_ERROR("cpu-multi-server", "QP {} DRAM registration failed: {}", index, region.error().message);
            return EXIT_FAILURE;
        }
        std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
        if (!nds::benchmark::serialize_remote_memory(
                {config->operation, reinterpret_cast<std::uint64_t>(region->address()), region->length(),
                 region->remote_key()},
                &record)) {
            NDS_LOG_ERROR("cpu-multi-server", "QP {} cannot serialize memory metadata", index);
            return EXIT_FAILURE;
        }
        std::memcpy(records.data() + index * record.size(), record.data(), record.size());
        regions.push_back(std::move(*region));
    }
    if (const auto sent = bootstrap.send_bytes(records.data(), records.size()); !sent) {
        NDS_LOG_ERROR("cpu-multi-server", "remote-memory bootstrap failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::vector<std::uint8_t> client_records(records.size());
    if (const auto received = bootstrap.receive_bytes(client_records.data(), client_records.size()); !received) {
        NDS_LOG_ERROR("cpu-multi-server", "client-memory bootstrap failed: {}", received.error().message);
        return EXIT_FAILURE;
    }
    std::uint8_t finished{};
    if (const auto received = bootstrap.receive_bytes(&finished, sizeof(finished)); !received || finished != 1U) {
        NDS_LOG_ERROR("cpu-multi-server", "benchmark completion handshake failed");
        return EXIT_FAILURE;
    }
    const std::uint8_t acknowledged = 1U;
    if (const auto sent = bootstrap.send_bytes(&acknowledged, sizeof(acknowledged)); !sent) {
        NDS_LOG_ERROR("cpu-multi-server", "benchmark acknowledgement failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-multi-server", "client completed multi-QP benchmark");
    return EXIT_SUCCESS;
}
