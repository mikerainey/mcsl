#pragma once

#include "mcsl_fiber.hpp"

/*---------------------------------------------------------------------*/
/* Context switching */

using _context_pointer = char*;

extern "C"
void* _mcsl_ctx_save(_context_pointer ctx);

extern "C"
void _mcsl_ctx_restore(_context_pointer ctx, void* t);

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
  util::control::context::context_type ctx;
};

static
perworker::array<context_wrapper_type> ctxs;

static
context::context_pointer my_ctx() {
  return context::addr(ctxs.mine().ctx);
}

/*---------------------------------------------------------------------*/
/* Native fork join */

bool try_pop_fiber() {
  assert(false);
  return false;
}

class forkable_fiber {
public:

  virtual
  void fork2(forkable_fiber*, forkable_fiber*) = 0;

};

static
perworker::array<forkable_fiber*> fibers;

template <typename F>
class fjnative : public fiber<basic_scheduler_configuration>, public forkable_fiber {
public:

  using context_type = uctx::context::context_type;
  using context = uctx::context;

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

  F f;

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

  fiber_status_type run() {
    fibers.mine() = this;
    f();
    return status;
  }

  // point of entry from the scheduler to the body of this thread
  // the scheduler may reenter this fiber via this method
  fiber_status_type exec() {
    if (stack == nullptr) {
      // initial entry by the scheduler into the body of this thread
      stack = context::spawn(context::addr(ctx), this);
    }
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

  fjnative(const F& f)
    : thread(), f(f)  { }

  ~fjnative() {
    if ((stack == nullptr) || (stack == notownstackptr)) {
      return;
    }
    auto s = stack;
    stack = nullptr;
    free(s);
  }

  void fork2(fjnative* f1, fjnative* f2) {
    status = fiber_status_pause;
    add_edge(f1, this);
    add_edge(f2, this);
    f1->release();
    f2->release();
    stats::increment(stats_configuration::nb_fibers);
    stats::increment(stats_configuration::nb_fibers);
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
    if (try_pop_fiber()) {
      //      util::atomic::aprintf("%d %d detected steal of %p\n",id,util::worker::get_my_id(),f2);
      exit_to_scheduler();
      return; // unreachable
    }
    //    util::atomic::aprintf("%d %d ran %p; going to run f %p\n",id,util::worker::get_my_id(),f1,f2);
    // prepare f2 for local run
    assert(f2->stack == nullptr);
    f2->stack = notownstackptr;
    f2->swap_with_scheduler();
    //    util::atomic::aprintf("%d %d this=%p f1=%p f2=%p\n",id,util::worker::get_my_id(),this, f1, f2);
    //    printf("ran %p and %p locally\n",f1,f2);
    // run end of sched->exec() starting after f1->exec()
    // run begin of sched->exec(f2) until f2->exec()
    f2->run();
    swap_with_scheduler();
    // run end of sched->exec() starting after f2->exec()
  }

};

template <typename F>
char fjnative<F>::dummy1;
template <typename F>
char fjnative<F>::dummy2;

template <class F>
fjnative<F>* new_fjnative_of_function(const F& f) {
  return new fjnative<F>(f);
}

template <class F1, class F2>
void fork2(const F1& f1, const F2& f2) {
#if defined(MCSL_SEQUENTIAL_ELISION)
  f1(); f2();
#else
  fibers.mine()->fork2(new_fjnative_of_function(f1), new_fjnative_of_function(f2));
#endif
}
  
} // end namespace
