#pragma once

#include "Defines.hpp"

namespace Helix {
struct Allocator;

char *string_concat(cstring a, cstring b, Allocator *allocator);
void string_replace(char *str, char find, char replace);

struct StringBuffer {

  // Includes a null terminator
  void init(Allocator *allocator, size_t size);
  void shutdown();

  void append(cstring string);
  void append(const StringBuffer &other_buffer);
  void append_m(void *memory, size_t size); // Memory version of append.
  void append_f(cstring format, ...);       // Formatted version of append.

  char *append_use(cstring string);
  char *append_use_f(cstring format, ...);
  char *append_use_substring(
      cstring string, u32 start_index,
      u32 end_index); // Append a substring of the passed string.

  // Null terminates the current string
  void close_current_string();

  // Index interface
  // u32 get_index(cstring text) const;
  // cstring get_text(u32 index) const;

  char *reserve(size_t size);

  char *current() { return data + current_size; }

  void clear();

  u32 buffer_size = 1024;
  u32 current_size = 0;
  Allocator *allocator = nullptr;

private:
  char *data = nullptr;
};
} // namespace Helix
