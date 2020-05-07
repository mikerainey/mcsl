#pragma once

#include <sys/time.h>
#include <sys/resource.h>

#include "mcsl_fiber.hpp"
#include "mcsl_snzi.hpp"

/*---------------------------------------------------------------------*/
/* Basic stats */

namespace mcsl {

class basic_stats_configuration {
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
    nb_sleeps,
    nb_counters
  };

  static
  const char* name_of_counter(counter_id_type id) {
    std::map<counter_id_type, const char*> names;
    names[nb_fibers] = "nb_fibers";
    names[nb_steals] = "nb_steals";
    names[nb_sleeps] = "nb_sleeps";
    return names[id];
  }
  
};

using basic_stats = stats_base<basic_stats_configuration>;

/*---------------------------------------------------------------------*/
/* Basic logging */

#ifdef MCSL_ENABLE_LOGGING
using basic_logging = logging_base<true>;
#else
using basic_logging = logging_base<false>;
#endif

/*---------------------------------------------------------------------*/
/* Basic elastic work stealing */

#ifdef MCSL_DISABLE_ELASTIC
template <typename Stats, typename Logging>
using basic_elastic = noop_elastic<Stats, Logging>;
#else
template <typename Stats, typename Logging>
using basic_elastic = default_elastic<Stats, Logging>;
#endif  

/*---------------------------------------------------------------------*/
/* Basic scheduler configuration */

class basic_scheduler_configuration {
public:
  
  static
  void initialize_worker() {
  }  

  static
  void initialize_signal_handler() {

  }

  static
  void wait_to_terminate_ping_thread() {
    
  }
  
  static
  void launch_ping_thread(std::size_t, perworker::array<pthread_t>&) {
    
  }

  template <template <typename> typename Fiber>
  static
  void schedule(Fiber<basic_scheduler_configuration>* f) {
    mcsl::schedule<basic_scheduler_configuration, Fiber, basic_elastic, basic_stats, basic_logging>(f);
  }

  template <template <typename> typename Fiber>
  static
  Fiber<basic_scheduler_configuration>* take() {
    return mcsl::take<basic_scheduler_configuration, Fiber, basic_elastic, basic_stats, basic_logging>();
  }

  using termination_detection_barrier_type = noop_termination_detection_barrier;

};

} // end namespace

/*---------------------------------------------------------------------*/
/* Context switching */

using _context_pointer = char*;

extern "C"
void* _mcsl_ctx_save(_context_pointer);
asm(R"(
.globl _mcsl_ctx_save
        .type _mcsl_ctx_save, @function
        .align 16
_mcsl_ctx_save:
        .cfi_startproc
        movq %rbx, 0(%rdi)
        movq %rbp, 8(%rdi)
        movq %r12, 16(%rdi)
        movq %r13, 24(%rdi)
        movq %r14, 32(%rdi)
        movq %r15, 40(%rdi)
        leaq 8(%rsp), %rdx
        movq %rdx, 48(%rdi)
        movq (%rsp), %rax
        movq %rax, 56(%rdi)
        xorq %rax, %rax
        ret
        .size _mcsl_ctx_save, .-_mcsl_ctx_save
        .cfi_endproc
)");

extern "C"
void _mcsl_ctx_restore(_context_pointer ctx, void* t);
asm(R"(
.globl _mcsl_ctx_restore
        .type _mcsl_ctx_restore, @function
        .align 16
_mcsl_ctx_restore:
        .cfi_startproc
        movq 0(%rdi), %rbx
        movq 8(%rdi), %rbp
        movq 16(%rdi), %r12
        movq 24(%rdi), %r13
        movq 32(%rdi), %r14
        movq 40(%rdi), %r15
        test %rsi, %rsi
        mov $01, %rax
        cmove %rax, %rsi
        mov %rsi, %rax
        movq 56(%rdi), %rdx
        movq 48(%rdi), %rsp
        jmpq *%rdx
        .size _mcsl_ctx_restore, .-_mcsl_ctx_restore
        .cfi_endproc
)");

static constexpr
int thread_stack_szb = 1<<20;

namespace mcsl {
  
class context {  
public:
  
  typedef char context_type[8*8];
  
  using context_pointer = _context_pointer;
  
  template <class X>
  static
  context_pointer addr(X r) {
    return r;
  }
  
  template <class Value>
  static
  void throw_to(context_pointer ctx, Value val) {
    _mcsl_ctx_restore(ctx, (void*)val);
  }
  
  template <class Value>
  static
  void swap(context_pointer ctx1, context_pointer ctx2, Value val2) {
    if (_mcsl_ctx_save(ctx1)) {
      return;
    }
    _mcsl_ctx_restore(ctx2, val2);
  }
  
  // register number 6
#define _X86_64_SP_OFFSET   6
  
  template <class Value>
  static
  Value capture(context_pointer ctx) {
    void* r = _mcsl_ctx_save(ctx);
    return (Value)r;
  }
  
