#include "endpoint.hh"

#include "transport_protocol.hh"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>
#include <unistd.h>

namespace nds::server {
namespace {

constexpr std::uint32_t kQpDepth = 128U;
constexpr std::uint8_t kMaxRdAtomic = 16U;

std::uint64_t next_wr_id(std::uint64_t current) {
    ++current;
    return current == 0U ? 1U : current;
}

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

MemoryRegion::~MemoryRegion() {
    if (mr_ != nullptr)
        (void)ibv_dereg_mr(mr_);
}

MemoryRegion::MemoryRegion(MemoryRegion &&other) noexcept
    : mr_(std::exchange(other.mr_, nullptr)), owner_(std::exchange(other.owner_, nullptr)) {}

MemoryRegion &MemoryRegion::operator=(MemoryRegion &&other) noexcept {
    if (this != &other) {
        if (mr_ != nullptr)
            (void)ibv_dereg_mr(mr_);
        mr_ = std::exchange(other.mr_, nullptr);
        owner_ = std::exchange(other.owner_, nullptr);
    }
    return *this;
}

void *MemoryRegion::address() const noexcept {
    return mr_ == nullptr ? nullptr : mr_->addr;
}

std::size_t MemoryRegion::length() const noexcept {
    return mr_ == nullptr ? 0U : mr_->length;
}

std::uint32_t MemoryRegion::local_key() const noexcept {
    return mr_ == nullptr ? 0U : mr_->lkey;
}

std::uint32_t MemoryRegion::remote_key() const noexcept {
    return mr_ == nullptr ? 0U : mr_->rkey;
}

bool MemoryRegion::belongs_to(const Endpoint *endpoint) const noexcept {
    return owner_ == endpoint;
}

QueuePair::QueuePair(Endpoint *endpoint, const nds::QpInfo &local, ibv_cq *cq, ibv_qp *handle)
    : endpoint_(endpoint), cq_(cq), handle_(handle), local_(local) {}

QueuePair::~QueuePair() {
    reset();
}

QueuePair::QueuePair(QueuePair &&other) noexcept
    : endpoint_(std::exchange(other.endpoint_, nullptr)),
      cq_(std::exchange(other.cq_, nullptr)),
      handle_(std::exchange(other.handle_, nullptr)),
      local_(other.local_),
      connected_(std::exchange(other.connected_, false)),
      next_send_wr_id_(std::exchange(other.next_send_wr_id_, 4U)),
      next_receive_wr_id_(std::exchange(other.next_receive_wr_id_, 1U)),
      pending_receive_ids_(std::move(other.pending_receive_ids_)),
      pending_completions_(std::move(other.pending_completions_)) {}

QueuePair &QueuePair::operator=(QueuePair &&other) noexcept {
    if (this != &other) {
        reset();
        endpoint_ = std::exchange(other.endpoint_, nullptr);
        cq_ = std::exchange(other.cq_, nullptr);
        handle_ = std::exchange(other.handle_, nullptr);
        local_ = other.local_;
        connected_ = std::exchange(other.connected_, false);
        next_send_wr_id_ = std::exchange(other.next_send_wr_id_, 4U);
        next_receive_wr_id_ = std::exchange(other.next_receive_wr_id_, 1U);
        pending_receive_ids_ = std::move(other.pending_receive_ids_);
        pending_completions_ = std::move(other.pending_completions_);
    }
    return *this;
}

void QueuePair::reset() noexcept {
    if (handle_ != nullptr)
        (void)ibv_destroy_qp(handle_);
    if (cq_ != nullptr)
        (void)ibv_destroy_cq(cq_);
    endpoint_ = nullptr;
    cq_ = nullptr;
    handle_ = nullptr;
    local_ = {};
    connected_ = false;
    next_send_wr_id_ = 4U;
    next_receive_wr_id_ = 1U;
    pending_receive_ids_.clear();
    pending_completions_.clear();
}

const nds::QpInfo &QueuePair::local_qp_info() const noexcept {
    return local_;
}

Result<void> QueuePair::connect(const nds::QpInfo &peer) {
    if (!created() || connected_)
        return Error{ErrorCode::kInvalidArgument, "CPU verbs QP must be created and disconnected before connect"};
    if (peer.qp_num == 0U || peer.psn > 0x00ffffffU || peer.port_num == 0U)
        return Error{ErrorCode::kInvalidArgument, "peer endpoint has invalid QP metadata"};
    ibv_mtu mtu{};
    if (!mtu_value(nds::transport::select_mtu(local_.path_mtu, peer.path_mtu), &mtu))
        return Error{ErrorCode::kVerbs, "unsupported local path MTU"};

    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = mtu;
    attr.dest_qp_num = peer.qp_num;
    attr.rq_psn = peer.psn;
    attr.max_dest_rd_atomic = kMaxRdAtomic;
    attr.min_rnr_timer = 12U;
    attr.ah_attr.is_global = 1;
    std::memcpy(&attr.ah_attr.grh.dgid, peer.gid, nds::wire::kGidBytes);
    attr.ah_attr.grh.sgid_index = static_cast<std::uint8_t>(endpoint_->config_.gid_index);
    attr.ah_attr.grh.hop_limit = 1U;
    attr.ah_attr.grh.traffic_class = static_cast<std::uint8_t>(peer.traffic_class);
    attr.ah_attr.sl = static_cast<std::uint8_t>(peer.service_level);
    attr.ah_attr.port_num = endpoint_->config_.port;
    if (ibv_modify_qp(handle_, &attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                          IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) != 0)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};

