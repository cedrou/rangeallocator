#pragma once

#include <stdint.h> // for uintptr_t


typedef void *ralloc_t;

typedef enum
{
    ALLOCATE_ANY,
    ALLOCATE_EXACT,
    ALLOCATE_ABOVE,
    ALLOCATE_BELOW
} allocation_flags;

typedef uintptr_t vaddr_t;

// Creates, and returns an opaque handle, to a range allocator representing the range[base, base + length).
// The parameter granularity specifies the required granularity for the allocations : 
// all allocations shall be rounded to a size multiple of the granularity.
ralloc_t create_range_allocator(vaddr_t base, size_t length, size_t granularity);

// Frees all control structures associated with the specified range allocator.
void destroy_range_allocator(ralloc_t ralloc);

// Allocates a range of the specified length and return the base address.
// The allocation flags parameter are interpreted as follows :
//  - ALLOCATE_ANY   : Allocates in any available address big enough to contain the requested length. The parameter optional_hint is ignored.
//  - ALLOCATE_EXACT : Allocates the requested length exactly at the address specified by optional_hint.
//  - ALLOCATE_ABOVE : Allocates the requested length above the address specified by optional_hint.
//  - ALLOCATE_BELOW : Allocates the requested length below the address specified by optional_hint.
//                     The complete allocated range must reside below the hint, not just the starting address.
// If the allocation cannot be satisfied, allocate_range() shall return (vaddr_t)-1.
vaddr_t allocate_range(ralloc_t ralloc, size_t length,allocation_flags flags, vaddr_t optional_hint);

// Releases a range (or part of a range) previously allocated.
void free_range(ralloc_t ralloc, vaddr_t base, size_t length);
