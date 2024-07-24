#pragma once

#include "Core/Platform.hpp"

namespace Helix {
    struct Service {
        virtual void                        init(void* configuration) { }
        virtual void                        shutdown() { }

#define HELIX_DECLARE_SERVICE(Type)        static Type* instance();
    };
}
