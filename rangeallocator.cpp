#include "rangeallocator.h"

#include <vector>


// Represents a contiguous run of memory and can be used in a linked-list.
struct span
{
    span*   next;
    vaddr_t base;
    size_t  length;
};


// manager of span instances that uses a pool that is fully allocated at start
class span_manager_pool
{
public:
    span_manager_pool(size_t max_instances)
        : _pool(max_instances)
    {
        for (size_t i = 0; i < max_instances - 1; i++)
        {
            _pool[i].next = &_pool[i + 1];
        }
        _pool[max_instances - 1].next = 0;
        _available_spans = &_pool[0];
    }

    ~span_manager_pool()
    {}

    span* get()
    {
        span* s = _available_spans;
        if (s)
        {
            _available_spans = s->next;
            return s;
        }
        return 0;
    }

    void release(span* s)
    {
        s->next = _available_spans;
        _available_spans = s;
    }

private:
    std::vector<span> _pool;
    span * _available_spans;
};

// manager of span instances that keeps a list of allocated objects and create a new one only if the list is empty
class span_manager_allocate
{
public:
    span_manager_allocate(size_t /*max_instances*/)
    {
        _available_spans = 0;
    }

    ~span_manager_allocate()
    {
        span* current = _available_spans;
        while (current)
        {
            span* next = current->next;
            delete current;
            current = next;
        }
    }

    span* get()
    {
        span* s = _available_spans;
        if (s)
        {
            _available_spans = s->next;
            return s;
        }
        return new span;
    }

    void release(span* s)
    {
        s->next = _available_spans;
        _available_spans = s;
    }

private:
    span* _available_spans;
};



template <class SpanAllocator>
class range_allocator
{
public:
    // Construct a new instance.
    // The stored length value is the size of the memory range that is effectively accessible given
    // the provided granularity. It can be smaller than or equal to the provided length value.
    range_allocator(vaddr_t base, size_t length, size_t granularity)
        : _base(base), _length(length), _granularity(granularity), _spans((length / granularity) / 2)
    {
        // adjust the base address on next granularity bound
        // correct length in consequence
        //_base = ((base + _granularity - 1) / _granularity) * _granularity;
        //_length = length - (_base - base);

        // align the corrected length on previous granularity bound
        _length = (_length / _granularity) * _granularity;

        span* s = add_span();
        s->base = _base;
        s->length = _length;
        s->next = 0;

        _free_mem_root.next = s;
    }

    ~range_allocator()
    {
        // Release all used spans and let the span allocator manage its destruction
        while (_free_mem_root.next)
        {
            span* s = _free_mem_root.next;
            _free_mem_root.next = _free_mem_root.next->next;
            _spans.release(s);
        }
    }

    vaddr_t allocate(size_t length, allocation_flags flags, vaddr_t hint)
    {
        // Align the length to the upper granularity boundary
        length = ((length + _granularity - 1) / _granularity) * _granularity;

        if (length == 0) return (vaddr_t)-1;
        if (length > _length) return (vaddr_t)-1;

        // find the first span that match the request
        span* previous = &_free_mem_root;
        span* current = _free_mem_root.next;
        while (current)
        {
            if (check_span(current, length, flags, hint))
                break;

            previous = current;
            current = current->next;
        }
        
        // no available block
        if (!current) return (vaddr_t)-1;

        // truncate the found span and get the base allocation
        return split_span(previous, current, length, flags, hint);
    }

    void free(vaddr_t base, size_t length)
    {
        // Align base and length on granularity.
        // Maybe an error if base is not aligned as this should be a value returned by the allocator.
        base   = (base / _granularity) * _granularity;
        length = ((length + _granularity - 1) / _granularity) * _granularity;

        if (length == 0) return;
        if (base < _base || base >= _base+_length) return; // base MUST be in the range
        if (base + length > _base + _length) return; // the range to free must be contained entirely 
        
        //
        span* curr = &_free_mem_root;
        span* next = _free_mem_root.next;
        while (next)
        {
            //    curr                        next              
            // |--------|..................|--------|...........
            //                   |-------|                      
            if (base + length < next->base)
            {
                // include a new span in the list
                span* s = add_span();
                s->base = base;
                s->length = length;
                s->next = next;
                curr->next = s;
                return;
            }

            //    curr                        next              
            // |--------|..................|--------|...........
            //                     |-------|                    
            if (base + length == next->base)
            {
                // merge the free region at the beginning of the next span
                next->base = base;
                next->length += length;
                return;
            }

            //    curr                        next              
            // |--------|..................|--------|...........
            //                         |-------|                
            //                           |--------------|       
            //                                   |-------|       
            if (base < next->base + next->length)
            {
                // intersection is not empty: treat this as an error
                return;
            }


            //    curr                        next              
            // |--------|..................|--------|...........
            //                                      |-------|   
            if (base == next->base + next->length)
            {
                // check any overlap with next next
                if (next->next)
                {
                    //    next           next->next        
                    // |--------|........|--------|
                    //          |------------|   
                    if (base + length > next->next->base)
                    {
                        // intersection is not empty: treat this as an error
                        return;
                    }

                    //    next           next->next        
                    // |--------|........|--------|
                    //          |--------|   
                    if (base + length == next->next->base)
                    {
                        // merge with next span
                        next->length += length + next->next->length;
                        remove_span(next, next->next);
                        return;
                    }
                }

                // merge the free region at the end of the next span
                next->length += length;
                return;
            }

            //    curr                        next              
            // |--------|..................|--------|...........
            //                                        |-------| 
            //if (base > next->base + next->length)

            curr = next;
            next = next->next;
        }

        // no more span, include a new one at the end of the list
        span* s = add_span();
        s->base = base;
        s->length = length;
        s->next = 0;
        curr->next = s;
    }

private:

