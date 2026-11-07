#include "nexapack/codec_interface.h"
#include "nexapack/store_codec.h"

namespace nexapack {

#if defined(NEXAPACK_HAVE_ZSTD)
void register_zstd_codec();
#endif

#if defined(NEXAPACK_HAVE_LZ4)
void register_lz4_codec();
#endif

#if defined(NEXAPACK_HAVE_BROTLI)
void register_brotli_codec();
#endif

#if defined(NEXAPACK_HAVE_LZMA2)
void register_lzma2_codec();
#endif

CodecRegistry& CodecRegistry::instance() {
    static CodecRegistry registry;
    static bool registered = false;
    if (!registered) {
        registered = true;
        register_all_builtin_codecs();
    }
    return registry;
}

void CodecRegistry::register_codec(std::unique_ptr<ICompressionCodec> codec) {
    if (!codec) Throw(ErrorCode::InvalidArgument, "cannot register null codec");
    codecs_.push_back(std::move(codec));
}

ICompressionCodec* CodecRegistry::find(CodecId id) const {
    for (auto& c : codecs_) {
        if (c->id() == id) return c.get();
    }
    return nullptr;
}

std::vector<CodecInfo> CodecRegistry::list() const {
    std::vector<CodecInfo> out;
    out.reserve(codecs_.size());
    for (auto& c : codecs_) {
        CodecInfo info;
        info.id = c->id();
        info.name = std::string(c->name());
        info.min_level = c->min_level();
        info.max_level = c->max_level();
        info.default_level = c->default_level();
        info.supports_streaming = true;
        info.max_window = 0;
        out.push_back(info);
    }
    return out;
}

void register_all_builtin_codecs() {
    register_store_codec();
#if defined(NEXAPACK_HAVE_ZSTD)
    register_zstd_codec();
#endif
#if defined(NEXAPACK_HAVE_LZ4)
    register_lz4_codec();
#endif
#if defined(NEXAPACK_HAVE_BROTLI)
    register_brotli_codec();
#endif
#if defined(NEXAPACK_HAVE_LZMA2)
    register_lzma2_codec();
#endif
}

} // namespace nexapack
