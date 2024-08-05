#pragma once

#include "Core/Log.hpp"

// Disable assertions by commenting out the below line.
#define HASSERTIONS_ENABLED

#ifdef HASSERTIONS_ENABLED
namespace Helix {
    void ReportAssertionFailure(cstring expression, cstring message, cstring file, i32 line);

#define HASSERT(expr)                                                \
    {                                                                \
        if (expr) {                                                  \
        } else {                                                     \
            ReportAssertionFailure(#expr, "", __FILE__, __LINE__); \
            HELIX_DEBUG_BREAK;                                            \
        }                                                            \
    }

#define HASSERT_MSG(expr, message)                                        \
    {                                                                     \
        if (expr) {                                                       \
        } else {                                                          \
            ReportAssertionFailure(#expr, message, __FILE__, __LINE__); \
            HELIX_DEBUG_BREAK;                                                 \
        }                                                                 \
    }

#define HASSERT_MSGS(expr, message, ...)                                        \
    {                                                                     \
        if (expr) {                                                       \
        } else {                                                          \
            HCRITICAL("Assertion Failure: {}, message: " message" , in file: {}, line: {}", #expr, __VA_ARGS__, __FILE__, __LINE__);\
            HELIX_DEBUG_BREAK;                                                 \
        }                                                                 \
    }

#ifdef _DEBUG
#define HASSERT_DEBUG(expr)                                          \
    {                                                                \
        if (expr) {                                                  \
        } else {                                                     \
            ReportAssertionFailure(#expr, "", __FILE__, __LINE__); \
            HELIX_DEBUG_BREAK;                                            \
        }                                                            \
    }
#else
#define VASSERT_DEBUG(expr)  // Does nothing at all
#endif

#else
#define VASSERT(expr)               // Does nothing at all
#define VASSERT_MSG(expr, message)  // Does nothing at all
#define VASSERT_DEBUG(expr)         // Does nothing at all
#endif
}