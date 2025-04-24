
#pragma once

#include "Core/Defines.hpp"

namespace Helix {

bool launch_tracy_profiler();

bool process_execute(cstring working_directory, cstring process_fullpath,
                     cstring arguments, cstring search_error_string = "");
cstring process_get_output();

} // namespace Helix
