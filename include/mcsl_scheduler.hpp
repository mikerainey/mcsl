#pragma once

#if defined(MCSL_LINUX)
#include <thread>
#include <condition_variable>
#elif defined(MCSL_NAUTILUS)
typedef uint64_t nk_stack_size_t;
typedef void* nk_thread_id_t;
typedef void (*nk_thread_fun_t)(void * input, void ** output);
extern "C"
int
nk_thread_start (nk_thread_fun_t fun, 
                 void * input,
                 void ** output,
                 uint8_t is_detached,
                 nk_stack_size_t stack_size,
                 nk_thread_id_t * tid,
                 int bound_cpu); // -1 => not bound
extern "C"
void nk_join_all_children(int);
#define TSTACK_DEFAULT 0  // will be 4K
using nk_worker_activation_type = std::pair<std::size_t, std::function<void(std::size_t)>>;
static
void nk_thread_init_fn(void *in, void **out) {
  nk_worker_activation_type* p = (nk_worker_activation_type*)in;
  std::function<void(std::size_t)>* fp = &(p->second);
  (*fp)(p->first);
  delete p;
}
#endif

#include "mcsl_util.hpp"
#include "mcsl_perworker.hpp"

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Statistics */

class minimal_stats {
public:

  class configuration_type {
  public:
    using counter_id_type = enum counter_id_enum {
      nb_fibers,
      nb_steals,
      nb_sleeps,
      nb_counters
    };
  };

  static
  void start_collecting() { }

  static
  void on_enter_launch() { }

  static
  void on_exit_launch() { }

  static
  void report(std::size_t) { }

  static
  clock::time_point_type on_enter_acquire() {
    return clock::now();
  }

  static
  void on_exit_acquire(clock::time_point_type enter_acquire_time) { }

  static
  clock::time_point_type on_enter_sleep() {
    return clock::now();
  }

  static
  void on_exit_sleep(clock::time_point_type enter_sleep_time) { }

  static inline
  void increment(configuration_type::counter_id_type id) { }
  
};

/*---------------------------------------------------------------------*/
/* Logging */

using event_kind_type = enum event_kind_enum {
  phases = 0,
  fibers,
  migration,
  program,
  nb_kinds
};

using event_tag_type = enum event_tag_type_enum {
  // important: the following events are reserved for pview
  enter_launch = 0,   exit_launch,
  enter_algo,         exit_algo,
  enter_wait,         exit_wait,
  worker_communicate, interrupt,
  algo_phase,
  // all the remaining events are free to be changed
  enter_sleep,        exit_sleep,     failed_to_sleep,
  wake_child,         worker_exit,    initiate_teardown,
  program_point,
  nb_events
};

class minimal_logging {
public:

  static
  void initialize() { }

  static
  void output(std::size_t) { }

  static inline
  void log_event(event_tag_type tag) { }

  static inline
  void log_enter_sleep(size_t parent_id, size_t prio_child, size_t prio_parent) { }

  static inline
  void log_wake_child(size_t child_id) { }

};

/*---------------------------------------------------------------------*/
/* Termination detection */

class minimal_termination_detection {
public:

  bool set_active(bool active) {
    return false;
  }

  bool is_terminated() {
    return false;
  }
  
};

/*---------------------------------------------------------------------*/
/* Workers (scheduler threads) */

#if defined(MCSL_LINUX)
  
class minimal_worker {
public:

  static
  void initialize_worker() { }

  template <typename Body>
  static
  void launch_worker_thread(std::size_t id, const Body& b) {
    auto t = std::thread([id, &b] {
      perworker::unique_id::initialize_worker(id);
      b(id);
    });
    t.detach();
  }

  class worker_exit_barrier {
  private:

    std::size_t nb_workers;
    std::size_t nb_workers_exited = 0;
    std::mutex exit_lock;
    std::condition_variable exit_condition_variable;

  public:

    worker_exit_barrier(std::size_t nb_workers) : nb_workers(nb_workers) { }

    void wait(std::size_t my_id)  {
      std::unique_lock<std::mutex> lk(exit_lock);
      auto nb = ++nb_workers_exited;
      if (my_id == 0) {
        exit_condition_variable.wait(
          lk, [&] { return nb_workers_exited == nb_workers; });
      } else if (nb == nb_workers) {
        exit_condition_variable.notify_one();
      }
    }

  };

  using termination_detection_type = minimal_termination_detection;

};

#elif defined(MCSL_NAUTILUS)

class minimal_worker {
public:

  static
  void initialize_worker() { }

  template <typename Body>
  static
  void launch_worker_thread(std::size_t id, const Body& b) {
    std::function<void(std::size_t)> f = [&] (std::size_t id) {
      perworker::unique_id::initialize_worker(id);
      b(id);
    };
    nk_worker_activation_type p = new nk_worker_activation_type(id, f);
    nk_thread_start(nk_thread_init_fn, (void*)p,0,0,TSTACK_DEFAULT,0,-1);

  }

  class worker_exit_barrier {
  private:

  public:

    worker_exit_barrier(std::size_t) { }

    void wait(std::size_t my_id)  {
      if (my_id == 0) {
        nk_join_all_children(0);
      }
    }

  };

  using termination_detection_type = minimal_termination_detection;

};

#endif

/*---------------------------------------------------------------------*/
/* Fibers */

using fiber_status_type = enum fiber_status_enum {
  fiber_status_continue,
  fiber_status_pause,
  fiber_status_finish,
  fiber_status_terminate
};

template <typename Scheduler>
class minimal_fiber {
public:

  virtual
  fiber_status_type exec() {
    return fiber_status_terminate;
  }

  virtual
  void finish() {
    delete this;
  }

  bool is_ready() {
    return true;
  }
  
};

/*---------------------------------------------------------------------*/
/* Elastic work stealing */

template <typename Stats, typename Logging>
class minimal_elastic {
public:

  static
  void wake_children() { }

  static
  void try_to_sleep(std::size_t) {
    cycles::spin_for(1000);
  }

  static
  void accept_lifelines() { }

  static
  void initialize() { }
  
};

/*---------------------------------------------------------------------*/
/* Interrupts */

class minimal_interrupt {
public:

  static
  void initialize_signal_handler() { }

  static
  void wait_to_terminate_ping_thread() { }
  
  static
  void launch_ping_thread(std::size_t) { }

};
  
} // end namespace
