#include "ureticulum/memory.h"

#include <stdlib.h>

namespace RNS { namespace Utilities {

Memory::pool_info      Memory::heap_pool_info(RNS_HEAP_POOL_BUFFER_SIZE);
Memory::allocator_info Memory::default_allocator_info;
Memory::allocator_info Memory::container_allocator_info;

void Memory::pool_init(Memory::pool_info& pi) {
    if (pi.pool_init) return;
    pi.pool_init = true;

    if (pi.buffer_size == 0) pi.buffer_size = 1024 * 1024;

    size_t align = tlsf_align_size();
    pi.buffer_size &= ~(align - 1);

    void* raw = malloc(pi.buffer_size);
    if (raw == nullptr) { ERROR("TLSF: pool buffer allocation failed"); return; }

    pi.tlsf = tlsf_create_with_pool(raw, pi.buffer_size);
    if (pi.tlsf == nullptr) ERROR("TLSF: pool initialization failed");
}

void* Memory::pool_malloc(Memory::pool_info& pi, size_t size) {
    if (size == 0) return nullptr;
    if (!pi.pool_init) pool_init(pi);

    if (pi.tlsf != nullptr) return tlsf_malloc(pi.tlsf, size);
    ++pi.alloc_fault;
    return malloc(size);
}

void Memory::pool_free(Memory::pool_info& pi, void* p) noexcept {
    if (p == nullptr) return;
    if (pi.tlsf != nullptr) { tlsf_free(pi.tlsf, p); return; }
    ++pi.free_fault;
    free(p);
}

}}

#if RNS_DEFAULT_ALLOCATOR == RNS_HEAP_POOL_ALLOCATOR

using RNS::Utilities::Memory;

static void* allocator_malloc(size_t size) {
    if (size == 0) return nullptr;
    ++Memory::default_allocator_info.alloc_count;
    Memory::default_allocator_info.alloc_size += size;
    if (size < Memory::default_allocator_info.min_alloc_size || Memory::default_allocator_info.min_alloc_size == 0)
        Memory::default_allocator_info.min_alloc_size = size;
    if (size > Memory::default_allocator_info.max_alloc_size)
        Memory::default_allocator_info.max_alloc_size = size;
    return Memory::pool_malloc(Memory::heap_pool_info, size);
}

static void allocator_free(void* p) noexcept {
    if (p == nullptr) return;
    ++Memory::default_allocator_info.free_count;
    Memory::pool_free(Memory::heap_pool_info, p);
}

void* operator new(size_t size)              { void* p = allocator_malloc(size); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t size)            { void* p = allocator_malloc(size); if (!p) throw std::bad_alloc(); return p; }
void* operator new(size_t size, const std::nothrow_t&)   noexcept { return allocator_malloc(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocator_malloc(size); }

void operator delete(void* p)                noexcept { allocator_free(p); }
void operator delete[](void* p)              noexcept { allocator_free(p); }
void operator delete(void* p, size_t)        noexcept { allocator_free(p); }
void operator delete[](void* p, size_t)      noexcept { allocator_free(p); }
void operator delete(void* p, const std::nothrow_t&)   noexcept { allocator_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { allocator_free(p); }

#endif
