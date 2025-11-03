#include "Prime.hpp"
// Vendor
#include <Vendor/rapidhash/rapidhash.h>

namespace hlx {
bool is_prime(const i32 n) {
  if (n <= 1)
    return false;
  if (n <= 3)
    return true;
  if (n % 2 == 0 || n % 3 == 0)
    return false;
  for (i32 i = 5; i * i <= n; i += 6)
    if (n % i == 0 || n % (i + 2) == 0)
      return false;
  return true;
}

i32 next_prime(i32 n) {
  while (!is_prime(n))
    ++n;
  return n;
}

u64 hlx_hash(const void *key, size_t len, u64 seed) {
  return rapidhash_withSeed(key, len, seed);
}
} // namespace hlx
