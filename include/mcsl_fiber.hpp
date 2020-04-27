#pragma once

#include "mcsl_chaselev.hpp"

namespace mcsl {
  
/*---------------------------------------------------------------------*/
/* Fibers */

template <typename Scheduler_configuration>
class fiber {
private:

  std::atomic<std::size_t> incounter;

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
      Scheduler_configuration::schedule(this);
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
  
template <typename Scheduler_configuration>
class terminal_fiber : public fiber<Scheduler_configuration> {
public:
  
  terminal_fiber() : fiber<Scheduler_configuration>() { }
  
  fiber_status_type run() {
    return fiber_status_terminate;
  }
  
};
  
} // end namespace
