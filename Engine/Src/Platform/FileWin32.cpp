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

bool FileService::open_file_binary(cstring filename,
                                   FileReadResult *read_result) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    HERROR("Failed to open file: {}", filename);
    return false;
  }

  read_result->internal_handle = file;
  read_result->size = file_get_size(file);

  return true;
}

bool FileService::open_read_file_binary(cstring filename,
                                        FileReadResult *read_result,
                                        Allocator *allocator) {
  if (!open_file_binary(filename, read_result)) {
    return false;
  }

  read_result->data = (char *)halloca(read_result->size, allocator);
  fread(read_result->data, read_result->size, 1,
        (FILE *)read_result->internal_handle);

  fclose((FILE *)read_result->internal_handle);

  return true;
}

void FileService::close_file(FileReadResult *read_result) {
  if (read_result->internal_handle) {
    fclose((FILE *)read_result->internal_handle);
    read_result->internal_handle = nullptr;
  }
}

bool FileService::read_file_binary(cstring filename,
                                   FileReadResult *read_result) {
  if (read_result->internal_handle) {
    if (read_result->size > 0) {
      fread(read_result->data, read_result->size, 1,
            (FILE *)read_result->internal_handle);
      fclose((FILE *)read_result->internal_handle);
    } else {
      HWARN("File: {} has a file size of 0 bytes", filename);
    }
  } else {
    HERROR("Unable to read file: {}", filename);
    return false;
  }

  return true;
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
