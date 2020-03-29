#pragma once

#include <memory>
#include <assert.h>
#include <deque>
#include <thread>
#include <condition_variable>

#include "mcsl_stats.hpp"
#include "mcsl_logging.hpp"

namespace mcsl {
  
/*---------------------------------------------------------------------*/
/* Chase-Lev Work-Stealing Deque data structure  */

// Deque from Arora, Blumofe, and Plaxton (SPAA, 1998).
template <typename Job>
struct chase_lev_deque {
  using qidx = unsigned int;
  using tag_t = unsigned int;

  // use unit for atomic access
  union age_t {
    struct {
      tag_t tag;
      qidx top;
    } pair;
    size_t unit;
  };

  // align to avoid false sharing
  struct alignas(64) padded_job { Job* job;  };

  static int
  const q_size = 200;
  age_t age;
  qidx bot;
  padded_job deq[q_size];

  inline bool cas(size_t* ptr, size_t oldv, size_t newv) {
    return __sync_bool_compare_and_swap(ptr, oldv, newv);
  }

  inline void fence() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  chase_lev_deque() : bot(0) {
    age.pair.tag = 0;
    age.pair.top = 0;
  }

  bool empty() {
    return bot == 0;
  }

  void push(Job* job) {
    qidx local_bot;
    local_bot = bot; // atomic load
    deq[local_bot].job = job; // shared store
    local_bot += 1;
    if (local_bot == q_size)
      throw std::runtime_error("internal error: scheduler queue overflow");
    bot = local_bot; // shared store
    fence();
  }

  Job* steal() {
    age_t old_age, new_age;
    qidx local_bot;
    Job *job, *result;
    old_age.unit = age.unit; // atomic load

    local_bot = bot; // atomic load
    if (local_bot <= old_age.pair.top)
      result = NULL;
    else {
      job = deq[old_age.pair.top].job; // atomic load
      new_age.unit = old_age.unit;
      new_age.pair.top = new_age.pair.top + 1;
      if (cas(&(age.unit), old_age.unit, new_age.unit))  // cas
	result = job;
      else
	result = NULL;
    }
    return result;
  }

  Job* pop() {
    age_t old_age, new_age;
    qidx local_bot;
    Job *job, *result;
    local_bot = bot; // atomic load
    if (local_bot == 0)
      result = NULL;
    else {
      local_bot = local_bot - 1;
      bot = local_bot; // shared store
      fence();
      job = deq[local_bot].job; // atomic load
      old_age.unit = age.unit; // atomic load
      if (local_bot > old_age.pair.top)
	result = job;
      else {
	bot = 0; // shared store
	new_age.pair.top = 0;
	new_age.pair.tag = old_age.pair.tag + 1;
	if ((local_bot == old_age.pair.top) &&
	    cas(&(age.unit), old_age.unit, new_age.unit))
	  result = job;
	else {
	  age.unit = new_age.unit; // shared store
	  result = NULL;
	}
	fence();
      }
    }
    return result;
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
  std::size_t random_other_worker(size_t nb_workers, size_t my_id) {
    assert(nb_workers != 1);
    auto& rn = random_number_generators.mine();
    auto id = (std::size_t)(rn % (nb_workers - 1));
    if (id >= my_id) {
      id++;
    }
    rn = hash(rn);
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

  static
  void launch(std::size_t nb_workers) {
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
      Logging::log_event(enter_wait);
      auto sa = Stats::on_enter_acquire();
      termination_barrier.set_active(false);
      auto my_id = perworker::unique_id::get_my_id();
      fiber_type* current = nullptr;
      while (current == nullptr) {
        auto k = random_other_worker(nb_workers, my_id);
	termination_barrier.set_active(true);
	current = deques[k].steal();
	if (current == nullptr) {
	  std::this_thread::yield();
	  termination_barrier.set_active(false);
	} else {
	  Stats::increment(Stats::configuration_type::nb_steals);
	}
        if (termination_barrier.is_terminated() || should_terminate) {
          assert(current == nullptr);
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
      auto& my_deque = deques.mine();
      scheduler_status_type status = scheduler_status_active;
      fiber_type* current = nullptr;
      while (status == scheduler_status_active) {
        current = flush();
        while ((current != nullptr) || ! my_deque.empty()) {
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
          exit_condition_variable.wait(lk, [&] { return nb_workers_exited == nb_workers; });
        } else if (nb == nb_workers) {
          exit_condition_variable.notify_one();
        }
      }
    };

    for (std::size_t i = 0; i < random_number_generators.size(); ++i) {
      random_number_generators[i] = hash(i);
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
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::cl_deque_type> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::deques;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<typename chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::buffer_type> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::buffers;

template <typename Scheduler_configuration,
          template <typename> typename Fiber,
          typename Stats, typename Logging>
perworker::array<random_number_seed_type> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::random_number_generators;

template <typename Scheduler_configuration,
	  template <typename> typename Fiber,
	  typename Stats, typename Logging>
perworker::array<pthread_t> chase_lev_work_stealing_scheduler<Scheduler_configuration,Fiber,Stats,Logging>::pthreads;

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
