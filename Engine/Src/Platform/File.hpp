#pragma once

#include "Core/Defines.hpp"
#include "Core/Service.hpp"
namespace Helix {
struct Allocator;

static const u32 k_max_path = 512;

struct Directory {
  char path[k_max_path];
}; // struct Directory

struct FileReadResult {
  char *data;
  size_t size;
};

struct FileService : public Service {

  virtual void init(void *config = nullptr) override;
  virtual void shutdown() override;
  HELIX_DECLARE_SERVICE(FileService);

  void current_directory(Directory *directory);
  void change_directory(cstring path);

  void open_file();
  void close_file();
  void delete_file(cstring path);
  FileReadResult read_file_binary(cstring filename, Allocator *allocator);
  FileReadResult read_file_text(cstring filename, Allocator *allocator);
};
} // namespace Helix
