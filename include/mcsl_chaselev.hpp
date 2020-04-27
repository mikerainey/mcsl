#pragma once

#include <memory>
#include <assert.h>
#include <deque>
#include <thread>
#include <condition_variable>
#include <iostream>

#include "mcsl_stats.hpp"
#include "mcsl_logging.hpp"
#include "mcsl_elastic.hpp"

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Chase-Lev Work-Stealing Deque data structure
 * 
 * based on the implementation of https://gist.github.com/Amanieu/7347121
 *
 * Dynamic Circular Work-Stealing Deque
 * http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.170.1097&rep=rep1&type=pdf
 *
 * Correct and Efﬁcient Work-Stealing for Weak Memory Models
 * http://www.di.ens.fr/~zappa/readings/ppopp13.pdf
 */
  
template <typename Fiber>
class chase_lev_deque {

  using index_type = long;
  
  class circular_array {
  private:
    
    cache_aligned_array<std::atomic<Fiber*>> items;
    std::unique_ptr<circular_array> previous;

  public:
    
    circular_array(index_type n) : items(n) {}
    
    index_type size() const {
      return items.size();
    }
    
    Fiber* get(index_type index) {
      return items[index % size()].load(std::memory_order_relaxed);
    }
    
    void put(index_type index, Fiber* x) {
      items[index % size()].store(x, std::memory_order_relaxed);
    }
    
    circular_array* grow(index_type top, index_type bottom) {
      circular_array* new_array = new circular_array(size() * 2);
      new_array->previous.reset(this);
      for (index_type i = top; i != bottom; ++i) {
        new_array->put(i, get(i));
      }
      return new_array;
    }

  };

  std::atomic<circular_array*> array;
  std::atomic<index_type> top, bottom;

public:
  
  chase_lev_deque()
    : array(new circular_array(1024)), top(0), bottom(0) {}
  
  ~chase_lev_deque() {
    circular_array* p = array.load(std::memory_order_relaxed);
    if (p) {
      delete p;
    }
  }

  index_type size() {
    auto b = bottom.load(std::memory_order_relaxed);
    auto t = top.load(std::memory_order_relaxed);
    return b - t;
  }

  bool empty() {
    return size() == 0;
  }

  void push(Fiber* x) {
    auto b = bottom.load(std::memory_order_relaxed);
    auto t = top.load(std::memory_order_acquire);
    circular_array* a = array.load(std::memory_order_relaxed);
    if (b - t > a->size() - 1) {
      a = a->grow(t, b);
      array.store(a, std::memory_order_relaxed);
    }
    a->put(b, x);
    std::atomic_thread_fence(std::memory_order_release);
    bottom.store(b + 1, std::memory_order_relaxed);
  }

  Fiber* pop() {
    auto b = bottom.load(std::memory_order_relaxed) - 1;
    circular_array* a = array.load(std::memory_order_relaxed);
    bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto t = top.load(std::memory_order_relaxed);
    if (t <= b) {
      auto x = a->get(b);
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

  Fiber* steal() {
    auto t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = bottom.load(std::memory_order_acquire);
    Fiber* x = nullptr;
    if (t < b) {
      circular_array* a = array.load(std::memory_order_relaxed);
      x = a->get(t);
      if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
        return nullptr;
      }
    }
    return x;
  }
  
};
  
/*---------------------------------------------------------------------*/
/* Work-stealing scheduler  */

using fiber_status_type = enum fiber_status_enum {
  fiber_status_continue,
  fiber_status_pause,
  fiber_status_finish,
  fiber_status_terminate
};
  
template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
class chase_lev_work_stealing_scheduler {
private:

  using fiber_type = Fiber<Scheduler_configuration>;

  using cl_deque_type = chase_lev_deque<fiber_type>;

  using buffer_type = std::deque<fiber_type*>;

  using elastic_type = elastic<Stats, Logging>;

  static
  perworker::array<cl_deque_type> deques;

  static
  perworker::array<buffer_type> buffers;

  static
  perworker::array<hash_value_type> random_number_generators;

  static
  std::size_t random_other_worker(size_t nb_workers, size_t my_id) {
    assert(nb_workers != 1);
    auto& rn = random_number_generators.mine();
    auto id = (std::size_t)(rn % (nb_workers - 1));
    if (id >= my_id) {
      id++;
    }
    rn = hash(rn);
    assert(id != my_id);
    assert(id >= 0 && id < nb_workers);
    return id;
  }

  static
  perworker::array<pthread_t> pthreads;

  static
  fiber_type* flush() {
    auto& my_buffer = buffers.mine();
    auto& my_deque = deques.mine();
    fiber_type* current = nullptr;
    if (my_buffer.empty()) {
      return nullptr;
    }
    current = my_buffer.back();
    my_buffer.pop_back();
    while (! my_buffer.empty()) {
      auto f = my_buffer.front();
      my_buffer.pop_front();
      my_deque.push(f);
    }
    assert(my_buffer.empty());
    return current;
  }

