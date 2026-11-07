#include "nexapack/planner.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace nexapack {

namespace {

double entropy_incompressible_threshold(PlannerPolicy p) {
    switch (p) {
        case PlannerPolicy::Fast:   return 7.6;   // aggressive store
        case PlannerPolicy::Balanced: return 7.2;
        case PlannerPolicy::Dense:  return 6.8;   // try harder to compress
        case PlannerPolicy::Store:  return 0.0;   // always store
    }
    return 7.2;
}

int zstd_level_for(PlannerPolicy p) {
    switch (p) {
        case PlannerPolicy::Fast:   return 1;
        case PlannerPolicy::Balanced: return 3;
        case PlannerPolicy::Dense:  return 7;
        case PlannerPolicy::Store:  return 0;
    }
    return 3;
}

} // namespace

PlannerDecision plan(const InputFeatures& features, const PlannerConfig& cfg) {
    PlannerDecision d;
    d.policy = cfg.policy;
    d.store_instead = false;
    d.chunk_size = 1024ULL * 1024ULL; // 1 MiB default
    d.estimated_ratio = 1.0;
    d.estimate_compress_cpu_us = 0;
    d.reason.clear();

    // Store policy: never compress.
    if (cfg.policy == PlannerPolicy::Store) {
        d.codec = CodecId::Store;
        d.level = 0;
        d.store_instead = true;
        d.reason = "store policy";
        return d;
    }

    double entropy = features.entropy_bits_per_byte;
    bool already_compressed = features.looks_compressed;
    double thr = entropy_incompressible_threshold(cfg.policy);

    // Small files: compression rarely wins; store.
    if (features.size < 256) {
        d.codec = CodecId::Store;
        d.level = 0;
        d.store_instead = true;
        d.reason = "small file; store";
        return d;
    }

    // Already-compressed or high-entropy input: store.
    if (already_compressed || entropy >= thr) {
        d.codec = CodecId::Store;
        d.level = 0;
        d.store_instead = true;
        d.reason = already_compressed ? "already-compressed magic; store"
                                      : "high entropy; store";
        return d;
    }

    // Compress with zstd if available; else store.
    d.codec = CodecId::Zstd;
    d.level = zstd_level_for(cfg.policy);
    d.store_instead = false;
    // Estimate ratio from entropy: rough heuristic, not a guarantee.
    d.estimated_ratio = std::max(1.0, 8.0 / std::max(entropy, 0.5));
    d.reason = "compressible input; zstd";
    return d;
}

std::string explain(const PlannerDecision& d) {
    std::string out = "policy=";
    switch (d.policy) {
        case PlannerPolicy::Fast: out += "fast"; break;
        case PlannerPolicy::Balanced: out += "balanced"; break;
        case PlannerPolicy::Dense: out += "dense"; break;
        case PlannerPolicy::Store: out += "store"; break;
    }
    out += ", codec=";
    out += (d.codec == CodecId::Store ? "store" :
            d.codec == CodecId::Zstd ? "zstd" :
            d.codec == CodecId::Lz4 ? "lz4" : "unknown");
    out += ", level=" + std::to_string(d.level);
    out += ", store_instead=" + std::to_string(d.store_instead ? 1 : 0);
    out += ", reason=" + d.reason;
    return out;
}

} // namespace nexapack


