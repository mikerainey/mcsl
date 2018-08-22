#include <atomic>

#include "mcsl_cycles.hpp"

#ifndef _MCSL_ATOMIC_H_
#define _MCSL_ATOMIC_H_

namespace mcsl {
namespace atomic {

namespace {
static constexpr
int backoff_nb_cycles = 1l << 17;
} // end namespace

template <class T>
bool compare_exchange(std::atomic<T>& cell, T& expected, T desired) {
  if (cell.compare_exchange_strong(expected, desired)) {
    return true;
  }
  cycles::spin_for(backoff_nb_cycles);
  return false;
}

} // end namespace
} // end namespace

#endif /*! _MCSL_ATOMIC_H_ */
