# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# EMU68_FORCE_LVO_CACHE_OPS: route cache_ops.h's cache_pre_dma()/cache_post_dma()
# through the exec LVO (CachePreDMA/CachePostDMA) instead of the emu68 inline
# LINE-F fast path.  The fast path needs a private range opcode that only a
# patched Emu68 build understands.  Set this ON when building against an Emu68
# that has not picked up that opcode yet (CI builds against a released Emu68);
# leave it OFF for local/dev builds against a patched Emu68 to get the inline
# fast path.  See emu68-common/include/cache_ops.h.

set(EMU68_FORCE_LVO_CACHE_OPS OFF CACHE BOOL
    "Route cache_pre_dma()/cache_post_dma() through the exec LVO instead of the emu68 inline fast path")

# emu68_cache_ops_definitions()
# Apply the LVO-fallback compile definition to the current directory, if
# selected.  Call before the directory's targets are defined.
macro(emu68_cache_ops_definitions)
    if(EMU68_FORCE_LVO_CACHE_OPS)
        add_compile_definitions(EMU68_FORCE_LVO_CACHE_OPS)
    endif()
endmacro()