  template <class Value>
  static
  char* spawn(context_pointer ctx, Value val) {
    Value target;
    if (target = (Value)_mcsl_ctx_save(ctx)) {
      target->enter(target);
      assert(false);
    }
    char* stack = (char*)malloc(thread_stack_szb);
    void** _ctx = (void**)ctx;
    _ctx[_X86_64_SP_OFFSET] = &stack[thread_stack_szb];
    return stack;
  }
  
};

class context_wrapper_type {
public:
  context::context_type ctx;
};

static
perworker::array<context_wrapper_type> ctxs;

static
context::context_pointer my_ctx() {
  return context::addr(ctxs.mine().ctx);
}

/*---------------------------------------------------------------------*/
/* Native fork join */

class forkable_fiber {
public:

  virtual
  void fork2(forkable_fiber*, forkable_fiber*) = 0;

};

static
perworker::array<forkable_fiber*> current_fiber;

class fjnative : public fiber<basic_scheduler_configuration>, public forkable_fiber {
public:

  using context_type = context::context_type;

  // declaration of dummy-pointer constants
  static
  char dummy1, dummy2;
  
  static constexpr
  char* notaptr = &dummy1;
  /* indicates to a thread that the thread does not need to deallocate
   * the call stack on which it is running
   */
  static constexpr
  char* notownstackptr = &dummy2;

  fiber_status_type status = fiber_status_finish;

  // pointer to the call stack of this thread
  char* stack = nullptr;
  // CPU context of this thread
  context_type ctx;

  void swap_with_scheduler() {
    context::swap(context::addr(ctx), my_ctx(), notaptr);
  }

  static
  void exit_to_scheduler() {
    context::throw_to(my_ctx(), notaptr);
  }

  virtual
  void run2() = 0;  

  fiber_status_type run() {
    run2();
    return status;
  }

  // point of entry from the scheduler to the body of this thread
  // the scheduler may reenter this fiber via this method
  fiber_status_type exec() {
    if (stack == nullptr) {
      // initial entry by the scheduler into the body of this thread
      stack = context::spawn(context::addr(ctx), this);
    }
    current_fiber.mine() = this;
    // jump into body of this thread
    context::swap(my_ctx(), context::addr(ctx), this);
    return status;
  }

  // point of entry to this thread to be called by the `context::spawn` routine
  static
  void enter(fjnative* t) {
    assert(t != nullptr);
    assert(t != (fjnative*)notaptr);
    t->run();
    // terminate thread by exiting to scheduler
    exit_to_scheduler();
  }

  fjnative() : fiber() { }

  void finish() {
    notify();
  } 

  ~fjnative() {
    if ((stack == nullptr) || (stack == notownstackptr)) {
      return;
    }
    auto s = stack;
    stack = nullptr;
    free(s);
  }

  void fork2(forkable_fiber* _f1, forkable_fiber* _f2) {
    mcsl::basic_stats::increment(mcsl::basic_stats_configuration::nb_fibers);
    mcsl::basic_stats::increment(mcsl::basic_stats_configuration::nb_fibers);
    fjnative* f1 = (fjnative*)_f1;
    fjnative* f2 = (fjnative*)_f2;
    status = fiber_status_pause;
    add_edge(f2, this);
    add_edge(f1, this);
    f2->release();
    f1->release();
    if (context::capture<fjnative*>(context::addr(ctx))) {
      //      util::atomic::aprintf("steal happened: executing join continuation\n");
      return;
    }
    // know f1 stays on my stack
    f1->stack = notownstackptr;
    f1->swap_with_scheduler();
    // sched is popping f1
    // run begin of sched->exec(f1) until f1->exec()
    f1->run();
    // if f2 was not stolen, then it can run in the same stack as parent
    auto f = basic_scheduler_configuration::take<fiber>();
    if (f == nullptr) {
      status = fiber_status_finish;
      //      util::atomic::aprintf("%d %d detected steal of %p\n",id,util::worker::get_my_id(),f2);
      exit_to_scheduler();
      return; // unreachable
    }
    //    util::atomic::aprintf("%d %d ran %p; going to run f %p\n",id,util::worker::get_my_id(),f1,f2);
    // prepare f2 for local run
    assert(f == f2);
    assert(f2->stack == nullptr);
    f2->stack = notownstackptr;
    f2->swap_with_scheduler();
    //    util::atomic::aprintf("%d %d this=%p f1=%p f2=%p\n",id,util::worker::get_my_id(),this, f1, f2);
    //    printf("ran %p and %p locally\n",f1,f2);
    // run end of sched->exec() starting after f1->exec()
    // run begin of sched->exec(f2) until f2->exec()
    f2->run();
    status = fiber_status_finish;
    swap_with_scheduler();
    // run end of sched->exec() starting after f2->exec()
  }

};

char fjnative::dummy1;
char fjnative::dummy2;

template <typename F>
class fjnative_of_function : public fjnative {
public:

