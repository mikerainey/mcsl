#include <type_traits>
#include <cstdlib>
#include <assert.h>

#ifndef _MCSL_ALIGNED_H_
#define _MCSL_ALIGNED_H_

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

  // Iterator

  using value_type = Item;
  using iterator = value_type*;    

  iterator begin() {
    return reinterpret_cast<Item*>(items);
  }

  iterator end() {
    return reinterpret_cast<Item*>(items + size());
  }

};

/*---------------------------------------------------------------------*/
/* Cache-aligned memory */

template <class Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
class cache_aligned_item {
private:

  cache_aligned_fixed_capacity_array<Item, 1, cache_align_szb> item;
  
public:
  
  Item& get() {
    return item[0];
  }
      
};  

/*---------------------------------------------------------------------*/
/* Cache-aligned malloc */

template <std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
void* alloc(std::size_t sizeb) {
  // aligned_sizeb needed because the second argument to aligned_alloc
  // is required to be a multiple of the first
  auto aligned_sizeb = sizeb + (sizeb % cache_align_szb);
  return aligned_alloc(cache_align_szb, aligned_sizeb);
}

template <typename Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
Item* alloc_uninitialized_array(std::size_t size) {
  return (Item*)alloc<cache_align_szb>(sizeof(Item) * size);
}

template <typename Item,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
Item* alloc_uninitialized() {
  return alloc_uninitialized_array<Item, cache_align_szb>(1);
}

template <typename Item>
class Malloc_deleter {
public:
  void operator()(Item* ptr) {
    std::free(ptr);
  }
};

} // end namespace

#undef MCSL_CACHE_LINE_SZB

#endif /*! _MCSL_ALIGNED_H_ */
