#pragma once

#include "mcsl_fiber.hpp"

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

using basic_stats = stats_base<basic_stats_configuration>;

/*---------------------------------------------------------------------*/
/* Basic Logging */

#ifdef MCSL_ENABLE_LOGGING
using basic_logging = logging_base<true>;
#else
using basic_logging = logging_base<false>;
#endif

/*---------------------------------------------------------------------*/
/* Basic scheduler configuration */

class basic_scheduler_configuration {
public:
  
  static
  void initialize_worker() {
  }  

  static
  void initialize_signal_handler(ping_thread_status_type& status) {
    status = ping_thread_status_disable;
  }
  
  static
  void launch_ping_thread(std::size_t, perworker::array<pthread_t>&,
                          ping_thread_status_type&,
                          std::mutex&,
                          std::condition_variable&) {
  }


  template <template <typename> typename Fiber>
  static
  void schedule(Fiber<basic_scheduler_configuration>* f) {
    mcsl::schedule<basic_scheduler_configuration, Fiber, basic_stats, basic_logging>(f);
  }

  template <template <typename> typename Fiber>
  static
  Fiber<basic_scheduler_configuration>* take() {
    return mcsl::take<basic_scheduler_configuration, Fiber, basic_stats, basic_logging>();
  }

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

  fjnative_of_function(const F& f) : f(f) { }

  F f;

  void run2() {
    f();
  }
};

template <class F>
fjnative_of_function<F>* new_fjnative_of_function(const F& f) {
  return new fjnative_of_function<F>(f);
}

template <class F1, class F2>
void fork2(const F1& f1, const F2& f2) {
#if defined(MCSL_SEQUENTIAL_ELISION)
  f1();
  f2();
#else
  auto f = current_fiber.mine();
  assert(f != nullptr);
  auto fp1 = new_fjnative_of_function(f1);
  auto fp2 = new_fjnative_of_function(f2);
  f->fork2(fp1, fp2);
#endif
}

/*---------------------------------------------------------------------*/
/* Scheduler launch */
  
template <typename Scheduler_configuration, typename Stats, typename Logging,
          typename Bench_pre, typename Bench_post>
void launch0(int argc, char** argv,
	     const Bench_pre& bench_pre,
	     const Bench_post& bench_post,
	     fiber<Scheduler_configuration>* f_body) {
  deepsea::cmdline::set(argc, argv);
  std::size_t nb_workers = deepsea::cmdline::parse_or_default_int("proc", 1);
  std::chrono::time_point<std::chrono::system_clock> start_time, end_time;
  Logging::initialize();
  bench_pre();
  {
    auto f_cont = new_fjnative_of_function([&] {
      end_time = std::chrono::system_clock::now();
    });
    fiber<Scheduler_configuration>::add_edge(f_body, f_cont);
    start_time = std::chrono::system_clock::now();
    f_cont->release();
    f_body->release();
  }
  using scheduler_type = chase_lev_work_stealing_scheduler<Scheduler_configuration, fiber, Stats, Logging>;
  Stats::on_enter_launch();
  scheduler_type::launch(nb_workers);
  Stats::on_exit_launch();
  bench_post();
  std::chrono::duration<double> elapsed = end_time - start_time;
  printf("exectime %.3f\n", elapsed.count());
  Stats::report();
  Logging::output();
}

template <typename Bench_pre, typename Bench_post, typename Bench_body>
void launch(int argc, char** argv,
            const Bench_pre& bench_pre,
            const Bench_post& bench_post,
            const Bench_body& bench_body) {
  auto f_body = new_fjnative_of_function([&] {
    bench_body();
  });
  launch0<basic_scheduler_configuration, basic_stats, basic_logging, Bench_pre, Bench_post>(argc, argv, bench_pre, bench_post, f_body);
}

} // end namespace
