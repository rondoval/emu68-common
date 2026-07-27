# SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#
# EMU68_FORCE_LVO_CACHE_OPS: route cache_ops.h's cache_pre_dma()/cache_post_dma()
# through the exec LVO (CachePreDMA/CachePostDMA) instead of the emu68 inline
# LINE-F fast path.  Both settings are shipped release flavors: ON builds the
# standard archives that run on any Emu68 release; OFF (the default) builds the
# "-rangeops" archives whose inline fast path needs an Emu68 with the dcache
# extensions — those drivers gate device init on emu68_has_dcache_range_ops()
# (emu68_features.h) and refuse to load on firmware without the opcode.
# See emu68-common/include/cache_ops.h.

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
