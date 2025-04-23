#pragma once

#include "Core/Defines.hpp"
#include "Core/Service.hpp"
namespace Helix {
struct Allocator;
struct HMutex;

static const u32 k_max_path = 512;

struct Directory {
  char path[k_max_path];
}; // struct Directory

struct FileReadResult {
  char *data{nullptr};
  size_t size{0};
  void *internal_handle{nullptr};
};

struct FileService : public Service {

  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;
  HELIX_DECLARE_SERVICE(FileService);

  void current_directory(Directory *directory);
  void change_directory(cstring path);

  bool file_exists(cstring path);

  void delete_file(cstring path);
  // TODO: Add enum for different open types like read or write
  bool open_file_binary(cstring filename, FileReadResult *read_result);
  bool open_file_text(cstring filename, FileReadResult *read_result);

  bool read_file_binary(cstring filename, FileReadResult *read_result);

  bool open_read_file_binary(cstring filename, FileReadResult *read_result,
                             Allocator *allocator);

  void close_file(FileReadResult *read_result);

  FileReadResult read_file_text(cstring filename, Allocator *allocator);

  void write_file_binary(cstring filename, void *memory, size_t size);

  void expand_enviroment_variable(cstring variable, char *dst_string, u32 size);
};
} // namespace Helix
