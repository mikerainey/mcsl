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

template <typename Scheduler>
class fib_fiber : public mcsl::fiber<Scheduler> {
public:

  using trampoline_type = enum { entry, exit };

  trampoline_type trampoline = entry;

  int64_t n; int64_t* dst;
  int64_t d1, d2;

  fib_fiber(int64_t n, int64_t* dst)
    : mcsl::fiber<Scheduler>(), n(n), dst(dst) { }

  mcsl::fiber_status_type run() {
    switch (trampoline) {
    case entry: {
      if (n <= fib_T) {
        *dst = fib_seq(n);
        break;
      }
      auto f1 = new fib_fiber(n-1, &d1);
      auto f2 = new fib_fiber(n-2, &d2);
      mcsl::fiber<Scheduler>::add_edge(f1, this);
      mcsl::fiber<Scheduler>::add_edge(f2, this);
      f1->release();
      f2->release();
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

  n = deepsea::cmdline::parse_or_default_int("n", n);

  deepsea::cmdline::dispatcher d;
  d.add("manual", [&] {
    using my_scheduler = mcsl::minimal_scheduler<>;
    auto nb_workers = deepsea::cmdline::parse_or_default_int("proc", 1);
    mcsl::perworker::unique_id::initialize(nb_workers);
    auto f_body = new fib_fiber<my_scheduler>(n, &dst);
    auto f_term = new mcsl::terminal_fiber<my_scheduler>;
    mcsl::fiber<my_scheduler>::add_edge(f_body, f_term);
    f_body->release();
    f_term->release();
    mcsl::chase_lev_work_stealing_scheduler<my_scheduler, mcsl::fiber>::launch(nb_workers);
    printf("result %ld\n", dst);
  });
  d.add("fjnative", [&] {
    mcsl::launch([&] {
     }, [&] {
      assert(fib_seq(n) == dst);
      printf("result %ld\n", dst);
    }, [&] {
      dst = fib_fjnative(n);
    });
  });
  d.dispatch_or_default("scheduler", "fjnative");
  
  return 0; 
}
