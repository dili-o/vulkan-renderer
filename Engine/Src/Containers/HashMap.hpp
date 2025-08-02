#pragma once

#include "Core/Defines.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Math/Prime.hpp"
// Vendor
#include <Vendor/rapidhash/rapidhash.h>

// https://github.com/jamesroutley/write-a-hash-table
namespace Helix {

// TODO: Is this even needed, why not just use a bool in the Item struct to mark
// if it is deleted or even a bitfield
enum class EntryState : u8 {
  EMPTY = 0,
  OCCUPIED,
  DELETED,
};

// TODO: Make this templated
template <typename K, typename V> struct Item {
  K key;
  V value;
  EntryState state{EntryState::EMPTY};
};

// static Item s_deleted_item = {nullptr, nullptr};
static const u64 s_prime_1 = 151;
static const u64 s_prime_2 = 163;
static const u64 s_initial_base_capacity = 50;

using HashFunction = u64 (*)(const void *, size_t, u64);

template <typename K, typename V> struct HashMap {

  // TODO: Use Allocator and maybe a custom hashing function
  void init(u64 base_capacity_, Allocator *allocator_,
            HashFunction hash_function_ = nullptr) {
    if (items) {
      HERROR("HashTable already initialized!");
      return;
    }

    hash_function = hash_function_ ? hash_function_ : rapidhash_withSeed;

    allocator = allocator_;
    size = 0;
    base_capacity = (base_capacity_ < 37) ? 37 : base_capacity_;
    capacity = next_prime(static_cast<i32>(base_capacity));
    items = static_cast<Item<K, V> *>(
        halloca(sizeof(Item<K, V>) * capacity, allocator));
    memset(items, 0, sizeof(Item<K, V>) * capacity);
  }

  void shutdown() {
    allocator->deallocate(items);

    allocator = nullptr;
    items = nullptr;
    size = 0;
    capacity = 0;
  }

  // TODO: Make templated
  u64 hash(const K &key, const u64 data_length, const u64 seed) {
    return hash_function(&key, sizeof(K), seed);
  }

  // TODO: Make templated
  u64 get_hash_index(const K &key, const u64 num_buckets, const u64 attempt) {
    const u64 hash_a = hash(key, s_prime_1, num_buckets);
    const u64 hash_b = hash(key, s_prime_2, num_buckets);
    return (hash_a + (attempt * (hash_b + 1))) % num_buckets;
  }

  // TODO: Make templated
  void insert(const K key, const V value) {
    const i32 load = size * 100 / capacity;
    if (load > 70)
      resize_up();

    u64 i = 0;
    u64 index = get_hash_index(key, capacity, i);

    const Item<K, V> *current_item = &items[index];
    Item<K, V> new_item = {key, value, EntryState::OCCUPIED};
    while (current_item->state != EntryState::EMPTY) {
      if (current_item->state == EntryState::OCCUPIED &&
          current_item->key == key) {
        items[index] = new_item;
        return;
      }
      ++i;
      index = get_hash_index(new_item.key, capacity, i);
      current_item = &items[index];
    }

    items[index] = new_item;
    ++size;

    if (i > 0) {
      HWARN("HashTable: {} collisions", i);
    }
  }

  const V *search(const K &key) {
    u64 i = 0;
    u64 index = get_hash_index(key, capacity, i);

    const Item<K, V> *item = &items[index];
    while (item->state != EntryState::EMPTY) {
      if (item->state == EntryState::OCCUPIED && item->key == key) {
        return &item->value;
      }
      ++i;
      index = get_hash_index(key, capacity, i);
      item = &items[index];
    }

    return nullptr;
  }

  void remove_item(const K &key) {
    const i32 load = size * 100 / capacity;
    if (load < 10)
      resize_down();
    u64 i = 0;
    u64 index = get_hash_index(key, capacity, i);
    const Item<K, V> *item = &items[index];
    while (item->state != EntryState::EMPTY) {
      // Item already
      if (item->state == EntryState::DELETED)
        return;

      if (item->state == EntryState::OCCUPIED) {
        items[index].state = EntryState::DELETED;
        break;
      }
      i++;
      index = get_hash_index(key, size, i);
      item = &items[index];
    }
    --size;
  }

private:
  // TODO: Make templated
  void resize(const u64 new_base_capacity) {
    if (new_base_capacity < s_initial_base_capacity)
      return;

    u64 new_capacity = next_prime(static_cast<i32>(base_capacity));
    Item<K, V> *old_items = items;
    items = static_cast<Item<K, V> *>(
        halloca(sizeof(Item<K, V>) * new_capacity, allocator));
    memset(items, 0, sizeof(Item<K, V>) * new_capacity);

    memcpy(items, old_items, capacity * sizeof(Item<K, V>));

    allocator->deallocate(old_items);
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
  u64 size{0};
  u64 capacity{0};
  u64 base_capacity{s_initial_base_capacity};
  Item<K, V> *items{nullptr};
  HashFunction hash_function{nullptr};
  Allocator *allocator{nullptr};
};

} // namespace Helix
