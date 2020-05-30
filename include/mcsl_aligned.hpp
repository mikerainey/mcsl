#pragma once

#include <type_traits>
#include <cstdlib>
#include <assert.h>
#include <memory>

#ifndef MCSL_CACHE_LINE_SZB
#define MCSL_CACHE_LINE_SZB 128
#endif

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Cache-aligned, fixed-capacity array */

/* This class provides storage for a given number, capacity, of items
 * of given type, Item, also ensuring that the starting of address of
 * each item is aligned by a multiple of a given number of bytes,
 * cache_align_szb (defaultly, MCSL_CACHE_LINE_SZB).
 *
 * The class *does not* itself initialize (or deinitialize) the
 * storage cells.
 */
template <typename Item, std::size_t capacity,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
class cache_aligned_fixed_capacity_array {
private:
  
  static constexpr
  int item_szb = sizeof(Item);
  
  using aligned_item_type =
    typename std::aligned_storage<item_szb, cache_align_szb>::type;
  
  aligned_item_type items[capacity] __attribute__ ((aligned (cache_align_szb)));

  inline
  Item& at(std::size_t i) {
    assert(i < capacity);
    return *reinterpret_cast<Item*>(items + i);
  }
  
public:
  
  Item& operator[](std::size_t i) {
    return at(i);
  }
  
  std::size_t size() const {
    return capacity;
  }

};

/*---------------------------------------------------------------------*/
/* Cache-aligned malloc */

template <std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
void* aligned_alloc(std::size_t sizeb) {
  return std::aligned_alloc(cache_align_szb, cache_align_szb * sizeb);
}

template <typename Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
Item* aligned_alloc_uninitialized_array(std::size_t size) {
  return (Item*)aligned_alloc<cache_align_szb>(sizeof(Item) * size);
}

template <typename Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
Item* aligned_alloc_uninitialized() {
  return aligned_alloc_uninitialized_array<Item, cache_align_szb>(1);
}

template <typename Item>
class Malloc_deleter {
public:
  void operator()(Item* ptr) {
    std::free(ptr);
  }
};

/*---------------------------------------------------------------------*/
/* Cache-aligned (heap-allocated) array */

/* This class provides storage for a given number, size, of items
 * of given type, Item, also ensuring that the starting of address of
 * each item is aligned by a multiple of a given number of bytes,
 * cache_align_szb (defaultly, MCSL_CACHE_LINE_SZB).
 *
 * The class *does not* itself initialize (or deinitialize) the
 * storage cells.
 */
template <typename Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
class cache_aligned_array {
private:
  
  static constexpr
  int item_szb = sizeof(Item);
  
  using aligned_item_type =
    typename std::aligned_storage<item_szb, cache_align_szb>::type;

  std::unique_ptr<aligned_item_type, Malloc_deleter<aligned_item_type>> items;

  std::size_t _size;

  inline
  Item& at(std::size_t i) {
    assert(i < size());
    return *reinterpret_cast<Item*>(items.get() + i);
  }
  
public:

  cache_aligned_array(std::size_t __size)
    : _size(__size) {
    items.reset(aligned_alloc_uninitialized_array<aligned_item_type>(__size));
  }
  
  Item& operator[](std::size_t i) {
    return at(i);
  }
  
  std::size_t size() const {
    return _size;
  }

  // Iterator

  using value_type = Item;
  using iterator = value_type*;    

  iterator begin() {
    return reinterpret_cast<Item*>(items.get());
  }

  iterator end() {
    return reinterpret_cast<Item*>(items.get() + size());
  }

};
  
} // end namespace


