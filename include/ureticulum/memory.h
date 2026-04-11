#pragma once

#include <stddef.h>
#include <stdint.h>
#include <new>

#include "ureticulum/log.h"

#include "tlsf.h"

#define RNS_HEAP_ALLOCATOR      0
#define RNS_HEAP_POOL_ALLOCATOR 1

#ifndef RNS_DEFAULT_ALLOCATOR
#define RNS_DEFAULT_ALLOCATOR RNS_HEAP_ALLOCATOR
#endif

#ifndef RNS_CONTAINER_ALLOCATOR
#define RNS_CONTAINER_ALLOCATOR RNS_HEAP_ALLOCATOR
#endif

#ifndef RNS_HEAP_POOL_BUFFER_SIZE
#define RNS_HEAP_POOL_BUFFER_SIZE 0
#endif

namespace RNS { namespace Utilities {

    class Memory {
    public:
        struct pool_info {
            pool_info(size_t s) : buffer_size(s) {}
            bool     pool_init    = false;
            size_t   buffer_size  = 0;
            tlsf_t   tlsf         = nullptr;
            uint32_t alloc_fault  = 0;
            uint32_t free_fault   = 0;
        };

        struct allocator_info {
            uint32_t alloc_count    = 0;
            uint32_t free_count     = 0;
            uint64_t alloc_size     = 0;
            size_t   min_alloc_size = 0;
            size_t   max_alloc_size = 0;
        };

        static void  pool_init(pool_info& pi);
        static void* pool_malloc(pool_info& pi, size_t size);
        static void  pool_free(pool_info& pi, void* p) noexcept;

        static pool_info      heap_pool_info;
        static allocator_info default_allocator_info;
        static allocator_info container_allocator_info;

        template <typename T>
        struct ContainerAllocator {
            using value_type = T;

            ContainerAllocator() noexcept = default;
            template <class U> ContainerAllocator(const ContainerAllocator<U>&) noexcept {}

            T* allocate(std::size_t n) {
                if (n == 0) return nullptr;
                size_t size = n * sizeof(value_type);
                ++container_allocator_info.alloc_count;
                container_allocator_info.alloc_size += size;
                if (size < container_allocator_info.min_alloc_size || container_allocator_info.min_alloc_size == 0)
                    container_allocator_info.min_alloc_size = size;
                if (size > container_allocator_info.max_alloc_size)
                    container_allocator_info.max_alloc_size = size;

#if RNS_CONTAINER_ALLOCATOR == RNS_HEAP_POOL_ALLOCATOR
                void* p = pool_malloc(heap_pool_info, size);
#else
                void* p = ::operator new(size);
#endif
                if (p == nullptr) throw std::bad_alloc();
                return static_cast<T*>(p);
            }

            void deallocate(T* p, std::size_t n) noexcept {
                if (p == nullptr) return;
                size_t size = n * sizeof(value_type);
                ++container_allocator_info.free_count;
                container_allocator_info.alloc_size -= size;
#if RNS_CONTAINER_ALLOCATOR == RNS_HEAP_POOL_ALLOCATOR
                pool_free(heap_pool_info, p);
#else
                ::operator delete(p);
#endif
            }

            bool operator==(const ContainerAllocator&) const noexcept { return true; }
            bool operator!=(const ContainerAllocator&) const noexcept { return false; }
        };
    };

}}
