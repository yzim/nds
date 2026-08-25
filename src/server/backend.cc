#include "backend.hh"

#include "nds/wire/transport.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

namespace nds::server {
namespace {

std::uint32_t make_psn() {
    timespec now{};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (static_cast<std::uint32_t>(now.tv_nsec) ^ static_cast<std::uint32_t>(getpid())) & 0x00ffffffU;
}

std::uint32_t mtu_bytes(ibv_mtu mtu) {
    switch (mtu) {
        case IBV_MTU_256:
            return 256U;
        case IBV_MTU_512:
            return 512U;
        case IBV_MTU_1024:
            return 1024U;
        case IBV_MTU_2048:
            return 2048U;
        case IBV_MTU_4096:
            return 4096U;
        default:
            return 0U;
    }
}

bool mtu_value(std::uint32_t bytes, ibv_mtu *mtu) {
    if (mtu == nullptr)
        return false;
    switch (bytes) {
        case 256U:
            *mtu = IBV_MTU_256;
            return true;
        case 512U:
            *mtu = IBV_MTU_512;
            return true;
        case 1024U:
            *mtu = IBV_MTU_1024;
            return true;
        case 2048U:
            *mtu = IBV_MTU_2048;
            return true;
        case 4096U:
            *mtu = IBV_MTU_4096;
            return true;
        default:
            return false;
    }
}

}  // namespace

RegisteredRegion::~RegisteredRegion() {
    if (mr_ != nullptr)
        (void)ibv_dereg_mr(mr_);
}
RegisteredRegion::RegisteredRegion(RegisteredRegion &&other) noexcept : mr_(std::exchange(other.mr_, nullptr)) {}
RegisteredRegion &RegisteredRegion::operator=(RegisteredRegion &&other) noexcept {
    if (this != &other) {
        if (mr_ != nullptr)
            (void)ibv_dereg_mr(mr_);
        mr_ = std::exchange(other.mr_, nullptr);
    }
    return *this;
}
void *RegisteredRegion::address() const noexcept {
    return mr_ == nullptr ? nullptr : mr_->addr;
}
std::size_t RegisteredRegion::length() const noexcept {
    return mr_ == nullptr ? 0U : mr_->length;
}
std::uint32_t RegisteredRegion::remote_key() const noexcept {
    return mr_ == nullptr ? 0U : mr_->rkey;
}
std::uint32_t RegisteredRegion::local_key() const noexcept {
    return mr_ == nullptr ? 0U : mr_->lkey;
}

VerbsBackend::~VerbsBackend() {
    if (qp_ != nullptr)
        (void)ibv_destroy_qp(qp_);
    if (cq_ != nullptr)
        (void)ibv_destroy_cq(cq_);
    if (pd_ != nullptr)
        (void)ibv_dealloc_pd(pd_);
    if (context_ != nullptr)
        (void)ibv_close_device(context_);
}

Result<void> VerbsBackend::open(const BackendConfig &config) {
    int count = 0;
    ibv_device **devices = ibv_get_device_list(&count);
    ibv_device *selected = nullptr;
    for (int index = 0; devices != nullptr && index < count; ++index) {
        if (config.device_name == ibv_get_device_name(devices[index]))
            selected = devices[index];
    }
    if (selected == nullptr) {
        if (devices != nullptr)
            ibv_free_device_list(devices);
        return unexpected(ErrorCode::kVerbs, "RDMA device not found: " + config.device_name);
    }
    context_ = ibv_open_device(selected);
    ibv_free_device_list(devices);
    if (context_ == nullptr || (pd_ = ibv_alloc_pd(context_)) == nullptr || config.send_queue_depth == 0U ||
        config.receive_queue_depth == 0U || config.send_queue_depth > std::numeric_limits<int>::max() / 2U ||
        config.receive_queue_depth > std::numeric_limits<int>::max() / 2U ||
        (cq_ = ibv_create_cq(context_, static_cast<int>(config.send_queue_depth + config.receive_queue_depth), nullptr,
                              nullptr, 0)) == nullptr) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    ibv_qp_init_attr init{};
    init.send_cq = cq_;
    init.recv_cq = cq_;
    init.qp_type = IBV_QPT_RC;
    init.cap.max_send_wr = config.send_queue_depth;
    init.cap.max_recv_wr = config.receive_queue_depth;
    init.cap.max_send_sge = 1U;
    init.cap.max_recv_sge = 1U;
    qp_ = ibv_create_qp(pd_, &init);
    ibv_port_attr port{};
    ibv_gid gid{};
    if (qp_ == nullptr || ibv_query_port(context_, config.port, &port) != 0 || port.state != IBV_PORT_ACTIVE ||
        ibv_query_gid(context_, config.port, static_cast<int>(config.gid_index), &gid) != 0) {
        return unexpected(ErrorCode::kVerbs, "failed to create CPU verbs QP or query active port");
    }
    config_ = config;
    local_.qp_num = qp_->qp_num;
    local_.psn = make_psn();
    local_.port_num = config.port;
    local_.gid_index = static_cast<std::uint16_t>(config.gid_index);
    local_.path_mtu = mtu_bytes(port.active_mtu);
    local_.retry_count = 7U;
    local_.retry_timeout = 14U;
    std::memcpy(local_.gid, &gid, nds::wire::kGidBytes);
    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = config.port;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    if (ibv_modify_qp(qp_, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) != 0) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    return {};
}

Result<void> VerbsBackend::connect(const nds::transport::QpInfo &peer) {
    ibv_mtu mtu{};
    if (!mtu_value(nds::transport::select_mtu(local_.path_mtu, peer.path_mtu), &mtu)) {
        return unexpected(ErrorCode::kVerbs, "unsupported local path MTU");
    }
    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = mtu;
    attr.dest_qp_num = peer.qp_num;
    attr.rq_psn = peer.psn;
    attr.max_dest_rd_atomic = config_.max_dest_rd_atomic;
    attr.min_rnr_timer = 12U;
    attr.ah_attr.is_global = 1;
    std::memcpy(&attr.ah_attr.grh.dgid, peer.gid, nds::wire::kGidBytes);
    attr.ah_attr.grh.sgid_index = static_cast<std::uint8_t>(config_.gid_index);
    attr.ah_attr.grh.hop_limit = 1U;
    attr.ah_attr.grh.traffic_class = static_cast<std::uint8_t>(peer.traffic_class);
    attr.ah_attr.sl = static_cast<std::uint8_t>(peer.service_level);
    attr.ah_attr.port_num = config_.port;
    if (ibv_modify_qp(qp_, &attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                          IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) != 0) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    attr = {};
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14U;
    attr.retry_cnt = 7U;
    attr.rnr_retry = 7U;
    attr.sq_psn = local_.psn;
    attr.max_rd_atomic = config_.max_rd_atomic;
    if (ibv_modify_qp(qp_, &attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                          IBV_QP_MAX_QP_RD_ATOMIC) != 0) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    return {};
}

Result<RegisteredRegion> VerbsBackend::register_memory(void *address, std::size_t length, int access) {
    if (address == nullptr || length == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "invalid memory region");
    RegisteredRegion region;
    region.mr_ = ibv_reg_mr(pd_, address, length, access);
    if (region.mr_ == nullptr) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    return region;
}

Result<void> VerbsBackend::post_receive(const RegisteredRegion &region) {
    ibv_sge sge{reinterpret_cast<std::uintptr_t>(region.address()), static_cast<std::uint32_t>(region.length()),
                region.local_key()};
    ibv_recv_wr wr{};
    ibv_recv_wr *bad = nullptr;
    wr.wr_id = 1U;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    if (ibv_post_recv(qp_, &wr, &bad) != 0) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    return {};
}

Result<void> VerbsBackend::poll(ibv_wc_opcode opcode, std::uint32_t timeout_ms) {
    for (std::uint32_t elapsed = 0U; elapsed < timeout_ms; ++elapsed) {
        ibv_wc completion{};
        const int count = ibv_poll_cq(cq_, 1, &completion);
        if (count < 0 || (count == 1 && (completion.status != IBV_WC_SUCCESS || completion.opcode != opcode))) {
            return unexpected(ErrorCode::kVerbs, "unexpected verbs completion");
        }
        if (count == 1)
            return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kVerbs, "timed out waiting for verbs completion");
}

Result<void> VerbsBackend::wait_receive(std::uint32_t timeout_ms) {
    return poll(IBV_WC_RECV, timeout_ms);
}

Result<void> VerbsBackend::send(const RegisteredRegion &local, std::uint32_t length) {
    if (length == 0U || length > local.length()) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid send length");
    }
    ibv_sge sge{reinterpret_cast<std::uintptr_t>(local.address()), length, local.local_key()};
    ibv_send_wr wr{};
    ibv_send_wr *bad = nullptr;
    wr.wr_id = 3U;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (ibv_post_send(qp_, &wr, &bad) != 0) {
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    }
    return poll(IBV_WC_SEND, 5000U);
}

