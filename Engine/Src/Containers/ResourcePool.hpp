#pragma once
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"

namespace Helix {

#define MIN_POOL_CAPACITY 4

static const u32 k_invalid_index = 0xffffffff;

struct ResourceHandle {
  u32 index{k_invalid_index};
  u32 generation{0};
};

template <typename T> struct ResourcePool {

  void init(Allocator *allocator, u32 pool_capacity);
  void shutdown();
  void grow();

  ResourceHandle obtain_new();
  T *obtain(ResourceHandle handle);
  const T *obtain(ResourceHandle handle) const;

  void release(ResourceHandle handle);

  u8 *memory = nullptr;
  u32 *free_indices = nullptr;
  u32 *generations = nullptr;
  Allocator *allocator = nullptr;

  u32 free_indices_head = 0;
  u32 capacity = MIN_POOL_CAPACITY;
  u32 size = 0;
}; // struct ResourcePool

template <typename T>
inline void ResourcePool<T>::init(Allocator *allocator_, u32 pool_capacity_) {
  allocator = allocator_;
  capacity =
      pool_capacity_ < MIN_POOL_CAPACITY ? MIN_POOL_CAPACITY : pool_capacity_;

  // Group allocate ( resource size + u32 )
  size_t allocation_size = capacity * (sizeof(T) + sizeof(ResourceHandle));
  memory = hallocam(allocation_size, allocator);
  memset(memory, 0, allocation_size);

  // Allocate and add free indices
  free_indices = (u32 *)(memory + capacity * sizeof(T));
  free_indices_head = 0;

  generations = free_indices + capacity;

  for (u32 i = 0; i < capacity; ++i) {
    free_indices[i] = i;
    generations[i] = 0; // I know it's already zeroed out.
  }

  size = 0;
}

template <typename T> inline void ResourcePool<T>::shutdown() {
  if (free_indices_head != 0) {
    HERROR("Resource pool has unfreed resources. {} resources unfreed!", size);
  }

  HASSERT(size == 0);
  allocator->deallocate(memory);
}

template <typename T> inline void ResourcePool<T>::grow() {
  u32 new_capacity = capacity * 2;

  size_t allocation_size = new_capacity * (sizeof(T) + sizeof(ResourceHandle));

  u8 *new_memory = hallocam(allocation_size, allocator);
  memset(new_memory, 0, allocation_size);

  u32 *new_free_indices = (u32 *)(new_memory + new_capacity * sizeof(T));

  u32 *new_generations = new_free_indices + new_capacity;

  // Copy the resources into new memory
  memory_copy((void *)new_memory, (void *)memory, capacity * sizeof(T));
  // Copy the free indices into new memory
  memory_copy((void *)new_free_indices, (void *)free_indices,
              sizeof(u32) * capacity);
  // Copy the generations into new memory
  memory_copy((void *)new_generations, (void *)generations,
              sizeof(u32) * capacity);

  for (u32 i = size; i < new_capacity; i++) {
    new_free_indices[i] = i;
    new_generations[i] = 0; // Also zeroed
  }

  allocator->deallocate(memory);

  capacity = new_capacity;
  memory = new_memory;
  free_indices = new_free_indices;
  generations = new_generations;
}

template <typename T> inline ResourceHandle ResourcePool<T>::obtain_new() {
  if (free_indices_head < capacity) {
    u32 free_index = free_indices[free_indices_head++];
    const ResourceHandle free_handle = {free_index, generations[free_index]};

    ++size;
    return free_handle;
  }
  // Error: no more resources left!
  HWARN("No more resources left, creating a larger pool");
  grow();
  return obtain_new();
}

template <typename T> inline T *ResourcePool<T>::obtain(ResourceHandle handle) {
  if (handle.index != k_invalid_index) {
    if (handle.generation == generations[handle.index]) {
      return (T *)&memory[handle.index * sizeof(T)];
    } else {
      HERROR("Generation mismatch in Handle");
      return nullptr;
    }
  }
  HERROR("Invalid Handle index!");
  return nullptr;
}

template <typename T>
inline const T *ResourcePool<T>::obtain(ResourceHandle handle) const {
  if (handle.index != k_invalid_index) {
    if (handle.generation == generations[handle.index]) {
      return &memory[handle.index * sizeof(T)];
    } else {
      HERROR("Generation mismatch in Handle");
      return nullptr;
    }
  }
  HERROR("Invalid Handle index!");
  return nullptr;
}

template <typename T>
inline void ResourcePool<T>::release(ResourceHandle handle) {
  if (handle.index != k_invalid_index &&
      handle.generation == generations[handle.index]) {
    free_indices[--free_indices_head] = handle.index;
    --size;
    ++generations[handle.index];
    return;
  }
  HWARN("Attempting to release invalid index");
}
} // namespace Helix
