#include <iostream>
#include <chrono>
#include <ctime>

#include "mcsl_fiber.hpp"

/*---------------------------------------------------------------------*/
/* Stats */

class stats_configuration {
public:

#ifdef MCSL_ENABLE_STATS
  static constexpr
  bool enabled = true;
#else
  static constexpr
  bool enabled = false;
#endif

  using counter_id_type = enum counter_id_enum {
    nb_fibers,
    nb_steals,
    nb_counters
  };

  static
  const char* name_of_counter(counter_id_type id) {
    std::map<counter_id_type, const char*> names;
    names[nb_fibers] = "nb_fibers";
    names[nb_steals] = "nb_steals";
    return names[id];
  }
  
};

using stats = mcsl::stats_base<stats_configuration>;

class noop_scheduler_configuration {
public:
  
  static
  void initialize_worker() {
  }  

  static
  void initialize_signal_handler(mcsl::ping_thread_status_type& status) {
    status = mcsl::ping_thread_status_disable;
  }
  
  static
  void launch_ping_thread(std::size_t, mcsl::perworker::array<pthread_t>&,
                          mcsl::ping_thread_status_type&,
                          std::mutex&,
                          std::condition_variable&) {
  }


  template <template <typename> typename Fiber>
  static
  void schedule(Fiber<noop_scheduler_configuration>* f) {
    mcsl::schedule<noop_scheduler_configuration, Fiber, stats>(f);
  }

};

int64_t fib_seq(int64_t n) {
  if (n <= 1) {
    return n;
  } else {
    return fib_seq(n-1) + fib_seq(n-2);
  }
}

int64_t fib_T = 15;

template <typename Scheduler_configuration>
class fib_fiber : public mcsl::fiber<Scheduler_configuration> {
public:

  using trampoline_type = enum { entry, exit };

  trampoline_type trampoline = entry;

  int64_t n; int64_t* dst;
  int64_t d1, d2;

  fib_fiber(int64_t n, int64_t* dst)
    : mcsl::fiber<Scheduler_configuration>(), n(n), dst(dst) { }

  mcsl::fiber_status_type run() {
    switch (trampoline) {
    case entry: {
      if (n <= fib_T) {
        *dst = fib_seq(n);
        break;
      }
      auto f1 = new fib_fiber(n-1, &d1);
      auto f2 = new fib_fiber(n-2, &d2);
      mcsl::fiber<Scheduler_configuration>::add_edge(f1, this);
      mcsl::fiber<Scheduler_configuration>::add_edge(f2, this);
      f1->release();
      f2->release();
      stats::increment(stats_configuration::nb_fibers);
      stats::increment(stats_configuration::nb_fibers);
      trampoline = exit;
      return mcsl::fiber_status_pause;	  
    }
    case exit: {
      *dst = d1 + d2;
      break;
    }
    }
    return mcsl::fiber_status_finish;
  }

};

int main() {
  std::size_t nb_workers = 4;
  int64_t n = 30;
  int64_t dst = 0;
  auto f_body = new fib_fiber<noop_scheduler_configuration>(n, &dst);
  f_body->release();
  using scheduler_type = mcsl::chase_lev_work_stealing_scheduler<noop_scheduler_configuration, mcsl::fiber, stats>;
  stats::on_enter_launch();
  auto start_time = std::chrono::system_clock::now();
  scheduler_type::launch(nb_workers);
  auto end_time = std::chrono::system_clock::now();
  stats::on_exit_launch();
  {
    assert(fib_seq(n) == dst);
    std::chrono::duration<double> elapsed = end_time - start_time;
    printf("exectime %.3f\n", elapsed.count());
    printf("result %ld\n", dst);
  }
  stats::report();

  return 0;
}
