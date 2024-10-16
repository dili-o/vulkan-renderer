#pragma once

#include "Core/Service.hpp"

#define HELIX_IMGUI

namespace Helix {

    // Memory Methods /////////////////////////////////////////////////////
    void                            memory_copy(void* destination, void* source, sizet size);

    //
    //  Calculate aligned memory size.
    sizet                           memory_align(sizet size, sizet alignment);

    // Memory Structs /////////////////////////////////////////////////////
    //
    //
    struct MemoryStatistics {
        sizet                       allocated_bytes;
        sizet                       total_bytes;

        u32                         allocation_count;

        void add(sizet a) {
            if (a) {
                allocated_bytes += a;
                ++allocation_count;
            }
        }
    }; // struct MemoryStatistics

    //
    //
    struct Allocator {
        virtual                     ~Allocator() { }
        virtual void*               allocate(sizet size, sizet alignment) = 0;
        virtual void*               allocate(sizet size, sizet alignment, cstring file, i32 line) = 0;

        virtual void                deallocate(void* pointer) = 0;
    }; // struct Allocator


    //
    //
    struct HeapAllocator : public Allocator {

        ~HeapAllocator() override;

        void                        init(sizet size);
        void                        shutdown();

#if defined HELIX_IMGUI
        void                        debug_ui();
#endif // HELIX_IMGUI

        void*                       allocate(sizet size, sizet alignment) override;
        void*                       allocate(sizet size, sizet alignment, cstring file, i32 line) override;

        void                        deallocate(void* pointer) override;

        void*                       tlsf_handle;
        void*                       memory;
        sizet                       allocated_size = 0;
        sizet                       max_size = 0;

    }; // struct HeapAllocator

    //
    //
    struct StackAllocator : public Allocator {

        void                        init(sizet size);
        void                        shutdown();

        void*                       allocate(sizet size, sizet alignment) override;
        void*                       allocate(sizet size, sizet alignment, cstring file, i32 line) override;

        void                        deallocate(void* pointer) override;

        sizet                       get_marker();
        void                        free_marker(sizet marker);

        void                        clear();

        u8*                         memory = nullptr;
        sizet                       total_size = 0;
        sizet                       allocated_size = 0;

    }; // struct StackAllocator

    //
    //
    struct DoubleStackAllocator : public Allocator {

        void                        init(sizet size);
        void                        shutdown();

        void*                       allocate(sizet size, sizet alignment) override;
        void*                       allocate(sizet size, sizet alignment, cstring file, i32 line) override;
        void                        deallocate(void* pointer) override;

        void*                       allocate_top(sizet size, sizet alignment);
        void*                       allocate_bottom(sizet size, sizet alignment);

        void                        deallocate_top(sizet size);
        void                        deallocate_bottom(sizet size);

        sizet                       get_top_marker();
        sizet                       get_bottom_marker();

        void                        free_top_marker(sizet marker);
        void                        free_bottom_marker(sizet marker);

        void                        clear_top();
        void                        clear_bottom();

        u8*                         memory = nullptr;
        sizet                       total_size = 0;
        sizet                       top = 0;
        sizet                       bottom = 0;

    }; // struct DoubleStackAllocator

    //
    // Allocator that can only be reset.
    //
    struct LinearAllocator : public Allocator {

        ~LinearAllocator();

        void                        init(sizet size);
        void                        shutdown();

        void*                       allocate(sizet size, sizet alignment) override;
        void*                       allocate(sizet size, sizet alignment, cstring file, i32 line) override;

        void                        deallocate(void* pointer) override;

        void                        clear();

        u8*                         memory = nullptr;
        sizet                       total_size = 0;
        sizet                       allocated_size = 0;
    }; // struct LinearAllocator

    //
    // DANGER: this should be used for NON runtime processes, like compilation of resources.
    struct MallocAllocator : public Allocator {
        void*                       allocate(sizet size, sizet alignment) override;
        void*                       allocate(sizet size, sizet alignment, cstring file, i32 line) override;

        void                        deallocate(void* pointer) override;
    };

    // Memory Service /////////////////////////////////////////////////////
    // 
    // 
    struct MemoryServiceConfiguration {

        sizet                       maximum_dynamic_size = 32 * 1024 * 1024;    // Defaults to max 32MB of dynamic memory.

    }; // struct MemoryServiceConfiguration
    //
    //
    struct MemoryService : public Service {

        HELIX_DECLARE_SERVICE(MemoryService);

        void                        init(void* configuration);
        void                        shutdown();

#if defined HELIX_IMGUI
        void                        imgui_draw();
#endif // HELIX_IMGUI

        // Frame allocator
        LinearAllocator             stack_allocator;
        HeapAllocator               system_allocator;

        //
        // Test allocators.
        void                        test();

        static constexpr cstring    k_name = "helix_memory_service";

    }; // struct MemoryService

    // Macro helpers //////////////////////////////////////////////////////
#define halloca(size, allocator)    ((allocator)->allocate( size, 1, __FILE__, __LINE__ ))
#define hallocam(size, allocator)   ((u8*)(allocator)->allocate( size, 1, __FILE__, __LINE__ ))
#define hallocat(type, allocator)   ((type*)(allocator)->allocate( sizeof(type), 1, __FILE__, __LINE__ ))

#define hallocaa(size, allocator, alignment)    ((allocator)->allocate( size, alignment, __FILE__, __LINE__ ))

#define hfree(pointer, allocator) (allocator)->deallocate(pointer)

#define hkilo(size)                 (size * 1024)
#define hmega(size)                 (size * 1024 * 1024)
#define hgiga(size)                 (size * 1024 * 1024 * 1024)

} // namespace Helix
