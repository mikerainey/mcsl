#include <iostream>
#include <chrono>
#include <ctime>

#include "mcsl_fjnative.hpp"

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
      mcsl::basic_stats::increment(mcsl::basic_stats_configuration::nb_fibers);
      mcsl::basic_stats::increment(mcsl::basic_stats_configuration::nb_fibers);
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

int64_t fib_fjnative(int64_t n) {
  if (n <= 1) {
    return n;
  } else {
    int64_t r1, r2;
    mcsl::fork2([&] {
      r1 = fib_fjnative(n-1);
    }, [&] {
      r2 = fib_fjnative(n-2);
    });
    return r1 + r2;
  }
}

int main() {
  std::size_t nb_workers = 1;
  int64_t n = 30;
  int64_t dst = 0;

  mcsl::basic_stats::on_enter_launch();

  auto f = [&] {
    dst = fib_fjnative(n);
  };             
  auto f_body = mcsl::new_fjnative_of_function(f);
  /*
  auto f_body = new fib_fiber<mcsl::basic_scheduler_configuration>(n, &dst);
  */
  f_body->release();
  using scheduler_type = mcsl::chase_lev_work_stealing_scheduler<mcsl::basic_scheduler_configuration, mcsl::fiber, mcsl::basic_stats>;

  auto start_time = std::chrono::system_clock::now();
  scheduler_type::launch(nb_workers);
  auto end_time = std::chrono::system_clock::now();
  
  mcsl::basic_stats::on_exit_launch();
  {
    assert(fib_seq(n) == dst);
    std::chrono::duration<double> elapsed = end_time - start_time;
    printf("exectime %.3f\n", elapsed.count());
    printf("result %ld\n", dst);
  }
  mcsl::basic_stats::report();

  return 0;
}
