#pragma once

#include <atomic>

#include "mcsl_aligned.hpp"

#ifndef MCSL_MAX_NB_WORKERS_LG
#define MCSL_MAX_NB_WORKERS_LG 7
#endif

namespace mcsl {
namespace perworker {

/*---------------------------------------------------------------------*/
/* Static threshold to upper bound the number of worker threads */

static constexpr
int default_max_nb_workers_lg = MCSL_MAX_NB_WORKERS_LG;

static constexpr
int default_max_nb_workers = 1 << default_max_nb_workers_lg;

#undef MCSL_MAX_NB_WORKERS_LG

/*---------------------------------------------------------------------*/
/* Per-worker, unique id */

class unique_id {
private:

  static constexpr
  int uninitialized_id = -1;

  static
  std::atomic<int> fresh_id;

  static __thread
  int my_id;

public:

  static
  std::size_t get_my_id() {
    if (my_id == uninitialized_id) {
      my_id = fresh_id++;
    }
    return (std::size_t)my_id;
  }

  static
  std::size_t get_nb_workers() {
    return (std::size_t)fresh_id.load();
  }

};

std::atomic<int> unique_id::fresh_id(0);
  
__thread
int unique_id::my_id = uninitialized_id;

/*---------------------------------------------------------------------*/
/* Per-worker array */

template <typename Item, std::size_t capacity=default_max_nb_workers>
class array {
private:

  cache_aligned_fixed_capacity_array<Item, capacity> items;

public:

  array() {
    for (std::size_t i = 0; i < items.size(); ++i) {
      new (&items[i]) value_type;
    }
  }

  ~array() {
    for (std::size_t i = 0; i < items.size(); ++i) {
      items[i].~value_type();
    }
  }

  using value_type = Item;
  using reference = Item&;
  using iterator = value_type*;    

  iterator begin() {
    return items.begin();
  }

  iterator end() {
    return items.end();
  }

  reference operator[](std::size_t i) {
    return items[i];
  }

  reference mine() {
    return items[unique_id::get_my_id()];
  }

  std::size_t size() const {
    return capacity;
  }
  
};
  
} // end namespace
} // end namespace
