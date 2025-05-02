#pragma once

#include "Core/Assert.hpp"
#include "Core/Memory.hpp"

namespace Helix {

// Array ///////////////////////////////////////////////////////
template <typename T> struct Array {

  Array();
  ~Array();

  void init(Allocator *allocator, u32 initial_capacity, u32 initial_size = 0);
  void shutdown();

  void push(const T &element);
  T &push_use(); // Grow the size and return T to be filled.

  template <typename U> void push_array(Array<U> &array);

  void pop();
  void pop_at(u32 index);
  void delete_swap(u32 index);

  T &operator[](u32 index);
  const T &operator[](u32 index) const;

  void clear();
  void set_size(u32 new_size);
  void set_capacity(u32 new_capacity);
  void grow(u32 new_capacity);

  T &back();
  const T &back() const;

  T &front();
  const T &front() const;

  u32 size_in_bytes() const;
  u32 capacity_in_bytes() const;

  class Iterator {
  private:
    T *ptr;

  public:
    Iterator(T *p) : ptr(p) {}

    // Iterator operators
    T &operator*() { return *ptr; }
    Iterator &operator++() {
      ++ptr;
      return *this;
    }
    Iterator operator++(int) {
      Iterator tmp = *this;
      ++ptr;
      return tmp;
    }
    bool operator!=(const Iterator &other) const { return ptr != other.ptr; }
    bool operator==(const Iterator &other) const { return ptr == other.ptr; }
  };

  Iterator begin() { return Iterator(data); }
  Iterator end() { return Iterator(data + size); }

  const Iterator begin() const { return Iterator(data); }
  const Iterator end() const { return Iterator(data + size); }

  T *data{nullptr};
  u32 size{0};     // Occupied size
  u32 capacity{0}; // Allocated capacity
  Allocator *allocator{nullptr};

}; // struct Array

// Implementation /////////////////////////////////////////////////////

// Array ///////////////////////////////////////////////////////
template <typename T> inline Array<T>::Array() {
  // HASSERT( true );
}

template <typename T> inline Array<T>::~Array() {
  // HASSERT( data == nullptr );
}

template <typename T>
inline void Array<T>::init(Allocator *allocator_, u32 initial_capacity,
                           u32 initial_size) {
  data = nullptr;
  size = initial_size;
  capacity = 0;
  allocator = allocator_;

  if (initial_capacity > 0) {
    grow(initial_capacity);
  }
}

template <typename T> inline void Array<T>::shutdown() {
  if (capacity > 0) {
    allocator->deallocate(data);
  }
  data = nullptr;
  size = capacity = 0;
}

template <typename T> inline void Array<T>::push(const T &element) {
  if (size >= capacity) {
    grow(capacity + 1);
  }

  data[size++] = element;
}

template <typename T> inline T &Array<T>::push_use() {
  if (size >= capacity) {
    grow(capacity + 1);
  }
  ++size;

  return back();
}

template <typename T>
template <typename U>
inline void Array<T>::push_array(Array<U> &array) {
  bool type_match = std::is_same<T, U>::value;
  HASSERT_MSG(type_match, "Attempting to push an array with a different type");

  if ((size + array.size) >= capacity) {
    grow(size + array.size);
  }

  memcpy(&data[size], array.data, array.size * sizeof(T));

  size = size + array.size;
}

template <typename T> inline void Array<T>::pop() {
  HASSERT(size > 0);
  --size;
}

template <typename T> inline void Array<T>::pop_at(u32 index) {
  HASSERT_MSG(index < size || index >= size,
              "Attempting to pop outside of index");

  u32 new_size = size - 1;
  T *new_data = (T *)hallocaa(new_size * sizeof(T), allocator, alignof(T));

  memcpy(new_data, data, index * sizeof(T));

  if (index != size - 1) {
    memcpy(new_data + index, data + (index + 1),
           sizeof(T) * (size - index - 1));
  }
  allocator->deallocate(data);

  data = new_data;
  --size;
}

template <typename T> inline void Array<T>::delete_swap(u32 index) {
  HASSERT(size > 0 && index < size);
  data[index] = data[--size];
}

template <typename T> inline T &Array<T>::operator[](u32 index) {
  HASSERT(index < size);
  return data[index];
}

template <typename T> inline const T &Array<T>::operator[](u32 index) const {
  HASSERT(index < size);
  return data[index];
}

template <typename T> inline void Array<T>::clear() { size = 0; }

template <typename T> inline void Array<T>::set_size(u32 new_size) {
  if (new_size > capacity) {
    grow(new_size);
  }
  size = new_size;
}

template <typename T> inline void Array<T>::set_capacity(u32 new_capacity) {
  if (new_capacity > capacity) {
    grow(new_capacity);
  }
}

template <typename T> inline void Array<T>::grow(u32 new_capacity) {
  if (new_capacity < capacity * 2) {
    new_capacity = capacity * 2;
  } else if (new_capacity < 4) {
    new_capacity = 4;
  }

  // T* new_data = (T*)allocator->allocate(new_capacity * sizeof(T),
  // alignof(T));
  T *new_data = (T *)hallocaa(new_capacity * sizeof(T), allocator, alignof(T));
  if (capacity) {
    memcpy(new_data, data, capacity * sizeof(T));
    allocator->deallocate(data);
  }

  data = new_data;
  capacity = new_capacity;
}

template <typename T> inline T &Array<T>::back() {
  HASSERT(size);
  return data[size - 1];
}

template <typename T> inline const T &Array<T>::back() const {
  HASSERT(size);
  return data[size - 1];
}

template <typename T> inline T &Array<T>::front() {
  HASSERT(size);
  return data[0];
}

template <typename T> inline const T &Array<T>::front() const {
  HASSERT(size);
  return data[0];
}

template <typename T> inline u32 Array<T>::size_in_bytes() const {
  return size * sizeof(T);
}

template <typename T> inline u32 Array<T>::capacity_in_bytes() const {
  return capacity * sizeof(T);
}
} // namespace Helix
