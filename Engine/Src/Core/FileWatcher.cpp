#include "FileWatcher.hpp"

#include "Core/Log.hpp"
#include "Platform/File.hpp"
// Vendor
#include <Vendor/FileWatch.hpp>

namespace hlx {
std::wstring to_wstring(const char *str) {
  int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
  std::wstring result(size - 1, 0);
  MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], size);
  return result;
}

std::string wstring_to_utf8(const std::wstring &wstr) {
  if (wstr.empty())
    return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0,
                                 nullptr, nullptr);
  std::string result(size - 1, 0); // exclude null terminator
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr,
                      nullptr);
  return result;
}

static FileWatcherService *s_file_watch_service{nullptr};
static std::unique_ptr<filewatch::FileWatch<std::wstring>> watch;

FileWatcherService *FileWatcherService ::instance() {
  return s_file_watch_service;
}

void FileWatcherService ::init(void *config) {
  if (s_file_watch_service) {
    HELIX_SERVICE_RECREATE_MSG(FileWatcherService);
    return;
  }

  s_file_watch_service = this;
  HELIX_SERVICE_INIT_MSG(FileWatcherService);
  std::wstring shader_path = to_wstring(ASSETS_PATH "/Shaders/");
  watch = std::make_unique<filewatch::FileWatch<std::wstring>>(
      shader_path,
      [](const std::wstring &path, const filewatch::Event change_type) {
        switch (change_type) {
        case filewatch::Event::modified:

          HTRACE("File {} was modified", wstring_to_utf8(path).c_str());
          break;
        default:
          break;
        };
      });
}

void FileWatcherService::shutdown() {
  watch_dir = nullptr;
  watch.reset();
  HELIX_SERVICE_SHUTDOWN_MSG(FileWatcherService);
}

bool FileWatcherService::set_watch_dir(cstring dir) {
  if (FileService::directory_exists(dir)) {
    watch_dir = dir;
    return true;
  }
  return false;
}
} // namespace hlx
