#pragma once

#include <assert.h>
#include <atomic>

#include "mcsl_scheduler.hpp"
#include "mcsl_aligned.hpp"

namespace mcsl {
  
/*---------------------------------------------------------------------*/
/* Fibers */

template <typename Scheduler>
class fiber {
private:

  alignas(MCSL_CACHE_LINE_SZB)
  std::atomic<std::size_t> incounter;

  alignas(MCSL_CACHE_LINE_SZB)
  fiber* outedge;

public:

  fiber() : incounter(1), outedge(nullptr) { }

  ~fiber() {
    assert(outedge == nullptr);
  }

  virtual
  fiber_status_type run() = 0;

  virtual
  fiber_status_type exec() {
    return run();
  }

  bool is_ready() {
    return incounter.load() == 0;
  }

  void release() {
    if (--incounter == 0) {
      Scheduler::schedule(this);
    }
  }

  fiber* capture_continuation() {
    auto oe = outedge;
    outedge = nullptr;
    return oe;
  }

  static
  void add_edge(fiber* src, fiber* dst) {
    assert(src->outedge == nullptr);
    src->outedge = dst;
    dst->incounter++;
  }

  virtual
  void notify() {
    assert(is_ready());
    auto fo = outedge;
    outedge = nullptr;
    if (fo != nullptr) {
      fo->release();
    }
  }

  virtual
  void finish() {
    notify();
    delete this;
  }

};

/* A fiber that, when executed, initiates the teardown of the 
 * scheduler. 
 */
  
template <typename Scheduler>
class terminal_fiber : public fiber<Scheduler> {
public:
  
  terminal_fiber() : fiber<Scheduler>() { }
  
  fiber_status_type run() {
    return fiber_status_terminate;
  }
  
};
  
} // end namespace
