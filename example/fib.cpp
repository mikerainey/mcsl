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

int main(int argc, char** argv) {
  int64_t n = 30;
  int64_t dst = 0;

  /*
    deepsea::cmdline::set(argc, argv);
    n = deepsea::cmdline::parse_or_default_int("n", n);
  auto bench_pre = [&] { };
    auto bench_post = [] {};
    auto f_body = new fib_fiber<mcsl::basic_scheduler_configuration>(n, &dst);
    mcsl::launch0<mcsl::basic_scheduler_configuration, mcsl::basic_stats, mcsl::basic_logging, decltype(bench_pre), decltype(bench_post)>(argc, argv, bench_pre, bench_post, f_body);
    return 0; */
  mcsl::launch(argc, argv,
               [&] {
                 n = deepsea::cmdline::parse_or_default_int("n", n);
               },
               [&] {
                 assert(fib_seq(n) == dst);
                 printf("result %ld\n", dst);
               }, [&] {
                 dst = fib_fjnative(n);
               });
  return 0;
}