Result<void> VerbsBackend::transfer(ibv_wr_opcode opcode, const RegisteredRegion &local, std::uint64_t remote_address,
                                    std::uint32_t remote_key, std::uint32_t length) {
    return transfer_window(opcode, local, remote_address, remote_key, length, 1U);
}

Result<void> VerbsBackend::transfer_window(ibv_wr_opcode opcode, const RegisteredRegion &local,
                                           std::uint64_t remote_address, std::uint32_t remote_key,
                                           std::uint32_t length, std::uint32_t request_count) {
    if (length == 0U || request_count == 0U || request_count > config_.send_queue_depth ||
        static_cast<std::uint64_t>(length) * request_count > local.length()) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid verbs transfer window");
    }
    std::vector<ibv_sge> sges(request_count);
    std::vector<ibv_send_wr> wrs(request_count);
    for (std::uint32_t index = 0U; index < request_count; ++index) {
        sges[index] = {reinterpret_cast<std::uintptr_t>(local.address()) + static_cast<std::uint64_t>(index) * length,
                       length, local.local_key()};
        wrs[index] = {};
        wrs[index].wr_id = 2U + index;
        wrs[index].sg_list = &sges[index];
        wrs[index].num_sge = 1;
        wrs[index].opcode = opcode;
        wrs[index].send_flags = index + 1U == request_count ? static_cast<int>(IBV_SEND_SIGNALED) : 0;
        wrs[index].wr.rdma.remote_addr = remote_address + static_cast<std::uint64_t>(index) * length;
        wrs[index].wr.rdma.rkey = remote_key;
        wrs[index].next = index + 1U == request_count ? nullptr : &wrs[index + 1U];
    }
    ibv_send_wr *bad = nullptr;
    if (ibv_post_send(qp_, &wrs[0], &bad) != 0)
        return unexpected(ErrorCode::kVerbs, std::strerror(errno));
    return poll(opcode == IBV_WR_RDMA_READ ? IBV_WC_RDMA_READ : IBV_WC_RDMA_WRITE, 5000U);
}

