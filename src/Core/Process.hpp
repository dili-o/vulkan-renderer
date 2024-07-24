#pragma once

#include "Core/Platform.hpp"

namespace Helix {

    bool                            process_execute(cstring working_directory, cstring process_fullpath, cstring arguments, cstring search_error_string = "");
    cstring                         process_get_output();

} // namespace Helix
