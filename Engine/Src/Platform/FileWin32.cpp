#include "File.hpp"

#if HELIX_PLATFORM_WINDOWS
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include <windows.h>
namespace Helix {

static long file_get_size(FILE *f) {
  long fileSizeSigned;

  fseek(f, 0, SEEK_END);
  fileSizeSigned = ftell(f);
  fseek(f, 0, SEEK_SET);

  return fileSizeSigned;
}

static FileService *s_file_service{nullptr};
FileService *FileService::instance() { return s_file_service; }

void FileService::init(void *_config) {
  if (s_file_service) {
    HELIX_SERVICE_RECREATE_MSG(FileService);
    return;
  }

  s_file_service = this;
  HELIX_SERVICE_INIT_MSG(FileService);
}

void FileService::shutdown() {
  s_file_service = nullptr;
  HELIX_SERVICE_SHUTDOWN_MSG(FileService);
}

void FileService::current_directory(Directory *directory) {
  DWORD written_chars = GetCurrentDirectoryA(k_max_path, directory->path);
  directory->path[written_chars] = 0;
}

void FileService::change_directory(cstring path) {
  if (!SetCurrentDirectoryA(path)) {
    HERROR("Cannot change current directory to {}", path);
  }
}

bool FileService::file_exists(cstring path) {
  WIN32_FILE_ATTRIBUTE_DATA unused;
  return GetFileAttributesExA(path, GetFileExInfoStandard, &unused);
}

void FileService::delete_file(cstring path) {
  int result = remove(path);
  if (result)
    HERROR("Failed to delete file: {}", path);
}
FileReadResult FileService::read_file_binary(cstring filename,
                                             Allocator *allocator) {
  FileReadResult result{nullptr, 0};

  FILE *file = fopen(filename, "rb");

  if (file) {

    size_t filesize = file_get_size(file);

    result.data = (char *)halloca(filesize, allocator);
    fread(result.data, filesize, 1, file);

    result.size = filesize;

    fclose(file);
  } else {
    HERROR("Unable to read file: {}", filename);
  }

  return result;
}

FileReadResult FileService::read_file_text(cstring filename,
                                           Allocator *allocator) {
  FileReadResult result{nullptr, 0};

  FILE *file = fopen(filename, "r");

  if (file) {

    size_t filesize = file_get_size(file);

    result.data = (char *)halloca(filesize, allocator);
    fread(result.data, filesize, 1, file);

    result.size = filesize;

    fclose(file);
  } else {
    HERROR("Unable to read file: {}", filename);
  }

  return result;
}

void FileService::write_file_binary(cstring filename, void *memory,
                                    size_t size) {
  FILE *file = fopen(filename, "wb");
  if (!file) {
    HERROR("Failed to write to file: {}", filename);
    return;
  }
  fwrite(memory, size, 1, file);
  fclose(file);
}

void FileService::expand_enviroment_variable(cstring variable, char *dst_string,
                                             u32 size) {
  ExpandEnvironmentStringsA(variable, dst_string, size);
}
} // namespace Helix
#endif
