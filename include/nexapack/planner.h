#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "nexapack/nexapack.h"
#include "nexapack/analyzer.h"
#include "nexapack/codec_interface.h"

namespace nexapack {

//---------------------------------------------------------------------------
// Adaptive planner policies.
//---------------------------------------------------------------------------
enum class PlannerPolicy {
    Fast,     // prioritize throughput and bounded memory
    Balanced, // balanced size/speed/memory (default)
    Dense,    // spend more CPU for better ratio, within resource limits
    Store,    // never compress
};

struct PlannerDecision {
    PlannerPolicy policy;
    CodecId codec;
    int level;
    uint64_t chunk_size;
    bool store_instead;                   // true if Store chosen over compression
    double estimated_ratio;               // estimated archive/input ratio
    uint64_t estimate_compress_cpu_us;    // microseconds (estimate)
    std::string reason;                   // human-readable explanation
};

// Planner configuration / resource limits.
struct PlannerConfig {
    PlannerPolicy policy = PlannerPolicy::Balanced;
    uint64_t memory_limit_bytes = 0;      // 0 = use conservative default
    int threads = 1;
    // If true, the planner may inspect previous similar files; must remain
    // deterministic and bounded.
    bool use_history = false;
};

// Choose a codec and level for a single entry.
// The decision is deterministic given the same inputs and config.
PlannerDecision plan(const InputFeatures& features, const PlannerConfig& cfg);

// Explain a decision without exposing file contents.
std::string explain(const PlannerDecision& d);

} // namespace nexapack
