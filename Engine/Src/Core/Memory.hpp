
#pragma once

#include "Defines.hpp"
#include "Platform/HMutex.hpp"
#include "Service.hpp"
#include <string.h>

namespace Helix {

// Memory Methods /////////////////////////////////////////////////////
inline void memory_copy(void *destination, void *source, size_t size) {
  memcpy(destination, source, size);
}
//
//  Calculate aligned memory size.
inline size_t memory_align(size_t size, size_t alignment) {
  const size_t alignment_mask = alignment - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

char *get_memory_usage_str(size_t size, char *buffer);

// Memory Structs /////////////////////////////////////////////////////
//
//
struct MemoryStatistics {
  size_t allocated_bytes;
  size_t total_bytes;

  u32 allocation_count;

  void add(size_t a) {
    if (a) {
      allocated_bytes += a;
      ++allocation_count;
    }
  }
}; // struct MemoryStatistics

//
//
struct Allocator {
  virtual ~Allocator() {}
  virtual void *allocate(size_t size, size_t alignment) = 0;
  virtual void *allocate(size_t size, size_t alignment, cstring file,
                         i32 line) = 0;

  virtual void deallocate(void *pointer) = 0;

  HMutex allocation_mutex{};
}; // struct Allocator

//
//
struct HLX_API HeapAllocator : public Allocator {

  ~HeapAllocator() override;

  void init(size_t size);
  void shutdown();

  void *allocate(size_t size, size_t alignment) override;
  void *allocate(size_t size, size_t alignment, cstring file,
                 i32 line) override;

  void deallocate(void *pointer) override;

  void *tlsf_handle;
  void *memory;
  size_t allocated_size = 0;
  size_t max_size = 0;

}; // struct HeapAllocator

//
//
struct HLX_API StackAllocator : public Allocator {

  void init(size_t size);
  void shutdown();

  void *allocate(size_t size, size_t alignment) override;
  void *allocate(size_t size, size_t alignment, cstring file,
                 i32 line) override;

  void deallocate(void *pointer) override;

  size_t get_marker();
  void free_marker(size_t marker);

  void clear();

  u8 *memory = nullptr;
  size_t total_size = 0;
  size_t allocated_size = 0;

}; // struct StackAllocator

//
//
struct HLX_API ScopedAllocator {
  ScopedAllocator(StackAllocator *stack_allocator_)
      : allocator(stack_allocator_),
        stack_marker(stack_allocator_->get_marker()) {}

  ~ScopedAllocator() {
    allocator->free_marker(stack_marker);
    stack_marker = 0;
    allocator = nullptr;
  }

  StackAllocator *allocator = nullptr;
  size_t stack_marker = 0;
}; // struct ScopeAllocator

// Memory Service /////////////////////////////////////////////////////
//
//
struct MemoryServiceConfiguration {

  size_t heap_size{0};
  size_t stack_size{0};
}; // struct MemoryServiceConfiguration
//
//
struct HLX_API MemoryService : public Service {

  HELIX_DECLARE_SERVICE(MemoryService);

  void init(void *configuration);
  void shutdown();

  // Frame allocator
  StackAllocator stack_allocator;
  HeapAllocator system_allocator;

  static constexpr cstring k_name = "helix_memory_service";

}; // struct MemoryService

// Macro helpers //////////////////////////////////////////////////////
#define halloca(size, allocator)                                               \
  ((allocator)->allocate(size, 1, __FILE__, __LINE__))
#define hallocam(size, allocator)                                              \
  ((u8 *)(allocator)->allocate(size, 1, __FILE__, __LINE__))
#define hallocat(type, allocator)                                              \
  ((type *)(allocator)->allocate(sizeof(type), 1, __FILE__, __LINE__))

#define hallocaa(size, allocator, alignment)                                   \
  ((allocator)->allocate(size, alignment, __FILE__, __LINE__))

#define hfree(pointer, allocator) (allocator)->deallocate(pointer)

#define hkilo(size) (size * 1024)
#define hmega(size) (size * 1024 * 1024)
#define hgiga(size) (size * 1024 * 1024 * 1024)

} // namespace Helix
