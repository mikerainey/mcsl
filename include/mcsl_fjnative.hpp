#pragma once

#include "mcsl_fiber.hpp"

using _context_pointer = char*;

extern "C"
void* _mcsl_cxt_save(_context_pointer cxt);

extern "C"
void _mcsl_cxt_restore(_context_pointer cxt, void* t);

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
  void throw_to(context_pointer cxt, Value val) {
    _mcsl_cxt_restore(cxt, (void*)val);
  }
  
  template <class Value>
  static
  void swap(context_pointer cxt1, context_pointer cxt2, Value val2) {
    if (_mcsl_cxt_save(cxt1)) {
      return;
    }
    _mcsl_cxt_restore(cxt2, val2);
  }
  
  // register number 6
#define _X86_64_SP_OFFSET   6
  
  template <class Value>
  static
  Value capture(context_pointer cxt) {
    void* r = _mcsl_cxt_save(cxt);
    return (Value)r;
  }
  
  template <class Value>
  static
  char* spawn(context_pointer cxt, Value val) {
    Value target;
    if (target = (Value)_mcsl_cxt_save(cxt)) {
      target->enter(target);
      assert(false);
    }
    char* stack = (char*)malloc(thread_stack_szb);
    void** _cxt = (void**)cxt;
    _cxt[_X86_64_SP_OFFSET] = &stack[thread_stack_szb];
    return stack;
  }
  
};

class context_wrapper_type {
public:
  util::control::context::context_type cxt;
};

static
perworker::array<context_wrapper_type> cxts;

static
context::context_pointer my_cxt() {
  return context::addr(cxts.mine().cxt);
}

template <typename Scheduler_config>
class fjnative : public fiber<Scheduler_config> {
public:

  using context_type = ucxt::context::context_type;
  using context = ucxt::context;

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

  // pointer to the call stack of this thread
  char* stack;
  // CPU context of this thread
  context_type cxt;

  void swap_with_scheduler() {
    context::swap(context::addr(cxt), my_cxt(), notaptr);
  }

  static void exit_to_scheduler() {
    context::throw_to(my_cxt(), notaptr);
  }

  void prepare() {
    threaddag::reuse_calling_thread();
  }

  void prepare_and_swap_with_scheduler() {
    prepare();
    swap_with_scheduler();
  }

  // point of entry from the scheduler to the body of this thread
  // the scheduler may reenter this fiber via this method
  void exec() {
    if (stack == nullptr) {
      // initial entry by the scheduler into the body of this thread
      stack = context::spawn(context::addr(cxt), this);
    }
    // jump into body of this thread
    context::swap(my_cxt(), context::addr(cxt), this);
  }

  // point of entry to this thread to be called by the `context::spawn` routine
  static void enter(fjnative* t) {
    assert(t != nullptr);
    assert(t != (fjnative*)notaptr);
    t->run();
    // terminate thread by exiting to scheduler
    exit_to_scheduler();
  }

public:

  fjnative()
  : thread(), stack(nullptr)  { }

  ~fjnative() {
    if (stack == nullptr) {
      return;
    }
    if (stack == notownstackptr) {
      return;
    }
    free(stack);
    stack = nullptr;
  }

  void fork2(fjnative* f0, fjnative* f1) {
    prepare();
    threaddag::binary_fork_join(thread0, thread1, this);
    if (context::capture<fjnative*>(context::addr(cxt))) {
      //      util::atomic::aprintf("steal happened: executing join continuation\n");
      return;
    }
    // know f0 stays on my stack
    f0->stack = notownstackptr;
    f0->swap_with_scheduler();
    // sched is popping f0
    // run begin of sched->exec(f0) until f0->exec()
    f0->run();
    // if f1 was not stolen, then it can run in the same stack as parent
    if (! sched->local_has() || sched->local_peek() != f1) {
      //      util::atomic::aprintf("%d %d detected steal of %p\n",id,util::worker::get_my_id(),f1);
      exit_to_scheduler();
      return; // unreachable
    }
    //    util::atomic::aprintf("%d %d ran %p; going to run f %p\n",id,util::worker::get_my_id(),f0,f1);
    // prepare f1 for local run
    assert(f1->stack == nullptr);
    f1->stack = notownstackptr;
    f1->swap_with_scheduler();
    //    util::atomic::aprintf("%d %d this=%p f0=%p f1=%p\n",id,util::worker::get_my_id(),this, f0, f1);
    //    printf("ran %p and %p locally\n",f0,f1);
    // run end of sched->exec() starting after f0->exec()
    // run begin of sched->exec(f1) until f1->exec()
    f1->run();
    swap_with_scheduler();
    // run end of sched->exec() starting after f1->exec()
  }

};

char fjnative::dummy1;
char fjnative::dummy2;


} // end namespace
