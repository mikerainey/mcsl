#include <atomic>
#include <stdarg.h>
#include <pthread.h>

#include "mcsl_cycles.hpp"

#ifndef _MCSL_ATOMIC_H_
#define _MCSL_ATOMIC_H_

namespace mcsl {
namespace atomic {

template <class T>
bool compare_exchange(std::atomic<T>& cell, T& expected, T desired) {
  static constexpr
  int backoff_nb_cycles = 1l << 12;
  if (cell.compare_exchange_strong(expected, desired)) {
    return true;
  }
  cycles::spin_for(backoff_nb_cycles);
  return false;
}

/*---------------------------------------------------------------------*/
/* Atomic printing routines */
  
pthread_mutex_t print_lock;
  
void init_print_lock() {
  pthread_mutex_init(&print_lock, nullptr);
}

void acquire_print_lock() {
  pthread_mutex_lock (&print_lock);
}

void release_print_lock() {
  pthread_mutex_unlock (&print_lock);
}

void die (const char *fmt, ...) {
  va_list	ap;
  va_start (ap, fmt);
  acquire_print_lock(); {
    fprintf (stderr, "Fatal error -- ");
    vfprintf (stderr, fmt, ap);
    fprintf (stderr, "\n");
    fflush (stderr);
  }
  release_print_lock();
  va_end(ap);
  assert(false);
  exit (-1);
}

void afprintf (FILE* stream, const char *fmt, ...) {
  va_list	ap;
  va_start (ap, fmt);
  acquire_print_lock(); {
    vfprintf (stream, fmt, ap);
    fflush (stream);
  }
  release_print_lock();
  va_end(ap);
}

void aprintf (const char *fmt, ...) {
  va_list	ap;
  va_start (ap, fmt);
  acquire_print_lock(); {
    vfprintf (stdout, fmt, ap);
    fflush (stdout);
  }
  release_print_lock();
  va_end(ap);
}

} // end namespace
} // end namespace

#endif /*! _MCSL_ATOMIC_H_ */