    span* add_span()
    {
        return _spans.get();
    }

    void remove_span(span* prev, span* curr)
    {
        prev->next = curr->next;
        _spans.release(curr);
    }

    // check if the span satisfy the constraints
    bool check_span(span* s, size_t length, allocation_flags flags, vaddr_t hint)
    {
        switch (flags)
        {
        case ALLOCATE_ANY:
            // need any span that has more than <length> bytes
            return (s->length >= length);

        case ALLOCATE_EXACT:
            // need a span that contains entirely [hint, hint+length[
            return (s->base <= hint) && (hint + length <= s->base + s->length);

        case ALLOCATE_ABOVE:
            if (s->base >= hint)
            {
                // _____'_____-----------_________
                //                   ^^^^         
                return (s->length >= length);
            }
            else if (s->base + s->length >= hint)
            {
                // ___________----'------_________
                //                   ^^^^         
                return (s->base + s->length >= hint + length);
            }
            return false;

        case ALLOCATE_BELOW:
            // s    |----------------h------------|
            //      |----------|                   
            return (s->base + length <= hint) && (s->length >= length);
        }
        return false;
    }

    // truncate the current span of <length> bytes on the lower addresses
    void trunc_span_low(span* prev, span* curr, size_t length)
    {
        if (length == curr->length)
        {
            remove_span(prev, curr);
        }
        else
        {
            curr->base += length;
            curr->length -= length;
        }
    }

    // truncate the current span of <length> bytes on the higher addresses
    void trunc_span_high(span* prev, span* curr, size_t length)
    {
        if (length == curr->length)
        {
            remove_span(prev, curr);
        }
        else
        {
            curr->length -= length;
        }
    }

    // truncate the current span of <length> bytes starting at <base>
    void trunc_span_middle(span* prev, span* curr, vaddr_t base, size_t length)
    {
        if (length == curr->length)
        {
            remove_span(prev, curr);
        }
        else
        {
            span* s = add_span();
            s->base = base + length;
            s->length = curr->base + curr->length - s->base;
            s->next = curr->next;

            curr->length = base - curr->base;
            curr->next = s;
        }
    }

    // remove a sub-span from the current span
    vaddr_t split_span(span* prev, span* curr, size_t length, allocation_flags flags, vaddr_t hint)
    {
        vaddr_t base = (vaddr_t )-1;
        switch (flags)
        {
        case ALLOCATE_ANY:
            // curr  |---------------------| 
            // alloc |------------|
            base = curr->base;
            trunc_span_low(prev, curr, length);
            break;

        case ALLOCATE_EXACT:
            base = hint;
            if (curr->base == hint)
            {
                // curr  h---------------------| 
                // alloc |------------|
                trunc_span_low(prev, curr, length);
            }
            else if (hint + length == curr->base + curr->length)
            {
                // curr  |--------h------------| 
                // alloc          |------------|
                trunc_span_high(prev, curr, length);
            }
            else
            {
                // curr  |-----h---------------| 
                // alloc       |------------|
                trunc_span_middle(prev, curr, hint, length);
            }
            break;

        case ALLOCATE_ABOVE:
            // curr      |----h-----------------| 
            // alloc               |------------|
            base = curr->base + curr->length - length;
            trunc_span_high(prev, curr, length);
            break;

        case ALLOCATE_BELOW:
            // s    |----------------h------------|
            //      |----------|                   
            base = curr->base;
            trunc_span_low(prev, curr, length);
            break;
        }

        return base;
    }

private:
    vaddr_t       _base;
    size_t        _length;
    size_t        _granularity;
    span          _free_mem_root;
    SpanAllocator _spans;
};



// Change the type here to change the strategy for span allocation
typedef span_manager_pool AllocatorStrategy;
//typedef span_manager_allocate AllocatorStrategy;


ralloc_t create_range_allocator(vaddr_t base, size_t length, size_t granularity)
{
    if (!base) return 0;
    if (!length) return 0;
    if (!granularity) return 0;
    if (granularity > length) return 0;

    return new range_allocator<AllocatorStrategy>(base, length, granularity);
}

void destroy_range_allocator(ralloc_t ralloc)
{
    // check here for sanity, but we have no way to check that the pointer is valid 
    // without adding some kind of header/signature
    if (!ralloc) return;

    range_allocator<AllocatorStrategy>* allocator = static_cast<range_allocator<AllocatorStrategy>*>(ralloc);
    delete allocator;
}

vaddr_t allocate_range(ralloc_t ralloc, size_t length, allocation_flags flags, vaddr_t optional_hint)
{
    // check here for sanity, but we have no way to check that the pointer is valid 
    // without adding some kind of header/signature
    if (!ralloc) return (vaddr_t)-1;

    return static_cast<range_allocator<AllocatorStrategy>*>(ralloc)->allocate(length, flags, optional_hint);
}

void free_range(ralloc_t ralloc, vaddr_t base, size_t length)
{
    // check here for sanity, but we have no way to check that the pointer is valid 
    // without adding some kind of header/signature
    if (!ralloc) return;

    static_cast<range_allocator<AllocatorStrategy>*>(ralloc)->free(base, length);
}
