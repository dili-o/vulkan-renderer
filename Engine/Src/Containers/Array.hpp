#pragma once

#include "Core/Assert.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"

namespace hlx {

// Array ///////////////////////////////////////////////////////
template <typename T> struct Array {

  Array();
  ~Array();

  void init(Allocator *allocator, size_t initial_capacity,
            size_t initial_size = 0);
  void shutdown();

  void push(const T &element);
  T &push_use(); // Grow the size and return T to be filled.

  template <typename U> void push_array(Array<U> &array);

  void pop();
  void pop_at(size_t index);
  void delete_swap(size_t index);

  T &operator[](size_t index);
  const T &operator[](size_t index) const;

  void clear();
  void set_size(size_t new_size);
  void set_capacity(size_t new_capacity);
  void resize(size_t new_capacity);

  T &back();
  const T &back() const;

  T &front();
  const T &front() const;

  size_t size_in_bytes() const;
  size_t capacity_in_bytes() const;

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
  size_t size{0};     // Occupied size
  size_t capacity{0}; // Allocated capacity
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
inline void Array<T>::init(Allocator *allocator_, size_t initial_capacity,
                           size_t initial_size) {
  data = nullptr;
  size = initial_size;
  capacity = 0;
  allocator = allocator_;

  if (initial_capacity > 0) {
    resize(initial_capacity);
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
    resize(capacity * 2);
  }

  data[size++] = element;
}

template <typename T> inline T &Array<T>::push_use() {
  if (size >= capacity) {
    resize(capacity * 2);
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
    resize(size + array.size);
  }

  memcpy(&data[size], array.data, array.size * sizeof(T));

  size = size + array.size;
}

template <typename T> inline void Array<T>::pop() {
  HASSERT(size > 0);
  --size;
}

template <typename T> inline void Array<T>::pop_at(size_t index) {
  HASSERT_MSG(index < size || index >= size,
              "Attempting to pop outside of index");

  size_t new_size = size - 1;
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

template <typename T> inline void Array<T>::delete_swap(size_t index) {
  HASSERT(size > 0 && index < size);
  data[index] = data[--size];
}

template <typename T> inline T &Array<T>::operator[](size_t index) {
  HASSERT(index < size);
  return data[index];
}

template <typename T> inline const T &Array<T>::operator[](size_t index) const {
  HASSERT(index < size);
  return data[index];
}

template <typename T> inline void Array<T>::clear() { size = 0; }

template <typename T> inline void Array<T>::set_size(size_t new_size) {
  if (new_size > capacity) {
    resize(new_size);
  }
  size = new_size;
}

template <typename T> inline void Array<T>::set_capacity(size_t new_capacity) {
  if (new_capacity > capacity) {
    resize(new_capacity);
  }
}

template <typename T> inline void Array<T>::resize(size_t new_capacity) {
  if (new_capacity < capacity) {
    HWARN("Cannot resize array: new_capacity < capacity");
    return;
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

template <typename T> inline size_t Array<T>::size_in_bytes() const {
  return size * sizeof(T);
}

template <typename T> inline size_t Array<T>::capacity_in_bytes() const {
  return capacity * sizeof(T);
}
} // namespace hlx
