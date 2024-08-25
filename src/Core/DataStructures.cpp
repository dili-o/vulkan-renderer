#include "DataStructures.hpp"

#include <string.h>

namespace Helix {

    static const u32                    k_invalid_index = 0xffffffff;

    // Resource Pool ////////////////////////////////////////////////////////////////

    void ResourcePool::init(Allocator* allocator_, u32 pool_size_, u32 resource_size_) {

        allocator = allocator_;
        pool_size = pool_size_;
        resource_size = resource_size_;

        // Group allocate ( resource size + u32 )
        sizet allocation_size = pool_size * (resource_size + sizeof(u32));
        memory = hallocam(allocation_size, allocator);
        memset(memory, 0, allocation_size);

        // Allocate and add free indices
        free_indices = (u32*)(memory + pool_size * resource_size);
        free_indices_head = 0;

        for (u32 i = 0; i < pool_size; ++i) {
            free_indices[i] = i;
        }

        used_indices = 0;
    }

    void ResourcePool::shutdown() {

        if (free_indices_head != 0) {
            HERROR("Resource pool has unfreed resources.");

            for (u32 i = 0; i < free_indices_head; ++i) {
                HERROR("\tResource {}", free_indices[i]);
            }
        }

        HASSERT(used_indices == 0);

        allocator->deallocate(memory);
    }

    void ResourcePool::grow(){
        u32 new_pool_size = pool_size * 2;

        sizet allocation_size = new_pool_size * (resource_size + sizeof(u32));

        u8* new_memory = hallocam(allocation_size, allocator);
        memset(new_memory, 0, allocation_size);

        memory_copy(new_memory, memory, pool_size * resource_size);

        u32* new_free_indicies = (u32*)(new_memory + new_pool_size * resource_size);
        memory_copy(new_free_indicies, free_indices, used_indices);

        for (u32 i = used_indices; i < new_pool_size; i++) {
            new_free_indicies[i] = i;
        }

        allocator->deallocate(memory);

        pool_size = new_pool_size;
        memory = new_memory;
        free_indices = new_free_indicies;
    }

    void ResourcePool::free_all_resources() {
        free_indices_head = 0;
        used_indices = 0;

        for (u32 i = 0; i < pool_size; ++i) {
            free_indices[i] = i;
        }
    }

    u32 ResourcePool::obtain_resource() {
        // TODO: add bits for checking if resource is alive and use bitmasks.
        if (free_indices_head < pool_size) {
            const u32 free_index = free_indices[free_indices_head++];
            ++used_indices;
            return free_index;
        }
        // Error: no more resources left!
        HWARN("No more resources left, creating a larger pool");
        grow();
        return obtain_resource();
        //return k_invalid_index;
    }

    void ResourcePool::release_resource(u32 index) {
        // TODO: add bits for checking if resource is alive and use bitmasks.
        free_indices[--free_indices_head] = index;
        --used_indices;
    }

    void* ResourcePool::access_resource(u32 index) {
        if (index != k_invalid_index) {
            return &memory[index * resource_size];
        }
        return nullptr;
    }

    const void* ResourcePool::access_resource(u32 index) const {
        if (index != k_invalid_index) {
            return &memory[index * resource_size];
        }
        return nullptr;
    }


} // namespace Helix
