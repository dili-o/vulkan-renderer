#pragma once

#include "Core/Defines.hpp"
#include "Core/Log.hpp"
#include "Math/Prime.hpp"
#include <cstdlib>
#include <string.h>

// https://github.com/jamesroutley/write-a-hash-table
namespace Helix {

// TODO: Make this templated
struct Item {
  char *key;
  char *value;
};

static Item s_deleted_item = {nullptr, nullptr};
static const u64 s_prime_1 = 151;
static const u64 s_prime_2 = 163;
static const u64 s_initial_base_capacity = 50;

struct HashMap {

  Item *new_item(cstring key, cstring value) {
    // TODO: Use the allocator
    Item *i = static_cast<Item *>(malloc(sizeof(Item)));
    i->key = _strdup(key);
    i->value = _strdup(value);
    return i;
  }

  void delete_item(Item *item) {
    free(item->value);
    free(item->key);
    free(item);
  }

  void init(u64 base_capacity_) {
    if (items) {
      HERROR("HashTable already initialized!");
      return;
    }

    base_capacity = base_capacity_;

    capacity = next_prime(static_cast<i32>(base_capacity));
    size = 0;
    items = static_cast<Item **>(
        calloc(static_cast<size_t>(capacity), sizeof(Item *)));
  }

  void shutdown() {
    for (u64 i = 0; i < size; ++i) {
      Item *item = items[i];
      if (item)
        delete_item(item);
      item = nullptr;
    }

    free(items);
    size = 0;
    capacity = 0;
  }

  // TODO: Make templated
  u64 hash(cstring string, const u64 data_length, const u64 seed) {
    u64 hash = 0;
    const u64 len_s = static_cast<u64>(strlen(string));
    for (u64 i = 0; i < len_s; ++i) {
      hash += static_cast<u64>(pow(seed, len_s - (i + 1)) * string[i]);
      hash = hash % data_length;
    }

    return hash;
  }

  // TODO: Make templated
  u64 get_hash_index(cstring s, const u64 num_buckets, const u64 attempt) {
    const u64 hash_a = hash(s, s_prime_1, num_buckets);
    const u64 hash_b = hash(s, s_prime_2, num_buckets);
    return (hash_a + (attempt * (hash_b + 1))) % num_buckets;
  }

  // TODO: Make templated
  void insert(cstring key, cstring value) {
    const i32 load = size * 100 / capacity;
    if (load > 70)
      resize_up();

    u64 i = 0;
    Item *item = new_item(key, value);
    u64 index = get_hash_index(key, capacity, i);

    Item *current_item = items[index];
    while (current_item != nullptr) {
      if (current_item != &s_deleted_item) {
        if (strcmp(current_item->key, key) == 0) {
          delete_item(current_item);
          items[index] = item;
          return;
        }
      }
      ++i;
      index = get_hash_index(item->key, capacity, i);
      current_item = items[index];
    }

    items[index] = item;
    ++size;

    if (i > 0) {
      HWARN("HashTable: {} collisions", i);
    }
  }

  // TODO: Make templated
  char *search(cstring key) {
    u64 i = 0;
    u64 index = get_hash_index(key, capacity, i);

    Item *item = items[index];
    while (item != nullptr) {
      if (item != &s_deleted_item) {
        if (strcmp(item->key, key) == 0) {
          return item->value;
        }
      }
      ++i;
      index = get_hash_index(key, capacity, i);
      item = items[index];
    }

    return nullptr;
  }

  // TODO: Make templated
  void remove_item(cstring key) {
    const i32 load = size * 100 / capacity;
    if (load < 10)
      resize_down();
    u64 i = 0;
    u64 index = get_hash_index(key, capacity, i);
    Item *item = items[index];
    while (item != nullptr) {
      if (item != &s_deleted_item) {
        if (strcmp(item->key, key) == 0) {
          delete_item(item);
          items[index] = &s_deleted_item;
          break;
        }
      }
      i++;
      index = get_hash_index(key, size, i);
      item = items[index];
    }
    --size;
  }

private:
  // TODO: Make templated
  void resize(const u64 new_base_capacity) {
    if (new_base_capacity < s_initial_base_capacity)
      return;

    u64 new_capacity = next_prime(static_cast<i32>(base_capacity));
    Item **old_items = items;
    items = static_cast<Item **>(
        calloc(static_cast<size_t>(new_capacity), sizeof(Item *)));

    for (u64 i = 0; i < capacity; ++i) {
      Item *item = old_items[i];
      if (item != nullptr && item != &s_deleted_item) {
        insert(item->key, item->value);
        delete_item(item);
      }
    }

    free(old_items);
    base_capacity = new_base_capacity;
    capacity = new_capacity;
  }

  void resize_up() {
    const u64 new_capacity = base_capacity * 2;
    resize(new_capacity);
  }

  void resize_down() {
    const u64 new_capacity = base_capacity / 2;
    resize(new_capacity);
  }

public:
  u64 size = 0;
  u64 capacity = 0;
  u64 base_capacity = s_initial_base_capacity;
  Item **items = nullptr;
};

} // namespace Helix
