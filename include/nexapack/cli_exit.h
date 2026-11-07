#pragma once

#include <cstdint>
#include <string_view>

namespace nexapack {

// CLI exit codes.
enum ExitCode : int {
    ExitOk                = 0,
    ExitInvalidArguments  = 1,
    ExitUnsupportedFormat = 2,
    ExitCorruptArchive    = 3,
    ExitIoFailure         = 4,
    ExitVerificationFail  = 5,
    ExitInternalError     = 6,
};

} // namespace nexapack
