# Range Allocator coding test

## Probleme Description
```
    typedef void *ralloc_t;

    typedef enum
    {
        ALLOCATE_ANY,
        ALLOCATE_EXACT,
        ALLOCATE_ABOVE,
        ALLOCATE_BELOW,
    } allocation_flags;

    typedef uintptr_t vaddr_t;

    ralloc_t create_range_allocator(vaddr_t base, size_t length, size_t granularity);
    void destroy_range_allocator(ralloc_t ralloc);
    vaddr_t allocate_range(ralloc_t ralloc, size_t length, allocation_flags flags, vaddr_t optional_hint);
    void free_range(ralloc_t ralloc, vaddr_t base, size_t length);
```

For certain subsystems rather than allocating physical memory, we are more interested in effectively managing address spaces. 
Write a small, simple range allocator conforming to the API above.

The function `create_range_allocator()` creates, and returns an opaque handle, to a range allocator representing the range [base, base+length). 
The parameter `granularity` specifies the required granularity for the allocations: all allocations shall be rounded to a size multiple of the granularity.

The function `destroy_range_allocator()` frees all control structures associated with the specified range allocator.

The function `allocate_range()` must allocate a range of the specified length and return the base address. 
The allocation flags parameter must be interpreted as follows:
 - ALLOCATE_ANY : allocate in any available address big enough to contain the requested length. The parameter optional_hint must be ignored.
 - ALLOCATE_EXACT : allocate (if possible) the requested length exactly at the address specified by optional_hint.
 - ALLOCATE_ABOVE : allocate the requested length above the address specified by optional_hint.
 - ALLOCATE_BELOW : allocate the requested length below the address specified by optional_hint. The complete allocated range must reside below the hint, not just the starting address.
If the allocation cannot be satisfied, `allocate_range()` shall return (vaddr_t)-1.

Finally, the function `free_range()` must release a range (or part of a range) previously allocated.

## Proposed Solution
The provided solution is build upon a class that keeps a linked-list of disjoint memory areas that are available for an allocation. 

An item in the linked-list is a `span` and it contains the base address and the length of the memory area, and a pointer to the next item of the list (or `0` if it is the last).

The list is ordered by increasing base address of spans.

When an allocation is requested, we look for the first span that satisfy the request, considering the provided constraint on the base address. Then, we split this span into pieces, potentially inserting a new span into the list. The allocated block of memory is removed from the list and the base address is returned to the caller.

## Caveat
A weakness of this solution is that we potentially need to allocate a lot of new spans to insert in the list, which would go against the whole idea of this work to better manage the memory.

Two methods have been implemented to minimize these allocations. We can choose the first or the second at compilation time according to the usage of the allocator.

1. Use a pool of `span` instances which pre-allocates the maximum of instances that would be needed for the range allocator. The maximum would occur when the memory is the most fragmented, that is when one out every two blocks is allocated. So, at most we would have `(length / granularity)/2` instances. The size of a span is `3*sizeof(ptr)`, that is 24 bytes in 64-bits platforms. For a 4kB range with 64B granularity, we need at most 32 items, that is less than 1kB. For 1GB range with 256B granularity, the max is 2M instances, that is 48MB.
    - Pros: The memory is fully allocated allocated at start and released when the allocator is destroyed. There is no allocation during the lifetime of the allocator.
    - Cons: Depending on the length and granularity of the memory, this can lead to a large memory allocation.

2. Allocate each instance of `span` when we need a new one, but don't delete it when we release it. Instead, keep a list of all discarded `span` instances. When the algorithm needs a new object, first look into this list if there is any available instance. 
    - Pros: The memory usage stays minimal as we only allocate what we need, when we need it. No dealloc is done suring the lifetime.
    - Cons: We're doing allocation!

This can also be a mix of both solutions: first start with a pool of several span instances and then allocates new ones on demand.

The provided solution also makes its best to avoid inserting new spans by favouring when possible the allocations on the edges of spans, instead of slicing a span in three parts. There is only two places in the code where we allocate new spans. The first is when allocating with ALLOCATE_EXACT, and the requested memory range is in the middle of a span. The second is when we free a memory range and that it is not contiguous with an existing span.

Another radically different solution would consist in using a large bitmap of all memory blocks: each block is represented by a single which indicates its state (0: used, 1: free).

## Known limitations/bugs
- We have no way to check that the passed `ralloc_t` handler is valid, except checking it against null. If the user gives a wrong handle, the app would certainly crash.

- There is no protection against the use of two range allocators with overlapped memory ranges.

- The provided solution is not thread-safe. For this, we would need to protect all list write accesses.

- The memory range that is effectively accessible may be smaller than requested in the constructor if the length is not aligned with the granularity.
