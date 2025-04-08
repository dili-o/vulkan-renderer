#include "String.hpp"
#include "Core/Assert.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"

#include <stdarg.h>

namespace Helix {
void StringBuffer::init(Allocator *allocator_, size_t size) {
  if (data) {
    allocator->deallocate(data);
  }

  if (size < 1) {
    HERROR("Buffer cannot be empty!");
    return;
  }
  allocator = allocator_;
  data = (char *)halloca(size + 1, allocator_);
  HASSERT(data);
  data[0] = 0;
  buffer_size = (u32)size;
  current_size = 0;
}

void StringBuffer::shutdown() {
  hfree(data, allocator);

  buffer_size = current_size = 0;
}

void StringBuffer::append(cstring string) { append_f("%s", string); }

void StringBuffer::append(const StringBuffer &other_buffer) {
  if (other_buffer.current_size == 0) {
    HERROR("Cannot append empty StringBuffer.");
    return;
  }

  if (current_size + other_buffer.current_size >= buffer_size) {
    HERROR("Buffer full! Cannot append StringBuffer");
    return;
  }

  memcpy(&data[current_size], other_buffer.data, other_buffer.current_size);
  current_size += other_buffer.current_size;
}

void StringBuffer::append_m(void *memory, size_t size) {

  if (current_size + size >= buffer_size) {
    HERROR("StringBuffer full! Please allocate more size.");
    return;
  }

  memcpy(&data[current_size], memory, size);
  current_size += (u32)size;
}

void StringBuffer::append_f(cstring format, ...) {
  if (current_size >= buffer_size) {
    HERROR("StringBuffer full! Please allocate more size.");
    return;
  }

  // TODO: safer version!
  va_list args;
  va_start(args, format);
#if defined(_MSC_VER)
  int written_chars = vsnprintf_s(
      &data[current_size], buffer_size - current_size, _TRUNCATE, format, args);
#else
  int written_chars =
      vsnprintf(&data[current_size], buffer_size - current_size, format, args);
#endif

  current_size += written_chars > 0 ? written_chars : 0;
  va_end(args);

  if (written_chars < 0) {
    HERROR("New string too big for current buffer! Please allocate more size.");
  }
}

char *StringBuffer::append_use(cstring string) {
  return append_use_f("%s", string);
}

char *StringBuffer::append_use_f(cstring format, ...) {
  u32 cached_offset = this->current_size;

  // TODO: safer version!
  // TODO: do not copy paste!
  if (current_size >= buffer_size) {
    HERROR("StringBuffer full! Please allocate more size.");
    return nullptr;
  }

  va_list args;
  va_start(args, format);
#if defined(_MSC_VER)
  int written_chars = vsnprintf_s(
      &data[current_size], buffer_size - current_size, _TRUNCATE, format, args);
#else
  int written_chars =
      vsnprintf(&data[current_size], buffer_size - current_size, format, args);
#endif
  current_size += written_chars > 0 ? written_chars : 0;
  va_end(args);

  if (written_chars < 0) {
    HERROR("New string too big for current buffer! Please allocate more size.");
  }

  // Add null termination for string.
  // By allocating one extra character for the null termination this is always
  // safe to do.
  data[current_size] = 0;
  ++current_size;

  return this->data + cached_offset;
}

char *StringBuffer::append_use_substring(cstring string, u32 start_index,
                                         u32 end_index) {
  u32 size = end_index - start_index;
  if (current_size + size >= buffer_size) {
    HERROR("StringBuffer full! Please allocate more size. Current size: {}, String "
           "Size: {}",
           current_size, size);
    return nullptr;
  }

  u32 cached_offset = this->current_size;

  memcpy(&data[current_size], string, size);
  current_size += size;

  data[current_size] = 0;
  ++current_size;

  return this->data + cached_offset;
}

void StringBuffer::close_current_string() { data[current_size++] = 0; }

char *StringBuffer::reserve(size_t size) {
  if (current_size + size >= buffer_size) {
    HERROR("Buffer full! Failed to reserve string!");
    return nullptr;
  }

  u32 offset = current_size;
  current_size += (u32)size;

  return data + offset;
}

void StringBuffer::clear() {
  current_size = 0;
  data[0] = 0;
}
} // namespace Helix
