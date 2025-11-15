#pragma once

#include "Core/Defines.hpp"
namespace hlx {
struct Allocator;
struct HMutex;

static const u32 k_max_path = 512;

struct HLX_API Directory {
  char path[k_max_path];
}; // struct Directory

struct HLX_API FileReadResult {
  char *data{nullptr};
  size_t size{0};
  void *internal_handle{nullptr};
};

struct HLX_API FileSys {
  static void init();
  static void shutdown();

  static void current_directory(Directory *directory);
  static void change_directory(cstring path);
  static bool directory_exists(cstring path);

  static cstring get_file_extension(cstring file_path);

  static cstring get_file_from_path(cstring path);

  static bool file_exists(cstring path);

  static void delete_file(cstring path);

  static bool open_file_dialog(char **file_name, char **path,
                               Allocator *allocator);
  // TODO: Add enum for different open types like read or write
  static bool open_file_binary(cstring filename, FileReadResult *read_result);
  static bool open_file_text(cstring filename, FileReadResult *read_result);

  static bool read_file_binary(cstring filename, FileReadResult *read_result);

  static bool open_read_file_binary(cstring filename,
                                    FileReadResult *read_result,
                                    Allocator *allocator);

  static void close_file(FileReadResult *read_result);

  static FileReadResult read_file_text(cstring filename, Allocator *allocator);

  static void write_file_binary(cstring filename, void *memory, size_t size);

  static void expand_enviroment_variable(cstring variable, char *dst_string,
                                         u32 size);
};
} // namespace hlx
