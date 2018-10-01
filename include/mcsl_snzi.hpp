#include <new>

#include "mcsl_aligned.hpp"
#include "mcsl_tagged.hpp"
#include "mcsl_atomic.hpp"
#include "mcsl_perworker.hpp"

#ifndef _MCSL_SNZI_H_
#define _MCSL_SNZI_H_

namespace mcsl {
  
/*---------------------------------------------------------------------*/
/* Scalable Non-Zero Indicator methods */

static constexpr
int snzi_one_half = -1;

static constexpr
int snzi_root_node_tag = 1;

template <typename Snzi_Node>
void snzi_increment(Snzi_Node& node) {
  auto& X = node.get_counter();
  auto parent = node.get_parent();
  bool succ = false;
  int undo_arr = 0;
  while (! succ) {
    auto x = X.load();
    if (x.c >= 1) {
      auto orig = x;
      auto next = x;
      next.c++;
      next.v++;
      succ = atomic::compare_exchange(X, orig, next);
    }
    if (x.c == 0) {
      auto orig = x;
      auto next = x;
      next.c = snzi_one_half;
      next.v++;
      if (atomic::compare_exchange(X, orig, next)) {
        succ = true;
        x.c = snzi_one_half;
        x.v++;
      }
    }
    if (x.c == snzi_one_half) {
      if (! Snzi_Node::is_root_node(parent)) {
        parent->increment();
      }
      auto orig = x;
      auto next = x;
      next.c = 1;
      if (! atomic::compare_exchange(X, orig, next)) {
        undo_arr++;
      }
    }
  }
  if (Snzi_Node::is_root_node(parent)) {
    return;
  }
  while (undo_arr > 0) {
    parent->decrement();
    undo_arr--;
  }
}

template <typename Snzi_Node>
bool snzi_decrement(Snzi_Node& node) {
  auto& X = node.get_counter();
  auto parent = node.get_parent();
  while (true) {
    auto x = X.load();
    assert(x.c >= 1);
    auto orig = x;
    auto next = x;
    next.c--;
    if (atomic::compare_exchange(X, orig, next)) {
      bool s = (x.c == 1);
      if (Snzi_Node::is_root_node(parent)) {
        return s;
      } else if (s) {
        return parent->decrement();
      } else {
        return false;
      }
    }
  }
}

/*---------------------------------------------------------------------*/
/* Scalable Non-Zero Indicator tree node */
  
class snzi_node {
public:

  using counter_type = struct {
    int c; // counter value
    int v; // version number
  };

private:
  
  cache_aligned_item<std::atomic<counter_type>> counter;

  cache_aligned_item<snzi_node*> parent;

  template <class Item>
  static
  snzi_node* create_root_node(Item x) {
    return (snzi_node*)tagged::tag_with(x, snzi_root_node_tag);
  }
  
public:

  snzi_node(snzi_node* _parent = nullptr) {
    {
      auto p = (_parent == nullptr) ? create_root_node(_parent) : _parent;
      parent.get() = p;
    }
    {
      counter_type c = {.c = 0, .v = 0};
      counter.get().store(c);
    }
  }

  std::atomic<counter_type>& get_counter() {
    return counter.get();
  }

  snzi_node* get_parent() {
    return parent.get();
  }

  void increment() {
    snzi_increment(*this);
  }

  bool decrement() {
    return snzi_decrement(*this);
  }

  bool is_nonzero() {
    return get_counter().load().c > 0;
  }

  static
  bool is_root_node(const snzi_node* n) {
    return tagged::tag_of(n) == snzi_root_node_tag;
  }
  
};

/*---------------------------------------------------------------------*/
/* Scalable Non-Zero Indicator tree container */

template <std::size_t height=perworker::default_max_nb_workers_lg>
class snzi_fixed_capacity_tree {
public:

  using node_type = snzi_node;
  
private:

  static constexpr
  int nb_leaves = 1 << height;
  
  static constexpr
  int heap_size = 2 * nb_leaves;

  cache_aligned_fixed_capacity_array<node_type, heap_size> heap;

  cache_aligned_item<node_type> root;

  void initialize_heap() {
    new (&root.get()) node_type;
    // cells at indices 0 and 1 are not used
    for (std::size_t i = 2; i < 4; i++) {
      new (&heap[i]) node_type(&(root.get()));
    }
    for (std::size_t i = 4; i < heap_size; i++) {
      new (&heap[i]) node_type(&heap[i / 2]);
    }
  }

  void destroy_heap() {
    for (std::size_t i = 2; i < heap_size; i++) {
      (&heap[i])->~node_type();
    }
  }
  
  inline
  std::size_t leaf_position_of(std::size_t i) {
    auto k = nb_leaves + (i & (nb_leaves - 1));
    assert(k >= 2 && k < heap_size);
    return k;
  }

  inline
  node_type& at(std::size_t i) {
    assert(i < nb_leaves);
    return heap[leaf_position_of(i)];
  }


public:

  snzi_fixed_capacity_tree() {
    initialize_heap();
  }

  ~snzi_fixed_capacity_tree() {
    destroy_heap();
  }

  inline
  node_type& operator[](std::size_t i) {
    return at(i);
  }

  inline
  node_type& mine() {
    return at(perworker::unique_id::get_my_id());
  }
  
};

} // end namespace

#endif /*! _MCSL_SNZI_H_ */
