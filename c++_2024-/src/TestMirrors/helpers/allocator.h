#ifndef __WASTELADNS_ALLOCATOR_H__
#define __WASTELADNS_ALLOCATOR_H__

namespace allocator {

// for allocators that are passed by copy, we store a separate pointer to their highmark value
// so we can keep track of the highest allocation at any time. When using virtual memory, this value
// is necessary to know when to commit more pages, while in other cases the value is useful for
// debug purposes
#define TRACK_HIGHMARKS USE_VIRTUAL_MEMORY || __DEBUG
#if TRACK_HIGHMARKS
#define __HIGHMARKS_DEF(...) __VA_ARGS__
#else
#define __HIGHMARKS_DEF(...)
#endif

// Simple arena buffer header: only the current offset is stored, as well as the capacity
// It can be used as a stack allocator by simply copying this header into a scoped variable
// For more details, see: https://nullprogram.com/blog/2023/09/27/
struct Arena {
    u8* curr;
    u8* end;
    __HIGHMARKS_DEF(uintptr_t* highmark;) // pointer to track overall allocations of scoped copies
};
#if USE_VIRTUAL_MEMORY
void init_arena(Arena& arena, u8* mem, size_t capacity) {
    // ignore parameters, reserve 4GB of virtual memory
    (void)mem;
    const size_t capacity_aligned = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    arena.curr = (u8*)platform::mem_reserve(capacity_aligned);
    arena.end = arena.curr;
    __HIGHMARKS_DEF(arena.highmark = nullptr;)
}
void* alloc_arena(Arena& arena, ptrdiff_t size, ptrdiff_t align) {
    assert((align & (align - 1)) == 0); // Alignment needs to be a power of two
    uintptr_t curr_aligned = ((uintptr_t)arena.curr + (align - 1)) & -align;
    // no bounds check when using virtual memory, if we commit more than 4GB we'll just crash
    uintptr_t end_aligned = curr_aligned + size;
    arena.curr = (u8*)end_aligned;
    uintptr_t highmark = (uintptr_t)arena.end;
    #if TRACK_HIGHMARKS
    if (arena.highmark) { highmark = math::max(*arena.highmark, highmark); };
    #endif
    // figure out highest memory accessed so far, and commit if needed
    if (end_aligned > highmark) {
        const ptrdiff_t pagesize = 4 * 1024;
        const size_t commitsize = end_aligned - highmark;
        size_t commitsize_aligned = (commitsize + (pagesize - 1)) & -pagesize;
        platform::mem_commit((void*)highmark, commitsize_aligned);
        arena.end = (u8*)(highmark + commitsize_aligned);
        #if TRACK_HIGHMARKS
        if (arena.highmark) { *arena.highmark = (uintptr_t)arena.end; }
        #endif
    }
    return (void*)curr_aligned;
}
#else
void init_arena(Arena& arena, u8 * mem, size_t capacity) {
    // ignore parameters, reserve 4GB of virtual memory
    arena.curr = mem;
    arena.end = arena.curr + capacity;
    __HIGHMARKS_DEF(arena.highmark = nullptr;)
}
void* alloc_arena(Arena& arena, ptrdiff_t size, ptrdiff_t align) {
    assert((align & (align - 1)) == 0); // Alignment needs to be a power of two
    uintptr_t curr_aligned = ((uintptr_t)arena.curr + (align - 1)) & -align;
    if (curr_aligned + size <= (uintptr_t)arena.end) {
        arena.curr = (u8*)(curr_aligned + size);
        #if TRACK_HIGHMARKS
        if (arena.highmark) {*arena.highmark = math::max(*arena.highmark, (uintptr_t)arena.curr); };
        #endif
        return (void*)curr_aligned;
    }
    assert(0); // out of memory: consider defining USE_VIRTUAL_MEMORY to see how much we actually use
    return 0;
}
#endif
void* realloc_arena(Arena& arena, void* oldptr, ptrdiff_t oldsize, ptrdiff_t newsize, ptrdiff_t align) {
    void* oldbuff = (void*)((uintptr_t)arena.curr - oldsize);
    if (oldbuff == oldptr) {
        return alloc_arena(arena, newsize - oldsize, align);
    } else {
        void* data = alloc_arena(arena, newsize, align);
        if (oldsize) memcpy(data, oldptr, oldsize);
        return data;
    }
}
void free_arena(Arena& arena, void* ptr, ptrdiff_t size) {
    // because of alignment differences between allocations, we don't have enough information
    // to guarantee freeing all allocations. However, we can still try and see if the requested
    // pointer coincides with the last allocation
    void* oldbuff = (void*)((uintptr_t)arena.curr - size);
    if (oldbuff == ptr) {
        arena.curr = (u8*)oldbuff;
    }
}

// Dynamic array. When used with the same arena without any external allocations in between calls
// to allocator::push, the array will continue to grow in place. Note that, in any other case,
// calls to allocator::grow will not free the previous array (this is useful if, for example, the
// array is initialized with some stack-stored data, but is intended to grow past it)
// For more details, see https://nullprogram.com/blog/2023/10/05/
struct Buffer_t {
    u8* data;
    ptrdiff_t len;
    ptrdiff_t cap;
};
void grow(Buffer_t& b, ptrdiff_t size, ptrdiff_t align, Arena& arena) {
    ptrdiff_t doublecap = b.cap ? 2 * b.cap : 2;
    if (b.data + size * b.cap == arena.curr) {
        alloc_arena(arena, size * b.cap, align);
    } else {
        u8* data = (u8*)alloc_arena(arena, doublecap * size, align);
        if (b.len) { memcpy(data, b.data, size * b.len); }
        b.data = data;
    }
    b.cap = doublecap;
}
u8& push(Buffer_t& b, ptrdiff_t size, ptrdiff_t align, Arena& arena) {
    if (b.len >= b.cap) { grow(b, size, align, arena); }
    return *(b.data + size * b.len++);
}
void reserve(Buffer_t& b, ptrdiff_t cap, ptrdiff_t size, ptrdiff_t align, Arena& arena) {
    assert(b.cap == 0); // buffer is not empty
    b.data = (u8*)alloc_arena(arena, cap * size, align);
    b.len = 0;
    b.cap = cap;
}
template<typename T>
struct Buffer {
    T* data;
    ptrdiff_t len;
    ptrdiff_t cap;
};
template<typename T>
T& push(Buffer<T>& b, Arena& arena) {
    if (b.len >= b.cap) { grow(*(Buffer_t*)&b, sizeof(T), alignof(T), arena); }
    return *(b.data + b.len++);
}
template<typename T>
void reserve(Buffer<T>& b, ptrdiff_t cap, Arena& arena) {
    assert(b.cap == 0); // buffer is not empty
    b.data = (T*)alloc_arena(arena, sizeof(T) * cap, alignof(T));
    b.len = 0;
    b.cap = cap;
}

// tmp pool: mostly untested
template<typename T>
struct Pool {
    // important: _T must be the first member of the union, so we can assume _T* = Slot<_T>*
    struct Slot {
        union {
            T live;
            Slot* next;
        } state;
        u32 alive;
    };
    __DEBUGDEF(const char* name;)
    Slot* data;
	Slot* firstAvailable;
	ptrdiff_t cap;
    ptrdiff_t count;
};
template<typename T>
void init_pool(Pool<T>& pool, ptrdiff_t cap, Arena& arena) {
	typedef typename Pool<T>::Slot Slot;
	pool.cap = cap;
    pool.data = (Slot*)allocator::alloc_arena(arena, sizeof(Slot) * cap, alignof(Slot));
    pool.firstAvailable = pool.data;
	for (u32 i = 0; i < cap - 1; i++)
    { pool.data[i].state.next = &(pool.data[i+1]); pool.data[i].alive = 0; }
	pool.data[cap - 1].state.next = nullptr; pool.data[cap - 1].alive = 0;
	pool.count = 0;
}
template<typename T>
T&  alloc_pool(Pool<T>& pool) {
    assert(pool.firstAvailable); // can't regrow without messing up existing pointers to the pool
    T& out = pool.firstAvailable->state.live;
    pool.firstAvailable->alive = 1;
    pool.firstAvailable = pool.firstAvailable->state.next;
	pool.count++;
    return out;
}
template<typename T>
void free_pool(Pool<T>& pool, T& slot) {
    typedef typename Pool<T>::Slot Slot;
    assert((Slot*)slot >= &pool.data && (Slot*)slot < &pool.data + pool.cap); // object didn't come from this pool
    ((Slot*)&slot)->state.next = pool.firstAvailable;
    ((Slot*)&slot)->alive = 0;
    pool.firstAvailable = (Slot*)slot;
	pool.count--;
}
template<typename T>
u32 get_pool_index(Pool<T>& pool, T& slot)
{ return (u32)(((typename Pool<T>::Slot*)&slot) - pool.data); }
template<typename T>
T& get_pool_slot(Pool<T>& pool, const u32 index) { return pool.data[index].state.live; }

}


#endif // __WASTELADNS_ALLOCATOR_H__