  using termination_detection_barrier_type = typename Scheduler_configuration::termination_detection_barrier_type;
  
public:
  static void launch(std::size_t nb_workers) {
    bool should_terminate = false;
    termination_detection_barrier_type termination_barrier;

    std::size_t nb_workers_exited = 0;
    std::mutex exit_lock;
    std::condition_variable exit_condition_variable;

    using scheduler_status_type = enum scheduler_status_enum {
      scheduler_status_active,
      scheduler_status_finish
    };

    auto acquire = [&] {
      if (nb_workers == 1) {
        termination_barrier.set_active(false);
        return scheduler_status_finish;
      }
      auto my_id = perworker::unique_id::get_my_id();
      Logging::log_event(enter_wait);
      auto sa = Stats::on_enter_acquire();
      termination_barrier.set_active(false);
      elastic_type::accept_lifelines();
      fiber_type *current = nullptr;
      while (current == nullptr) {
        auto k = random_other_worker(nb_workers, my_id);
        termination_barrier.set_active(true);
        if (! deques[k].empty()) {
          current = deques[k].steal();
          if (current == nullptr) {
            termination_barrier.set_active(false);
          } else {
            Stats::increment(Stats::configuration_type::nb_steals);
          }
        }
        if (current == nullptr) {
          elastic_type::try_to_sleep(k);
        } else {
          elastic_type::wake_children();
        }
        if (termination_barrier.is_terminated() || should_terminate) {
          assert(current == nullptr);
          Logging::log_event(worker_exit);
          elastic_type::wake_children();
          Stats::on_exit_acquire(sa);
          Logging::log_event(exit_wait);
          return scheduler_status_finish;
        }
      }
      assert(current != nullptr);
      buffers.mine().push_back(current);
      Stats::on_exit_acquire(sa);
      Logging::log_event(exit_wait);
      return scheduler_status_active;
    };

    auto worker_loop = [&] {
      Scheduler_configuration::initialize_worker();
      auto &my_deque = deques.mine();
      scheduler_status_type status = scheduler_status_active;
      fiber_type *current = nullptr;
      while (status == scheduler_status_active) {
        current = flush();
        while ((current != nullptr) || !my_deque.empty()) {
          current = (current == nullptr) ? my_deque.pop() : current;
          if (current != nullptr) {
            auto s = current->exec();
            if (s == fiber_status_continue) {
              buffers.mine().push_back(current);
            } else if (s == fiber_status_pause) {
              // do nothing
            } else if (s == fiber_status_finish) {
              current->finish();
            } else {
              assert(s == fiber_status_terminate);
              current->finish();
              status = scheduler_status_finish;
              Logging::log_event(initiate_teardown);
              should_terminate = true;
              // This worker is currently busy, so it has no children!
            }
            current = flush();
          }
        }
        assert((current == nullptr) && my_deque.empty());
        status = acquire();
      }
      Scheduler_configuration::wait_to_terminate_ping_thread();
      {
        std::unique_lock<std::mutex> lk(exit_lock);
        auto nb = ++nb_workers_exited;
        if (perworker::unique_id::get_my_id() == 0) {
          exit_condition_variable.wait(
              lk, [&] { return nb_workers_exited == nb_workers; });
        } else if (nb == nb_workers) {
          exit_condition_variable.notify_one();
        }
      }
    };
    
    for (std::size_t i = 0; i < random_number_generators.size(); ++i) {
      // The hash function used here has a weired property: 0 == hash(0)
      random_number_generators[i] = hash(i + 31);
    }

    elastic_type::initialize();
    
    Scheduler_configuration::initialize_signal_handler();

    termination_barrier.set_active(true);
    for (std::size_t i = 1; i < nb_workers; i++) {
      auto t = std::thread([&] {
        termination_barrier.set_active(true);
        worker_loop();
      });
      pthreads[i] = t.native_handle();
      t.detach();
    }
    pthreads[0] = pthread_self();
    Scheduler_configuration::launch_ping_thread(nb_workers, pthreads);
    worker_loop();
  }

  static
  fiber_type* take() {
    auto& my_buffer = buffers.mine();
    auto& my_deque = deques.mine();
    fiber_type* current = nullptr;
    assert(my_buffer.empty());
    current = my_deque.pop();
    if (current != nullptr) {
      my_buffer.push_back(current);
    }
    return current;
  }
  
  static
  void schedule(fiber_type* f) {
    assert(f->is_ready());
    buffers.mine().push_back(f);
  }

  static
  void commit() {
    auto f = flush();
    if (f != nullptr) {
      deques.mine().push(f);
    }
  }

};

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::cl_deque_type> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::deques;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::buffer_type> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::buffers;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<hash_value_type> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::random_number_generators;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<pthread_t> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::pthreads;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
Fiber<Scheduler_configuration>* take() {
  return chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::take();  
}

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
void schedule(Fiber<Scheduler_configuration>* f) {
  chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::schedule(f);  
}

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
void commit() {
  chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::commit();
}
  
} // end namespace