    attr = {};
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14U;
    attr.retry_cnt = 7U;
    attr.rnr_retry = 7U;
    attr.sq_psn = local_.psn;
    attr.max_rd_atomic = kMaxRdAtomic;
    if (ibv_modify_qp(handle_, &attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                          IBV_QP_MAX_QP_RD_ATOMIC) != 0)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    connected_ = true;
    return {};
}

bool QueuePair::created() const noexcept {
    return endpoint_ != nullptr && handle_ != nullptr;
}

bool QueuePair::connected() const noexcept {
    return connected_;
}

bool QueuePair::valid_local_region(const MemoryRegion &region, std::uint64_t local_offset,
                                   std::uint32_t length) const noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(region.address());
    return created() && connected() && region.belongs_to(endpoint_) && region.address() != nullptr &&
           region.local_key() != 0U && length != 0U && local_offset <= region.length() &&
           length <= region.length() - local_offset &&
           local_offset <= std::numeric_limits<std::uintptr_t>::max() - address &&
           address + local_offset <= std::numeric_limits<std::uintptr_t>::max() - length;
}

Result<void> QueuePair::post_receive(const MemoryRegion &region) {
    if (!valid_local_region(region, 0U, static_cast<std::uint32_t>(region.length())) ||
        region.length() > std::numeric_limits<std::uint32_t>::max())
        return Error{ErrorCode::kInvalidArgument, "CPU receive requires a valid QP and registered memory region"};
    if (pending_receive_ids_.size() >= kQpDepth)
        return Error{ErrorCode::kRuntime, "CPU receive queue has no available credit"};
    ibv_sge sge{reinterpret_cast<std::uintptr_t>(region.address()), static_cast<std::uint32_t>(region.length()),
                region.local_key()};
    ibv_recv_wr wr{};
    ibv_recv_wr *bad = nullptr;
    wr.wr_id = next_receive_wr_id_;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    if (ibv_post_recv(handle_, &wr, &bad) != 0)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    next_receive_wr_id_ = next_wr_id(wr.wr_id);
    pending_receive_ids_.push_back(wr.wr_id);
    return {};
}

