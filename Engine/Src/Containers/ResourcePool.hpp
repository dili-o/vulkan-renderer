#pragma once
#include "Core/Memory.hpp"

namespace Helix {

struct ResourceHandle {
  u32 index : 20;
  u32 generation : 12;
};

template <typename T> struct ResourcePool {

  void init(Allocator *allocator, u32 pool_size);
  void shutdown();
  void grow();

  ResourceHandle *obtain_new();
  void release(T *resource);
  void release_all();

  T *obtain(ResourceHandle handle);
  const T *obtain(ResourceHandle handle) const;

  u8 *memory = nullptr;
  ResourceHandle *free_indices = nullptr;
  Allocator *allocator = nullptr;

  u32 free_indices_head = 0;
  u32 pool_size = 16;
  u32 used_indices = 0;
}; // struct ResourcePool

template <typename T>
inline void ResourcePool<T>::init(Allocator *allocator_, u32 pool_size_) {
  allocator = allocator_;
  pool_size = pool_size_;

  // Group allocate ( resource size + u32 )
  size_t allocation_size = pool_size * (sizeof(T) + sizeof(ResourceHandle));
  memory = hallocam(allocation_size, allocator);
  memset(memory, 0, allocation_size);

  // Allocate and add free indices
  free_indices = (ResourceHandle *)(memory + pool_size * sizeof(T));
  free_indices_head = 0;

  for (u32 i = 0; i < pool_size; ++i) {
    free_indices[i].index = i;
    free_indices[i].generation = 0; // I know it's already zeroed out.
  }

  used_indices = 0;
}

} // namespace Helix
