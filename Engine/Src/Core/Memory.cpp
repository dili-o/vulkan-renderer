#include "Memory.hpp"

#include <string.h>
#include <tlsf.h>

#include "Assert.hpp"
#include "Log.hpp"

#ifndef _WIN32
#define _strdup strdup
#endif

#define HEAP_ALLOCATOR_STATS

// #define DEBUG_ALLOCATIONS

namespace Helix {
#pragma region Memory_Methods ////////////////////////////////////////
char *get_memory_usage_str(size_t size, char *buffer) {
  f32 amount = 0.f;
  char unit[4] = "XiB";

  if (size >= hgiga(1)) {
    unit[0] = 'G';
    amount = (f32)size / (f32)hgiga(1);
  } else if (size >= hmega(1)) {
    unit[0] = 'M';
    amount = (f32)size / (f32)hmega(1);
  } else if (size >= hkilo(1)) {
    unit[0] = 'K';
    amount = (f32)size / (f32)hkilo(1);
  } else {
    unit[0] = 'B';
    unit[1] = ' ';
    unit[2] = ' ';
    amount = (f32)size;
  }

  snprintf(buffer, sizeof(char) * 20, "%.2f %s", amount, unit);
  return buffer;
}

void exit_walker(void *ptr, size_t size, int used, void *user) {
  MemoryStatistics *stats = (MemoryStatistics *)user;
  stats->add(used ? size : 0);

  if (used)
    HERROR("Found active allocation {}, {}", ptr, size);
}
#pragma endregion Memory_Methods /////////////////////////////////////////

#pragma region HeapAllocator /////////////////////////////////////////////
HeapAllocator::~HeapAllocator() {}

void HeapAllocator::init(size_t size) {
  // Allocate
  memory = malloc(size);
  max_size = size;
  allocated_size = 0;

  allocation_mutex.create();

  tlsf_handle = tlsf_create_with_pool(memory, size);

  char str[20];
  HINFO("HeapAllocator of size {} created", get_memory_usage_str(size, str));
}

void HeapAllocator::shutdown() {

  // Check memory at the application exit.
  MemoryStatistics stats{0, max_size};
  pool_t pool = tlsf_get_pool(tlsf_handle);
  tlsf_walk_pool(pool, exit_walker, (void *)&stats);

  if (stats.allocated_bytes) {
    char str[20];
    HERROR("HeapAllocator Shutdown - FAILURE! Allocated memory detected. "
           "allocated {}, total {}",
           get_memory_usage_str(stats.allocated_bytes, str),
           get_memory_usage_str(stats.total_bytes, str));
  } else {
    HINFO("HeapAllocator Shutdown");
  }

  HASSERT_MSG(stats.allocated_bytes == 0,
              "Allocations still present. Check your code!");

  tlsf_destroy(tlsf_handle);

  free(memory);

  allocation_mutex.destroy();
}

void *HeapAllocator::allocate(size_t size, size_t alignment) {
  HASSERT(allocation_mutex.lock());
  if ((size + allocated_size) > max_size) {
    HCRITICAL("HeapAllocator has ran out of memory!");
  }
#if defined(HEAP_ALLOCATOR_STATS)
  void *allocated_memory = alignment == 1
                               ? tlsf_malloc(tlsf_handle, size)
                               : tlsf_memalign(tlsf_handle, alignment, size);
  size_t actual_size = tlsf_block_size(allocated_memory);
  allocated_size += actual_size;

  HASSERT(allocation_mutex.unlock());
  return allocated_memory;
#else
  return tlsf_malloc(tlsf_handle, size);
  HASSERT(allocation_mutex.unlock());
#endif // HEAP_ALLOCATOR_STATS
}

void *HeapAllocator::allocate(size_t size, size_t alignment, cstring file,
                              i32 line) {
  void *allocation = allocate(size, alignment);
#ifdef DEBUG_ALLOCATIONS
  HDEBUG("Allocation pointer: {}, in file: {}, in line: {}", allocation, file,
         line);
#endif
  return allocation;
}

void HeapAllocator::deallocate(void *pointer) {
  HASSERT(allocation_mutex.lock());
#if defined(HEAP_ALLOCATOR_STATS)
  size_t actual_size = tlsf_block_size(pointer);
  allocated_size -= actual_size;

  tlsf_free(tlsf_handle, pointer);
  HASSERT(allocation_mutex.unlock());
#else
  tlsf_free(tlsf_handle, pointer);
  HASSERT(allocation_mutex.unlock());
#endif
}
#pragma endregion HeapAllocator //////////////////////////////////////

#pragma region StackAllocator ////////////////////////////////////////
void StackAllocator::init(size_t size) {
  memory = (u8 *)malloc(size);
  allocated_size = 0;
  total_size = size;

  char str[20];
  HINFO("StackAllocator of size {} created", get_memory_usage_str(size, str));
  allocation_mutex.create();
}

void StackAllocator::shutdown() {
  free(memory);
  allocation_mutex.destroy();
  HINFO("StackAllocator Shutdown");
}

void *StackAllocator::allocate(size_t size, size_t alignment) {
  HASSERT(allocation_mutex.lock());
  HASSERT_MSG(size > 0, "Attempting to allocate 0 bytes!");

  const size_t new_start = memory_align(allocated_size, alignment);
  HASSERT_MSG(new_start < total_size, "New allocation exceeds the total size");
  const size_t new_allocated_size = new_start + size;
  HASSERT_MSG(new_allocated_size < total_size,
              "StackAllocator: New allocation exceeds the total size");

  allocated_size = new_allocated_size;
  void *new_memory = memory + new_start;
  HASSERT(allocation_mutex.unlock());
  return new_memory;
}

void *StackAllocator::allocate(size_t size, size_t alignment, cstring file,
                               i32 line) {
  return allocate(size, alignment);
}

void StackAllocator::deallocate(void *pointer) {}

size_t StackAllocator::get_marker() { return allocated_size; }

void StackAllocator::free_marker(size_t marker) {
  const size_t difference = marker - allocated_size;
  if (difference > 0) {
    allocated_size = marker;
  }
}

void StackAllocator::clear() { allocated_size = 0; }

#pragma endregion StackAllocator /////////////////////////////////////

#pragma region MemoryService

static MemoryService *s_memory_service{nullptr};

MemoryService *MemoryService::instance() { return s_memory_service; }

void MemoryService::init(void *config_) {
  if (s_memory_service) {
    HELIX_SERVICE_RECREATE_MSG(MemoryService);
    return;
  }
  HELIX_SERVICE_INIT_MSG(MemoryService);
  MemoryServiceConfiguration *config = (MemoryServiceConfiguration *)config_;

  system_allocator.init(config ? config->heap_size : hmega(32));
  stack_allocator.init(config ? config->stack_size : hmega(32));
  s_memory_service = this;
}

void MemoryService::shutdown() {
  system_allocator.shutdown();
  stack_allocator.shutdown();

  HELIX_SERVICE_SHUTDOWN_MSG(MemoryService);
}

#pragma endregion MemoryService
} // namespace Helix
