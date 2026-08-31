#include "launcher.hh"

#include "loaders/shared_library.hh"
#include "backend_abi.hh"

#include <utility>

namespace nds {

class RaLauncher::Impl {
public:
    client::SharedLibrary library;
    NdsRaBackendPostSend post_send{};
    NdsRaBackendPostRecv post_recv{};
    NdsRaBackendPollCq poll_cq{};
};

RaLauncher::RaLauncher() = default;
RaLauncher::~RaLauncher() = default;

Result<void> RaLauncher::load(const std::string &backend_path) {
    if (impl_ != nullptr || backend_path.empty())
        return Error{ErrorCode::kInvalidArgument, impl_ != nullptr ? "NDS RA launcher is already loaded"
                                                                   : "NDS RA requires an NDS backend artifact path"};

    NDS_ASSIGN_OR_RETURN(client::SharedLibrary library, client::SharedLibrary::open(backend_path));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPostSend post_send,
                         library.resolve_required<NdsRaBackendPostSend>("nds_ra_backend_post_send"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPostRecv post_recv,
                         library.resolve_required<NdsRaBackendPostRecv>("nds_ra_backend_post_recv"));
    NDS_ASSIGN_OR_RETURN(NdsRaBackendPollCq poll_cq,
                         library.resolve_required<NdsRaBackendPollCq>("nds_ra_backend_poll_cq"));
    impl_ = std::make_unique<Impl>();
    impl_->library = std::move(library);
    impl_->post_send = post_send;
    impl_->post_recv = post_recv;
    impl_->poll_cq = poll_cq;
    return {};
}

Result<void> RaLauncher::post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr, void *stream) {
    if (impl_ == nullptr || impl_->post_send == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_send(&qp, &wr, stream) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend Send failed"};
}

Result<void> RaLauncher::post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr) {
    if (impl_ == nullptr || impl_->post_recv == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    return impl_->post_recv(&qp, &wr) == 0 ? Result<void>{} : Error{ErrorCode::kRa, "RA backend receive failed"};
}

Result<std::uint32_t> RaLauncher::poll_cq(const NdsDeviceQp &qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                          NdsDeviceWc *wc) {
    if (impl_ == nullptr || impl_->poll_cq == nullptr)
        return Error{ErrorCode::kRuntime, "RA backend is not loaded"};
    const int result = impl_->poll_cq(&qp, send_cq, max_completions, wc);
    return result < 0 ? Result<std::uint32_t>(Error{ErrorCode::kRa, "RA backend CQ poll failed"})
                      : Result<std::uint32_t>(static_cast<std::uint32_t>(result));
}

}  // namespace nds
