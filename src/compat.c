#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/devicetree_protos.h>
#include <clib/utility_protos.h>
#else
#include <proto/exec.h>
#include <proto/devicetree.h>
#include <proto/utility.h>
#endif

#include <exec/types.h>

#include <compat.h>

APTR AllocVecPooled(APTR poolHeader, ULONG size)
{
    size += sizeof(ULONG);
    APTR ptr = AllocPooled(poolHeader, size);
    if (ptr == NULL)
        return NULL;

    *(ULONG *)ptr = size;
    return ptr + sizeof(ULONG);
}
void FreeVecPooled(APTR poolHeader, APTR ptr)
{
    if (ptr == NULL)
        return;
    ptr -= sizeof(ULONG);
    ULONG size = *(ULONG *)ptr;
    FreePooled(poolHeader, ptr, size);
}

void *memalign(APTR poolHeader, ULONG align, ULONG size)
{
    // Over-allocate: space for alignment, original pointer, and size header
    ULONG total = size + (align - 1) + sizeof(APTR) + sizeof(ULONG);
    APTR raw = AllocPooled(poolHeader, total);
    if (!raw)
        return NULL;

    // Align the pointer after the size header and pointer storage
    APTR aligned = (APTR)(((ULONG)raw + sizeof(ULONG) + sizeof(APTR) + align - 1) & ~(align - 1));

    // Store the original pointer just before the aligned pointer
    ((APTR *)aligned)[-1] = raw;
    // Store the size header before the original pointer (for FreePooled)
    *(ULONG *)raw = total;

    return aligned;
}

void memalign_free(APTR poolHeader, void *ptr)
{
    if (ptr)
    {
        // Retrieve the original pointer and size header
        APTR raw = ((APTR *)ptr)[-1];
        ULONG size = *(ULONG *)raw;
        FreePooled(poolHeader, raw, size);
    }
}
