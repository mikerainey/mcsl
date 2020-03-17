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
    assert(is_ready());
    auto f = outedge;
    outedge = nullptr;
    if (f != nullptr) {
      f->release();
    }
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

};

} // end namespace
