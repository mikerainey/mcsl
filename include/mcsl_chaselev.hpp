#include <memory>
#include <assert.h>
#include <deque>
#include <thread>
#include <condition_variable>

#include "mcsl_atomic.hpp"
#include "mcsl_aligned.hpp"
#include "mcsl_random.hpp"
#include "mcsl_stats.hpp"

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

/*---------------------------------------------------------------------*/
/* Work-stealing scheduler  */

using fiber_status_type = enum fiber_status_enum {
  fiber_status_continue,
  fiber_status_pause,
  fiber_status_finished
};

template <typename Scheduler_configuration,
	  template <typename> typename Fiber,
	  typename Stats>
class chase_lev_work_stealing_scheduler {
private:

  using fiber_type = Fiber<Scheduler_configuration>;

  using cl_deque_type = chaselev_deque<fiber_type>;

  using buffer_type = std::deque<fiber_type*>;

  static
  perworker::array<cl_deque_type> deques;

  static
  perworker::array<buffer_type> buffers;

  static
  perworker::array<std::mt19937> random_number_generators;

  static
  std::size_t random_other_worker(size_t my_id) {
    return random::other_worker(my_id, random_number_generators);
  }

  static
  perworker::array<pthread_t> pthreads;
  
public:

  static
  void launch(std::size_t nb_workers) {
    std::atomic<std::size_t> nb_running_workers(nb_workers);
    std::size_t nb_workers_exited = 0;
    std::mutex exit_lock;
    std::condition_variable exit_condition_variable;

    using scheduler_status_type = enum scheduler_status_enum {
      scheduler_status_active,
      scheduler_status_finished
    };

    auto flush = [&] {
      auto& my_buffer = buffers.mine();
      auto& my_deque = deques.mine();
      fiber_type* current = nullptr;
      if (my_buffer.empty()) {
        return current;
      }
      current = my_buffer.back();
      my_buffer.pop_back();
      while (! my_buffer.empty()) {
        auto f = my_buffer.front();
        my_buffer.pop_front();
        my_deque.push(f);
      }
      assert(current != nullptr);
      return current;
    };

    auto acquire = [&] {
      if (perworker::unique_id::get_nb_workers() == 1) {
        --nb_running_workers;
        return scheduler_status_finished;
      }
      if (--nb_running_workers == 0) {
        return scheduler_status_finished;
      }
      auto sa = Stats::on_enter_acquire();
      auto my_id = perworker::unique_id::get_my_id();
      fiber_type* current = nullptr;
      while (true) {
        nb_running_workers++;
        auto k = random_other_worker(my_id);
        current = deques[k].steal();
        if (current != nullptr) {
          Stats::increment(Stats::configuration_type::nb_steals);
          break;
        }
        if (--nb_running_workers == 0) {
          Stats::on_exit_acquire(sa);
          return scheduler_status_finished;
        }
      }
      assert(current != nullptr);
      buffers.mine().push_back(current);
      Stats::on_exit_acquire(sa);
      return scheduler_status_active;
    };

    auto worker_loop = [&] {
      Scheduler_configuration::initialize_worker();
      auto& my_deque = deques.mine();
      scheduler_status_type status = scheduler_status_active;
      fiber_type* current = nullptr;
      while (status == scheduler_status_active) {
        current = flush();
        while ((current != nullptr) || ! my_deque.empty()) {
          current = (current == nullptr) ? my_deque.pop() : current;
          if (current != nullptr) {
            auto s = current->run();
            if (s == fiber_status_continue) {
              buffers.mine().push_back(current);
            } else if (s == fiber_status_pause) {
              // do nothing
            } else {
              assert(s == fiber_status_finished);
              delete current;
            }
            current = flush();
          }
        }
        assert((current == nullptr) && my_deque.empty());
        status = acquire();
      }
      std::unique_lock<std::mutex> lk(exit_lock);
      auto nb = ++nb_workers_exited;
      if (perworker::unique_id::get_my_id() == 0) {
	exit_condition_variable.wait(lk, [&] { return nb_workers_exited != nb_workers; });
      } else if (nb == nb_workers) {
	exit_condition_variable.notify_one();
      }
    };

    for (std::size_t i = 0; i < random_number_generators.size(); ++i) {
      random_number_generators[i].seed(i);
    }

    Scheduler_configuration::initialize_signal_handler();

    for (int i = 1; i < nb_workers; i++) {
      auto t = std::thread([&] {
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
  void schedule(fiber_type* f) {
    assert(f->is_ready());
    buffers.mine().push_back(f);
  }
  
};

template <typename Scheduler_configuration,
	template <typename> typename Fiber,
	typename Stats>
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::cl_deque_type> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::deques;

template <typename Scheduler_configuration,
	template <typename> typename Fiber,
	typename Stats>
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::buffer_type> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::buffers;

template <typename Scheduler_configuration,
	template <typename> typename Fiber,
	typename Stats>
perworker::array<std::mt19937> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::random_number_generators;

template <typename Scheduler_configuration,
	  template <typename> typename Fiber,
	  typename Stats>
perworker::array<pthread_t> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::pthreads;

template <typename Scheduler_configuration,
	  template <typename> typename Fiber,
	  typename Stats>
void schedule(Fiber<Scheduler_configuration>* f) {
  chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats>::schedule(f);  
}

} // end namespace

#endif /*! _MCSL_CHASELEV_H_ */
