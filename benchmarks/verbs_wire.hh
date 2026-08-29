#ifndef NDS_BENCHMARKS_VERBS_WIRE_HH
#define NDS_BENCHMARKS_VERBS_WIRE_HH

#include <cstdint>

namespace nds::benchmark {

enum class VerbsOperation : std::uint16_t {
    Read,
    Write,
    Send,
};

inline const char *operation_name(VerbsOperation operation) {
    switch (operation) {
        case VerbsOperation::Read:
            return "read";
        case VerbsOperation::Write:
            return "write";
        case VerbsOperation::Send:
            return "send";
    }
    return "unknown";
}

}  // namespace nds::benchmark

#endif
