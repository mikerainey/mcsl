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
  
template <class Item, std::size_t capacity,
          std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
class cache_aligned_fixed_capacity_array {
private:
  
  static constexpr
  int item_szb = sizeof(Item);
  
  using aligned_item_type = typename std::aligned_storage<item_szb, cache_align_szb>::type;
  
  aligned_item_type items[capacity];
  
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
  
} // end namespace

/*---------------------------------------------------------------------*/
/* Cache-aligned malloc */

template <std::size_t cache_align_szb=MCSL_CACHE_LINE_SZB>
void* alloc(std::size_t sizeb) {
  return std::aligned_alloc(cache_align_szb, sizeb);
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
    free(ptr);
  }
};

#undef MCSL_CACHE_LINE_SZB

#endif /*! _MCSL_ALIGNED_H_ */
