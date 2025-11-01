#include "Process.hpp"

#ifdef HELIX_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "Core/Log.hpp"
#include <windows.h>

namespace hlx {
// Static buffer to log the error coming from windows.
static const u32 k_process_log_buffer = 256;
char s_process_log_buffer[k_process_log_buffer];
static char k_process_output_buffer[1025];

void win32_get_error(char *buffer, u32 size) {
  DWORD errorCode = GetLastError();

  char *error_string;
  if (!FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL,
          errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPSTR)&error_string, 0, NULL))
    return;

  sprintf_s(buffer, size, "%s", error_string);

  LocalFree(error_string);
}

bool launch_tracy_profiler() {
  // Construct command line
  char command[512];
  snprintf(command, sizeof(command), "%s -a %s", TRACY_PROFILER_DIR,
           TRACY_CLIENT_ADDRESS);

  // Create process info and startup info
  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = SW_SHOW;

  PROCESS_INFORMATION process_info = {};

  // Launch Tracy as detached process without redirecting I/O
  if (!CreateProcessA(NULL,               // No module name (use command line)
                      command,            // Command line
                      NULL,               // Process security attributes
                      NULL,               // Thread security attributes
                      FALSE,              // Don't inherit handles
                      CREATE_NEW_CONSOLE, // Creation flags
                      NULL,               // Use parent's environment
                      ".",                // Use current directory
                      &startup_info,      // Startup info
                      &process_info       // Process info
                      )) {
    HERROR("Failed to launch Tracy Profiler: {}", GetLastError());
    return false;
  }

  // Close handles but keep the process running
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  // Give Tracy a moment to initialize
  Sleep(500);

  return true;
}

bool process_execute(cstring working_directory, cstring process_fullpath,
                     cstring arguments, cstring search_error_string) {
  // From the post in
  // https://stackoverflow.com/questions/35969730/how-to-read-output-from-cmd-exe-using-createprocess-and-createpipe/55718264#55718264
  // Create pipes for redirecting output
  HANDLE handle_stdin_pipe_read = NULL;
  HANDLE handle_stdin_pipe_write = NULL;
  HANDLE handle_stdout_pipe_read = NULL;
  HANDLE handle_std_pipe_write = NULL;

  SECURITY_ATTRIBUTES security_attributes = {sizeof(SECURITY_ATTRIBUTES), NULL,
                                             TRUE};

  BOOL ok = CreatePipe(&handle_stdin_pipe_read, &handle_stdin_pipe_write,
                       &security_attributes, 0);
  if (ok == FALSE) {
    HERROR("Failed to create pipe!");
    return false;
  }
  ok = CreatePipe(&handle_stdout_pipe_read, &handle_std_pipe_write,
                  &security_attributes, 0);
  if (ok == FALSE) {
    HERROR("Failed to create pipe!");
    return false;
  }

  // Create startup informations with std redirection
  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  startup_info.hStdInput = handle_stdin_pipe_read;
  startup_info.hStdError = handle_std_pipe_write;
  startup_info.hStdOutput = handle_std_pipe_write;
  startup_info.wShowWindow = SW_SHOW;

  bool execution_success = false;
  // Execute the process
  PROCESS_INFORMATION process_info = {};
  BOOL inherit_handles = TRUE;

  if (CreateProcessA(process_fullpath, (char *)arguments, 0, 0, inherit_handles,
                     0, 0, working_directory, &startup_info, &process_info)) {

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    execution_success = true;
  } else {
    win32_get_error(&s_process_log_buffer[0], k_process_log_buffer);

    HERROR("Execute process error.\n Exe: \"{}\" - Args: \"{}\" - Work_dir: "
           "\"{}\"",
           process_fullpath, arguments, working_directory);
    HERROR("Message: {}", s_process_log_buffer);
  }
  CloseHandle(handle_stdin_pipe_read);
  CloseHandle(handle_std_pipe_write);

  // Output
  DWORD bytes_read;
  ok = ReadFile(handle_stdout_pipe_read, k_process_output_buffer, 1024,
                &bytes_read, nullptr);

  // Consume all outputs.
  // Terminate current read and initialize the next.
  if (ok == TRUE) {
    k_process_output_buffer[bytes_read] = 0;
    // HINFO("{}", k_process_output_buffer);

    // ok = ReadFile(handle_stdout_pipe_read, k_process_output_buffer, 1024,
    //               &bytes_read, nullptr);
  }

  if (strlen(search_error_string) > 0 &&
      strstr(k_process_output_buffer, search_error_string)) {
    execution_success = false;
  }

  // Close handles.
  CloseHandle(handle_stdout_pipe_read);
  CloseHandle(handle_stdin_pipe_write);

  DWORD process_exit_code = 0;
  GetExitCodeProcess(process_info.hProcess, &process_exit_code);

  return execution_success;
}

cstring process_get_output() { return k_process_output_buffer; }
} // namespace hlx
#endif
