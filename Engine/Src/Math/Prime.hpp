#pragma once

#include "Core/Defines.hpp"

namespace hlx {
bool HLX_API is_prime(const i32 n);

i32 HLX_API next_prime(i32 n);

u64 HLX_API hlx_hash(const void *key, size_t len, u64 seed); 
} // namespace hlx
