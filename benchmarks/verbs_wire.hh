#ifndef NDS_BENCHMARKS_VERBS_WIRE_HH
#define NDS_BENCHMARKS_VERBS_WIRE_HH

#include <cstdint>

namespace nds::benchmark {

enum class VerbsOperation : std::uint16_t {
    Read,
    Write,
};

inline const char *operation_name(VerbsOperation operation) {
    return operation == VerbsOperation::Read ? "read" : "write";
}

}  // namespace nds::benchmark

#endif