Result<void> VerbsBackend::transfer_window_offsets(ibv_wr_opcode opcode, const RegisteredRegion &local,
                                                   std::uint64_t remote_address, std::uint32_t remote_key,
                                                   std::uint32_t length, std::span<const std::uint64_t> offsets,
                                                   std::uint32_t post_batch) {
    if (length == 0U || offsets.empty() || offsets.size() > config_.send_queue_depth || post_batch == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "invalid verbs transfer offsets");
    }
    for (const std::uint64_t offset : offsets) {
        if (offset > local.length() || length > local.length() - offset)
            return unexpected(ErrorCode::kInvalidArgument, "verbs transfer offset exceeds local region");
    }
    for (std::size_t first = 0U; first < offsets.size();) {
        const std::size_t count = std::min<std::size_t>(post_batch, offsets.size() - first);
        if (offset_sge_cache_.size() < count)
            offset_sge_cache_.resize(count);
        if (offset_wr_cache_.size() < count)
            offset_wr_cache_.resize(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const std::uint64_t offset = offsets[first + index];
            offset_sge_cache_[index] = {reinterpret_cast<std::uintptr_t>(local.address()) + offset, length,
                                        local.local_key()};
            auto &wr = offset_wr_cache_[index];
            wr.wr_id = 2U + first + index;
            wr.sg_list = &offset_sge_cache_[index];
            wr.num_sge = 1;
            wr.opcode = opcode;
            wr.send_flags = first + index + 1U == offsets.size() ? static_cast<int>(IBV_SEND_SIGNALED) : 0;
            wr.wr.rdma.remote_addr = remote_address + offset;
            wr.wr.rdma.rkey = remote_key;
            wr.next = index + 1U == count ? nullptr : &offset_wr_cache_[index + 1U];
        }
        ibv_send_wr *bad = nullptr;
        if (ibv_post_send(qp_, &offset_wr_cache_[0], &bad) != 0)
            return unexpected(ErrorCode::kVerbs, std::strerror(errno));
        first += count;
    }
    return poll(opcode == IBV_WR_RDMA_READ ? IBV_WC_RDMA_READ : IBV_WC_RDMA_WRITE, 5000U);
}

Result<void> VerbsBackend::read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                                std::uint32_t length) {
    return transfer(IBV_WR_RDMA_READ, local, remote_address, remote_key, length);
}

Result<void> VerbsBackend::write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                                 std::uint32_t length) {
    return transfer(IBV_WR_RDMA_WRITE, local, remote_address, remote_key, length);
}

Result<void> VerbsBackend::read_window(const RegisteredRegion &local, std::uint64_t remote_address,
                                       std::uint32_t remote_key, std::uint32_t length,
                                       std::uint32_t request_count) {
    return transfer_window(IBV_WR_RDMA_READ, local, remote_address, remote_key, length, request_count);
}

Result<void> VerbsBackend::write_window(const RegisteredRegion &local, std::uint64_t remote_address,
                                        std::uint32_t remote_key, std::uint32_t length,
                                        std::uint32_t request_count) {
    return transfer_window(IBV_WR_RDMA_WRITE, local, remote_address, remote_key, length, request_count);
}

Result<void> VerbsBackend::read_window_offsets(const RegisteredRegion &local, std::uint64_t remote_address,
                                               std::uint32_t remote_key, std::uint32_t length,
                                               std::span<const std::uint64_t> offsets, std::uint32_t post_batch) {
    return transfer_window_offsets(IBV_WR_RDMA_READ, local, remote_address, remote_key, length, offsets, post_batch);
}

Result<void> VerbsBackend::write_window_offsets(const RegisteredRegion &local, std::uint64_t remote_address,
                                                std::uint32_t remote_key, std::uint32_t length,
                                                std::span<const std::uint64_t> offsets, std::uint32_t post_batch) {
    return transfer_window_offsets(IBV_WR_RDMA_WRITE, local, remote_address, remote_key, length, offsets, post_batch);
}

const nds::transport::QpInfo &VerbsBackend::local_qp_info() const noexcept {
    return local_;
}

}  // namespace nds::server
