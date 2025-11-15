#include "File.hpp"
#include <cstring>

#ifdef HELIX_PLATFORM_WINDOWS
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include <shlwapi.h>
#include <shobjidl.h>
#pragma comment(lib, "Shlwapi.lib")
#include <windows.h>
namespace hlx {

static long file_get_size(FILE *f) {
  long fileSizeSigned;

  fseek(f, 0, SEEK_END);
  fileSizeSigned = ftell(f);
  fseek(f, 0, SEEK_SET);

  return fileSizeSigned;
}

static bool is_initialized{false};


void FileSys::init() {
  if (is_initialized) {
    HELIX_SERVICE_RECREATE_MSG(FileSys);
    return;
  }

  HELIX_SERVICE_INIT_MSG(FileSys);
  is_initialized = true;
}

void FileSys::shutdown() {
  HELIX_SERVICE_SHUTDOWN_MSG(FileSys);
  is_initialized = false;
}

void FileSys::current_directory(Directory *directory) {
  DWORD written_chars = GetCurrentDirectoryA(k_max_path, directory->path);
  directory->path[written_chars] = 0;
}

void FileSys::change_directory(cstring path) {
  if (!SetCurrentDirectoryA(path)) {
    HERROR("Cannot change current directory to {}", path);
  }
}

bool FileSys::directory_exists(cstring path) {
  DWORD attributes = GetFileAttributesA(path);

  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  return (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

cstring FileSys::get_file_extension(cstring file_path) {
  // TODO: Maybe check if the '.' comes after the '/' or '\'
  cstring last_fullstop = strrchr(file_path, '.');
  if (last_fullstop) {
    return last_fullstop + 1;
  }
  HERROR("File path does not contain an extension");
  return nullptr;
}

cstring FileSys::get_file_from_path(cstring path) {
  cstring last_forward_separator = strrchr(path, '/');
  cstring last_backward_separator = strrchr(path, '\\');

  if (last_backward_separator != nullptr && last_forward_separator != nullptr) {
    HWARN("Path contains both forward and backward slashes");
    return (last_backward_separator > last_forward_separator)
               ? last_backward_separator + 1
               : last_forward_separator + 1;
  } else if (last_forward_separator != nullptr) {
    return last_forward_separator + 1;
  } else if (last_backward_separator != nullptr) {
    return last_backward_separator + 1;
  }

  return path;
}

bool FileSys::file_exists(cstring path) {
  WIN32_FILE_ATTRIBUTE_DATA unused;
  return GetFileAttributesExA(path, GetFileExInfoStandard, &unused);
}

void FileSys::delete_file(cstring path) {
  int result = remove(path);
  if (result)
    HERROR("Failed to delete file: {}", path);
}

bool FileSys::open_file_dialog(char **file_name, char **path,
                                   Allocator *allocator) {
  HRESULT hr =
      CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (SUCCEEDED(hr)) {
    IFileOpenDialog *pFileOpen;

    // Create the FileOpenDialog object.
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                          IID_IFileOpenDialog,
                          reinterpret_cast<void **>(&pFileOpen));
    if (SUCCEEDED(hr)) {
      // Show the Open dialog box.
      hr = pFileOpen->Show(NULL);

      // Get the file name from the dialog box.
      if (SUCCEEDED(hr)) {
        IShellItem *pItem;
        hr = pFileOpen->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
          PWSTR pszFilePath;
          hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

          // Display the file name to the user.
          if (SUCCEEDED(hr)) {
            PWSTR pszDirPath = _wcsdup(pszFilePath);
            PathRemoveFileSpecW(pszDirPath);

            PWSTR pszFileName = PathFindFileNameW(pszFilePath);

            char dir_buffer[512] = {0};
            char file_buffer[512] = {0};
            WideCharToMultiByte(CP_UTF8, 0, pszDirPath, -1, dir_buffer, 512,
                                NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, pszFileName, -1, file_buffer, 512,
                                NULL, NULL);

            *file_name = (char *)halloca(strlen(file_buffer) + 1, allocator);
            strcpy(*file_name, file_buffer);

            size_t dir_len = strlen(dir_buffer);
            *path = (char *)halloca(dir_len + 2, allocator);
            strcpy(*path, dir_buffer);
            (*path)[dir_len] = '\\';
            (*path)[dir_len + 1] = '\0';

            CoTaskMemFree(pszFilePath);
            free(pszDirPath);
          }
          pItem->Release();
        }
      }
      pFileOpen->Release();
      CoUninitialize();
    }
  } else {
    HERROR("Failed to open file");
    return false;
  }

  return true;
}

bool FileSys::open_file_binary(cstring filename,
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

bool FileSys::open_read_file_binary(cstring filename,
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

void FileSys::close_file(FileReadResult *read_result) {
  if (read_result->internal_handle) {
    fclose((FILE *)read_result->internal_handle);
    read_result->internal_handle = nullptr;
  }
}

bool FileSys::read_file_binary(cstring filename,
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

FileReadResult FileSys::read_file_text(cstring filename,
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

void FileSys::write_file_binary(cstring filename, void *memory,
                                    size_t size) {
  FILE *file = fopen(filename, "wb");
  if (!file) {
    HERROR("Failed to write to file: {}", filename);
    return;
  }
  fwrite(memory, size, 1, file);
  fclose(file);
}

void FileSys::expand_enviroment_variable(cstring variable, char *dst_string,
                                             u32 size) {
  ExpandEnvironmentStringsA(variable, dst_string, size);
}

} // namespace hlx
#endif