Result<ibv_wc> QueuePair::poll_matching(ibv_wc_opcode opcode, std::uint64_t expected_wr_id, std::uint32_t timeout_ms) {
    if (!created())
        return Error{ErrorCode::kInvalidArgument, "CPU verbs QP is not created"};
    for (auto iterator = pending_completions_.begin(); iterator != pending_completions_.end(); ++iterator) {
        if (iterator->opcode == opcode && iterator->wr_id == expected_wr_id) {
            const ibv_wc completion = *iterator;
            pending_completions_.erase(iterator);
            return completion;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        ibv_wc completion{};
        const int count = ibv_poll_cq(cq_, 1, &completion);
        if (count == 1) {
            if (completion.opcode == opcode && completion.wr_id == expected_wr_id)
                return completion;
            pending_completions_.push_back(completion);
            continue;
        }
        if (count < 0)
            return Error{ErrorCode::kVerbs, "verbs CQ polling failed"};
    }
    return Error{ErrorCode::kVerbs, "timed out waiting for verbs completion"};
}

Result<void> QueuePair::poll(ibv_wc_opcode opcode, std::uint64_t expected_wr_id, std::uint32_t timeout_ms) {
    const auto completion = poll_matching(opcode, expected_wr_id, timeout_ms);
    if (!completion.ok())
        return completion.error();
    if (completion.value().status != IBV_WC_SUCCESS || completion.value().opcode != opcode ||
        completion.value().wr_id != expected_wr_id)
        return Error{ErrorCode::kVerbs, "unexpected verbs completion (status " +
                                            std::to_string(static_cast<int>(completion.value().status)) + ", opcode " +
                                            std::to_string(static_cast<int>(completion.value().opcode)) + ", wr_id " +
                                            std::to_string(completion.value().wr_id) + ", vendor error " +
                                            std::to_string(completion.value().vendor_err) + ")"};
    return {};
}

Result<void> QueuePair::wait_receive(std::uint32_t timeout_ms) {
    if (timeout_ms == 0U || timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "CPU transport completion timeout is outside the supported range"};
    if (pending_receive_ids_.empty())
        return Error{ErrorCode::kInvalidArgument, "CPU receive completion has no pending receive"};
    const std::uint64_t expected_wr_id = pending_receive_ids_.front();
    const auto completion = poll_matching(IBV_WC_RECV, expected_wr_id, timeout_ms);
    if (!completion.ok())
        return completion.error();
    pending_receive_ids_.pop_front();
    if (completion.value().status != IBV_WC_SUCCESS || completion.value().opcode != IBV_WC_RECV ||
        completion.value().wr_id != expected_wr_id)
        return Error{ErrorCode::kVerbs, "unexpected receive completion (status " +
                                            std::to_string(static_cast<int>(completion.value().status)) + ", opcode " +
                                            std::to_string(static_cast<int>(completion.value().opcode)) + ", wr_id " +
                                            std::to_string(completion.value().wr_id) + ", expected " +
                                            std::to_string(expected_wr_id) + ", vendor error " +
                                            std::to_string(completion.value().vendor_err) + ")"};
    return {};
}

Result<void> QueuePair::send(const MemoryRegion &local, std::uint32_t length, std::uint32_t timeout_ms) {
    const auto posted = post_send(local, length);
    if (!posted.ok())
        return posted.error();
    return poll(IBV_WC_SEND, posted.value(), timeout_ms);
}

Result<std::uint64_t> QueuePair::post_send(const MemoryRegion &local, std::uint32_t length) {
    if (!valid_local_region(local, 0U, length))
        return Error{ErrorCode::kInvalidArgument, "invalid send length"};
    ibv_sge sge{reinterpret_cast<std::uintptr_t>(local.address()), length, local.local_key()};
    ibv_send_wr wr{};
    ibv_send_wr *bad = nullptr;
    wr.wr_id = next_send_wr_id_;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (ibv_post_send(handle_, &wr, &bad) != 0)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    next_send_wr_id_ = next_wr_id(wr.wr_id);
    return wr.wr_id;
}

Result<void> QueuePair::transfer(ibv_wr_opcode opcode, const MemoryRegion &local, std::uint64_t remote_address,
                                 std::uint32_t remote_key, std::uint32_t length, std::uint32_t timeout_ms) {
    const auto posted = post_transfer(opcode, local, 0U, remote_address, remote_key, length);
    if (!posted.ok())
        return posted.error();
    return poll(opcode == IBV_WR_RDMA_READ ? IBV_WC_RDMA_READ : IBV_WC_RDMA_WRITE, posted.value(), timeout_ms);
}

Result<std::uint64_t> QueuePair::post_transfer(ibv_wr_opcode opcode, const MemoryRegion &local,
                                               std::uint64_t local_offset, std::uint64_t remote_address,
                                               std::uint32_t remote_key, std::uint32_t length) {
    const TransferRequest request{&local, local_offset, remote_address, remote_key, length};
    const auto posted = post_transfer_batch(opcode, std::span<const TransferRequest>{&request, 1U});
    if (!posted.ok())
        return posted.error();
    return posted.value().front();
}

Result<std::vector<std::uint64_t>> QueuePair::post_transfer_batch(ibv_wr_opcode opcode,
                                                                  std::span<const TransferRequest> requests) {
    if (!created() || !connected() || requests.empty())
        return Error{ErrorCode::kInvalidArgument, "invalid RDMA transfer batch"};

    std::vector<ibv_sge> sges(requests.size());
    std::vector<ibv_send_wr> wrs(requests.size());
    std::vector<std::uint64_t> wr_ids(requests.size());
    for (std::size_t index = 0U; index < requests.size(); ++index) {
        const TransferRequest &request = requests[index];
        if (request.local == nullptr || !valid_local_region(*request.local, request.local_offset, request.length) ||
            request.remote_address == 0U || request.remote_key == 0U ||
            request.remote_address > std::numeric_limits<std::uint64_t>::max() - request.length)
            return Error{ErrorCode::kInvalidArgument, "invalid RDMA transfer range"};

        wr_ids[index] = next_send_wr_id_;
        sges[index] = {reinterpret_cast<std::uintptr_t>(request.local->address()) + request.local_offset,
                       request.length, request.local->local_key()};
        wrs[index].wr_id = wr_ids[index];
        wrs[index].sg_list = &sges[index];
        wrs[index].num_sge = 1U;
        wrs[index].opcode = opcode;
        wrs[index].send_flags = index + 1U == requests.size() ? static_cast<int>(IBV_SEND_SIGNALED) : 0U;
        wrs[index].wr.rdma.remote_addr = request.remote_address;
        wrs[index].wr.rdma.rkey = request.remote_key;
        wrs[index].next = index + 1U == requests.size() ? nullptr : &wrs[index + 1U];
        next_send_wr_id_ = next_wr_id(next_send_wr_id_);
    }

    ibv_send_wr *bad = nullptr;
    if (ibv_post_send(handle_, wrs.data(), &bad) != 0)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    return wr_ids;
}

Result<void> QueuePair::read(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                             std::uint32_t length, std::uint32_t timeout_ms) {
    return transfer(IBV_WR_RDMA_READ, local, remote_address, remote_key, length, timeout_ms);
}

Result<void> QueuePair::write(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                              std::uint32_t length, std::uint32_t timeout_ms) {
    return transfer(IBV_WR_RDMA_WRITE, local, remote_address, remote_key, length, timeout_ms);
}

Endpoint::~Endpoint() {
    reset();
}

Endpoint::Endpoint(Endpoint &&other) noexcept
    : context_(std::exchange(other.context_, nullptr)),
      pd_(std::exchange(other.pd_, nullptr)),
      config_(std::move(other.config_)),
      port_(other.port_),
      gid_(other.gid_),
      qp_sequence_(std::exchange(other.qp_sequence_, 0U)) {}

Endpoint &Endpoint::operator=(Endpoint &&other) noexcept {
    if (this != &other) {
        reset();
        context_ = std::exchange(other.context_, nullptr);
        pd_ = std::exchange(other.pd_, nullptr);
        config_ = std::move(other.config_);
        port_ = other.port_;
        gid_ = other.gid_;
        qp_sequence_ = std::exchange(other.qp_sequence_, 0U);
    }
    return *this;
}

Result<void> Endpoint::open(const EndpointConfig &config) {
    if (opened())
        return Error{ErrorCode::kInvalidArgument, "CPU verbs endpoint is already open"};
    if (config.device_name.empty() || config.port == 0U)
        return Error{ErrorCode::kInvalidArgument, "CPU verbs endpoint configuration is invalid"};
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
        return Error{ErrorCode::kVerbs, "RDMA device not found: " + config.device_name};
    }
    context_ = ibv_open_device(selected);
    ibv_free_device_list(devices);
    if (context_ == nullptr)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    pd_ = ibv_alloc_pd(context_);
    if (pd_ == nullptr) {
        const std::string message = std::strerror(errno);
        reset();
        return Error{ErrorCode::kVerbs, message};
    }
    if (ibv_query_port(context_, config.port, &port_) != 0 || port_.state != IBV_PORT_ACTIVE ||
        ibv_query_gid(context_, config.port, static_cast<int>(config.gid_index), &gid_) != 0) {
        reset();
        return Error{ErrorCode::kVerbs, "failed to query active CPU verbs port"};
    }
    config_ = config;
    qp_sequence_ = 0U;
    return {};
}