  fjnative_of_function(const F& f) : fjnative(), f(f) { }

  F f;

  void run2() {
    f();
  }
};

template <class F1, class F2>
void fork2(const F1& f1, const F2& f2) {
#if defined(MCSL_SEQUENTIAL_ELISION)
  f1();
  f2();
#else
  auto f = current_fiber.mine();
  assert(f != nullptr);
  fjnative_of_function<F1> fj1(f1);
  fjnative_of_function<F2> fj2(f2);
  f->fork2(&fj1, &fj2);
#endif
}


/*---------------------------------------------------------------------*/
/* Scheduler launch */
  
bool started = false;
  
template <typename Scheduler_configuration, typename Stats, typename Logging,
          typename Bench_pre, typename Bench_post>
void launch0(const Bench_pre& bench_pre,
	     const Bench_post& bench_post,
	     fiber<Scheduler_configuration>* f_body) {
  using scheduler_type = chase_lev_work_stealing_scheduler<Scheduler_configuration, fiber, basic_elastic, Stats, Logging>;
  std::size_t nb_workers = deepsea::cmdline::parse_or_default_int("proc", 1);
  {
    deepsea::cmdline::dispatcher d;
    d.add("once", [] { scheduler_type::nb_steal_attempts = 1; });
    d.add("coupon", [&] { scheduler_type::nb_steal_attempts = nb_workers * 100; });
    d.dispatch_or_default("steal_policy", "once");
  }
  {
    deepsea::cmdline::dispatcher d;
    d.add("default", [] { scheduler_type::elastic_type::policy = elastic_policy_enabled; });
    d.add("disabled", [&] { scheduler_type::elastic_type::policy = elastic_policy_disabled; });
    d.dispatch_or_default("elastic_policy", "default");
  }
  clock::time_point_type start_time;
  struct rusage ru_before, ru_after;
  double elapsed;
  Logging::initialize();
  fjnative_of_function fj_init([&] { started = true; });
  fjnative_of_function fj_bench_pre(bench_pre);
  fjnative_of_function fj_before_bench([&] {
    Logging::log_event(enter_algo); // to log that the benchmark f_body is to be scheduled next
    Stats::start_collecting();
    Stats::on_enter_launch();
    getrusage (RUSAGE_SELF, &ru_before);
    start_time = clock::now();
  });
  fjnative_of_function fj_after_bench([&] {
    getrusage (RUSAGE_SELF, &ru_after);
    elapsed = clock::since(start_time);
    Stats::on_exit_launch();
    Logging::log_event(exit_algo); // to log that the benchmark f_body has completed
    Stats::report(nb_workers);
  });
  fjnative_of_function fj_bench_post(bench_post);  
  {
    auto f_init = &fj_init;
    auto f_bench_pre = &fj_bench_pre;
    auto f_before_bench = &fj_before_bench;
    auto f_after_bench = &fj_after_bench;
    auto f_bench_post = &fj_bench_post;
    auto f_term = new terminal_fiber<Scheduler_configuration>;
    fiber<Scheduler_configuration>::add_edge(f_init, f_bench_pre);
    fiber<Scheduler_configuration>::add_edge(f_bench_pre, f_before_bench);
    fiber<Scheduler_configuration>::add_edge(f_before_bench, f_body);
    fiber<Scheduler_configuration>::add_edge(f_body, f_after_bench);
    fiber<Scheduler_configuration>::add_edge(f_after_bench, f_bench_post);
    fiber<Scheduler_configuration>::add_edge(f_bench_post, f_term);
    f_init->release();
    f_bench_pre->release();
    f_before_bench->release();
    f_body->release();    
    f_after_bench->release();
    f_bench_post->release();
    f_term->release();
  }
  scheduler_type::launch(nb_workers);
  aprintf("exectime %.3f\n", elapsed);
  {
    auto double_of_tv = [] (struct timeval tv) {
      return ((double) tv.tv_sec) + ((double) tv.tv_usec)/1000000.;
    };
    aprintf("usertime  %.3lf\n",
            double_of_tv(ru_after.ru_utime) -
            double_of_tv(ru_before.ru_utime));
    aprintf("systime  %.3lf\n",
            double_of_tv(ru_after.ru_stime) -
            double_of_tv(ru_before.ru_stime));
  }
  Logging::output(nb_workers);
}

template <typename Bench_pre, typename Bench_post, typename Bench_body>
void launch(const Bench_pre& bench_pre,
            const Bench_post& bench_post,
            const Bench_body& bench_body) {
  fjnative_of_function fj_body(bench_body);
  auto f_body = &fj_body;
  launch0<basic_scheduler_configuration, basic_stats, basic_logging, Bench_pre, Bench_post>(bench_pre, bench_post, f_body);
}

} // end namespace
