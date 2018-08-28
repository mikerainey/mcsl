#include <memory>
#include <assert.h>

#include "mcsl_atomic.hpp"
#include "mcsl_aligned.hpp"

#ifndef _MCSL_CHASELEV_H_
#define _MCSL_CHASELEV_H_

namespace mcsl {
  
/*---------------------------------------------------------------------*/
/* Chase-Lev Work-Stealing Deque data structure  */

template <typename T>
class chaselev_deque {
private:
    
  class circular_array {
  private:

    cache_aligned_array<std::atomic<T*>> items;

    std::unique_ptr<circular_array> previous;

  public:

    circular_array(std::size_t size) : items(size) { }

    std::size_t size() const {
      return items.size();
    }

    T* get(std::size_t i) {
      return items[i & (size() - 1)].load(std::memory_order_relaxed);
    }

    void put(std::size_t i, T* x) {
      items[i & (size() - 1)].store(x, std::memory_order_relaxed);
    }

    circular_array* grow(std::size_t top, std::size_t bottom) {
      circular_array* new_array = new circular_array(size() * 2);
      new_array->previous.reset(this);
      for (auto i = top; i != bottom; ++i) {
        new_array->put(i, get(i));
      }
      return new_array;
    }

  };

  std::atomic<circular_array*> array;
  
  std::atomic<std::size_t> top, bottom;

public:

  chaselev_deque()
    : array(new circular_array(32)), top(0), bottom(0) { }

  ~chaselev_deque() {
    auto p = array.load(std::memory_order_relaxed);
    if (p) {
      delete p;
    }
  }

  std::size_t size() {
    return (std::size_t)bottom.load() - top.load();
  }

  bool empty() {
    return size() == 0;
  }

  void push(T* x) {
    auto b = bottom.load(std::memory_order_relaxed);
    auto t = top.load(std::memory_order_acquire);
    auto a = array.load(std::memory_order_relaxed);
    if (b - t > a->size() - 1) {
      a = a->grow(t, b);
      array.store(a, std::memory_order_relaxed);
    }
    a->put(b, x);
    std::atomic_thread_fence(std::memory_order_release);
    bottom.store(b + 1, std::memory_order_relaxed);
  }

  T* pop() {
    auto b = bottom.load(std::memory_order_relaxed) - 1;
    auto a = array.load(std::memory_order_relaxed);
    bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto t = top.load(std::memory_order_relaxed);
    if (t <= b) {
      T* x = a->get(b);
      if (t == b) {
        if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
          x = nullptr;
        }
        bottom.store(b + 1, std::memory_order_relaxed);
      }
      return x;
    } else {
      bottom.store(b + 1, std::memory_order_relaxed);
      return nullptr;
    }
  }

  T* steal() {
    auto t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = bottom.load(std::memory_order_acquire);
    T* x = nullptr;
    if (t < b) {
      auto a = array.load(std::memory_order_relaxed);
      x = a->get(t);
      if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
        return nullptr;
      }
    }
    return x;
  }

};
    
} // end namespace

#endif /*! _MCSL_CHASELEV_H_ */