Result<QueuePair> Endpoint::create_qp() {
    if (!opened())
        return Error{ErrorCode::kInvalidArgument, "CPU verbs endpoint is not open"};
    ibv_cq *cq = ibv_create_cq(context_, static_cast<int>(kQpDepth * 2U), nullptr, nullptr, 0);
    if (cq == nullptr)
        return Error{ErrorCode::kVerbs, "failed to create CPU verbs CQ"};
    ibv_qp_init_attr init{};
    init.send_cq = cq;
    init.recv_cq = cq;
    init.qp_type = IBV_QPT_RC;
    init.cap.max_send_wr = kQpDepth;
    init.cap.max_recv_wr = kQpDepth;
    init.cap.max_send_sge = 1U;
    init.cap.max_recv_sge = 1U;
    ibv_qp *handle = ibv_create_qp(pd_, &init);
    if (handle == nullptr) {
        (void)ibv_destroy_cq(cq);
        return Error{ErrorCode::kVerbs, "failed to create CPU verbs QP"};
    }

    nds::QpInfo local{};
    local.qp_num = handle->qp_num;
    local.psn = (make_psn() + qp_sequence_++) & UINT32_C(0x00ffffff);
    local.port_num = config_.port;
    local.gid_index = static_cast<std::uint16_t>(config_.gid_index);
    local.path_mtu = mtu_bytes(port_.active_mtu);
    local.retry_count = 7U;
    local.retry_timeout = 14U;
    std::memcpy(local.gid, &gid_, nds::wire::kGidBytes);

    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = config_.port;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    if (ibv_modify_qp(handle, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) != 0) {
        (void)ibv_destroy_qp(handle);
        (void)ibv_destroy_cq(cq);
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    }
    return QueuePair{this, local, cq, handle};
}

Result<MemoryRegion> Endpoint::reg_mr(void *address, std::size_t length, int access) {
    if (pd_ == nullptr || !opened() || address == nullptr || length == 0U ||
        reinterpret_cast<std::uintptr_t>(address) > std::numeric_limits<std::uintptr_t>::max() - length)
        return Error{ErrorCode::kInvalidArgument, "invalid memory region"};
    MemoryRegion region;
    region.mr_ = ibv_reg_mr(pd_, address, length, access);
    if (region.mr_ == nullptr)
        return Error{ErrorCode::kVerbs, std::strerror(errno)};
    region.owner_ = this;
    return region;
}

bool Endpoint::opened() const noexcept {
    return context_ != nullptr && pd_ != nullptr;
}

void Endpoint::reset() noexcept {
    if (pd_ != nullptr)
        (void)ibv_dealloc_pd(pd_);
    if (context_ != nullptr)
        (void)ibv_close_device(context_);
    pd_ = nullptr;
    context_ = nullptr;
    config_ = {};
    port_ = {};
    gid_ = {};
    qp_sequence_ = 0U;
}

}  // namespace nds::server
