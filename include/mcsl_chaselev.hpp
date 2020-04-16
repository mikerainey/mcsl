#pragma once

#include <memory>
#include <assert.h>
#include <deque>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <semaphore.h>

#include "mcsl_stats.hpp"
#include "mcsl_logging.hpp"

namespace mcsl {

// Status word. 64-bits wide
union status_word {
    uint64_t asUint64; // The order of fields is significant 
                       // Always initializes the first member
    struct {
      uint8_t  busybit  : 1 ;
      uint64_t priority : 56;
      uint8_t  head     : 7 ;  // Supports at most 128 processors
    } bits; 
};

  // A status word that can be operated on atomically
  // 1) clear() will always success in bounded number of steps.
  // 2) setBusyBit() uses atomic fetch_and_AND. It is guaranteed to
  //    succeed in bounded number of steps.
  // 3) updateHead() may fail. It's upto the caller to verify that the
  //    operations succeeded. This is to ensure that the operation completes
  //    in bounded number of steps.
  // Invariant: If a worker is busy, its head field points to itself
class AtomicStatusWord {
    std::atomic<uint64_t> statusWord;

public:
    // Since no processor can be a child of itself, the thread_id of the 
    // processor itself can be used as the nullary value of the head

    AtomicStatusWord() : statusWord(UINT64_C(0)) {}

    AtomicStatusWord(uint64_t prio, uint8_t nullaryHead) {
      clear(prio, nullaryHead);
    }

    // 1) Unsets the busy bit
    // 2) Hashes and obtain a new priority
    // 3) Resets the head value
    void clear(uint64_t prio, uint8_t nullaryHead, bool isBusy=false) {
      status_word word = {UINT64_C(0)};
      word.bits.busybit  = isBusy;   // Not busy
      word.bits.priority = prio; 
      word.bits.head     = nullaryHead;
      statusWord.store(word.asUint64);
    }

    void clear(std::function<uint64_t(void)> rng, uint8_t nullary, bool isBusy=false) {
        auto prio = rng();
        clear(prio, nullary, isBusy);
    }

    // Sets busy bit and returns the old status word
    status_word setBusyBit() {
      status_word word = {UINT64_C(0)};
      word.bits.busybit = 1u; // I'm going to be busy
      word = {statusWord.fetch_or(word.asUint64)};
      return word;
    }

    // Update the head field while preserving all other fields
    bool casHead(status_word word, uint8_t newHead) {
      uint64_t expected = word.asUint64;
      auto word2 = word;
      word2.bits.head = newHead; // Update only the head field
      return statusWord.compare_exchange_weak(expected, word2.asUint64);
    }

    status_word load() {
      return status_word{statusWord.load()};
    }
};

// Grouping fields for elastic scheduling together for potentiallly
// better cache behavior and easier initialization.
struct ElasticSchedFields {
    AtomicStatusWord     status;
    sem_t                sem;     
    size_t               next;    // Next pointer for the wake-up list

    ElasticSchedFields() {}
};
  
/*---------------------------------------------------------------------*/
/* Chase-Lev Work-Stealing Deque data structure  */
/* 
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

using random_number_seed_type = uint64_t;
  
template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
class chase_lev_work_stealing_scheduler {
private:

  using fiber_type = Fiber<Scheduler_configuration>;

  using cl_deque_type = chase_lev_deque<fiber_type>;

  using buffer_type = std::deque<fiber_type*>;

  static
  perworker::array<cl_deque_type> deques;

  static
  perworker::array<buffer_type> buffers;

  static
  perworker::array<random_number_seed_type> random_number_generators;

  static
  perworker::array<ElasticSchedFields> elastic;

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

    // Busybit is set separately, this function only traverses and wakes people up
    auto wakeChildren = [&]() {
        auto my_id = perworker::unique_id::get_my_id();
        auto status = elastic.mine().status.load();
        auto idx = status.bits.head;
        while (idx != my_id) {
            Logging::log_wake_child(idx);
            sem_post(&elastic[idx].sem);
            idx = elastic[idx].next;
        }
    };

    auto randMyRng = [&] {
        auto& rn = random_number_generators.mine();
        rn = hash(rn);
        return rn;
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
      fiber_type *current = nullptr;
      while (current == nullptr) {
        auto k = random_other_worker(nb_workers, my_id);
        termination_barrier.set_active(true);
        if (!deques[k].empty()) {
          current = deques[k].steal();
          if (current == nullptr) {
            termination_barrier.set_active(false);
          } else {
            Stats::increment(Stats::configuration_type::nb_steals);
          }
        }
        if (current == nullptr) { 
          // For whatever reason we failed to steal from our victim
          // It is possible that we are in this branch because the steal failed
          // due to contention instead of empty queue. However we are still safe 
          // because of the busy bit.
          auto target_status = elastic[k].status.load();
          auto my_status = elastic[my_id].status.load();
          if ((!target_status.bits.busybit) && 
              (target_status.bits.priority > my_status.bits.priority)){
            elastic[my_id].next = target_status.bits.head;
            // It's safe to just leave it in the array even if the following
            // CAS fails because it will never be referenced in case of failure.
            if (elastic[k].status.casHead(target_status, my_id)) {
              // Wait on my own semaphore
              Logging::log_enter_sleep(k, target_status.bits.priority, my_status.bits.priority);
              sem_wait(&elastic[my_id].sem);
              elastic[my_id].status.clear(randMyRng, my_id, true);
              Logging::log_event(exit_sleep);
              // TODO: Add support for CRS
            } // Otherwise we just give up
          }
        } else {
          // We succeeded in stealing, let's start to wake people up
          elastic.mine().status.setBusyBit();
          wakeChildren();
        }
        if (termination_barrier.is_terminated() || should_terminate) {
          assert(current == nullptr);
          wakeChildren();
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
              should_terminate = true;
              wakeChildren();
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


    // Initializations
    
    for (std::size_t i = 0; i < random_number_generators.size(); ++i) {
      random_number_generators[i] = hash(i);
    }
    
    for (std::size_t i = 0; i < elastic.size(); ++i) {
        // We need to start off by setting everyone as busy
        // Using the first processor's rng to initialize everyone's prio seems fine
        elastic[i].status.clear(randMyRng, i, true);  
        sem_init(&elastic[i].sem, 0, 0); // Initialize the semaphore
        // We don't really care what next points to at this moment
    }

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
perworker::array<random_number_seed_type> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::random_number_generators;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<pthread_t> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::pthreads;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<ElasticSchedFields> 
chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::elastic;

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
