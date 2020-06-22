#pragma once

#include <cstdint>
#include <atomic>
#include <stdarg.h>
#include <assert.h>
#include <time.h>

#if defined(MCSL_LINUX)
#include <pthread.h>
#include <chrono>
#elif defined(MCSL_NAUTILUS)
extern "C"
int printk(const char* fmt, ...);
extern "C"
void** nk_get_tid();
typedef long time_t;
typedef int clockid_t;
#define CLOCK_MONOTONIC                 1
extern "C"
int clock_gettime(clockid_t, struct timespec*);
namespace nautilus {
#include <nautilus/spinlock.h>
}
#endif

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Hash function */

using hash_value_type = uint64_t;

inline
hash_value_type hash(hash_value_type u) {
  uint64_t v = u * 3935559000370003845ul + 2691343689449507681ul;
  v ^= v >> 21;
  v ^= v << 37;
  v ^= v >>  4;
  v *= 4768777513237032717ul;
  v ^= v << 20;
  v ^= v >> 41;
  v ^= v <<  5;
  return v;
}

/*---------------------------------------------------------------------*/
/* Cycle counter */

namespace cycles {

namespace {
  
static inline
uint64_t rdtsc() {
  unsigned int hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return  ((uint64_t) lo) | (((uint64_t) hi) << 32);
}

static inline
void rdtsc_wait(uint64_t n) {
  const uint64_t start = rdtsc();
  while (rdtsc() < (start + n)) {
    __asm__("PAUSE");
  }
}
  
} // end namespace
  
static inline
uint64_t diff(uint64_t start, uint64_t finish) {
  return finish - start;
}

static inline
uint64_t now() {
  return rdtsc();
}

static inline
uint64_t since(uint64_t start) {
  return diff(start, now());
}

static inline
void spin_for(uint64_t nb_cycles) {
  rdtsc_wait(nb_cycles);
}
  
} // end namespace

/*---------------------------------------------------------------------*/
/* System clock */

namespace clock {

#if defined(MCSL_LINUX)
  
using time_point_type = std::chrono::time_point<std::chrono::system_clock>;
  
static inline
double diff(time_point_type start, time_point_type finish) {
  std::chrono::duration<double> elapsed = finish - start;
  return elapsed.count();
}

static inline
time_point_type now() {
  return std::chrono::system_clock::now();
}

static inline
double since(time_point_type start) {
  return diff(start, now());
}

#elif defined(MCSL_NAUTILUS)

using time_point_type = struct timespec;
  
static inline
double diff(time_point_type start, time_point_type finish) {
  long seconds = finish.tv_sec - start.tv_sec; 
  long ns = finish.tv_nsec - start.tv_nsec; 
  if (start.tv_nsec > finish.tv_nsec) { // clock underflow 
    --seconds; 
    ns += 1000000000; 
  }
  return (double)seconds + (double)ns/(double)1000000000;
}

static inline
time_point_type now() {
  struct timespec te;
  clock_gettime(CLOCK_MONOTONIC, &te);
  return te;
}

static inline
double since(time_point_type start) {
  return diff(start, now());
}

#endif
  
} // end namespace
  
/*---------------------------------------------------------------------*/
/* Atomic compare-and-exchange operation, with backoff */

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

#if defined(MCSL_LINUX)
  
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

void die(const char *fmt, ...) {
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

void afprintf(FILE* stream, const char *fmt, ...) {
  va_list	ap;
  va_start (ap, fmt);
  acquire_print_lock(); {
    vfprintf (stream, fmt, ap);
    fflush (stream);
  }
  release_print_lock();
  va_end(ap);
}

void aprintf(const char *fmt, ...) {
  va_list	ap;
  va_start (ap, fmt);
  acquire_print_lock(); {
    vfprintf (stdout, fmt, ap);
    fflush (stdout);
  }
  release_print_lock();
  va_end(ap);
}

#elif defined(MCSL_NAUTILUS)

nautilus::spinlock_t print_lock;
  
void init_print_lock() {
  nautilus::spinlock_init(&print_lock);
}

void acquire_print_lock() {
  nautilus::spin_lock(&print_lock);
}

void release_print_lock() {
  nautilus::spin_unlock(&print_lock);
}

#define die(f_, ...) \
  acquire_print_lock();  \
  printk((f_), ##__VA_ARGS__); \
  release_print_lock();  \
  assert(false);

#define aprintf(f_, ...) \
  acquire_print_lock(); \
  printk((f_), ##__VA_ARGS__); \
  release_print_lock();
  
#endif

} // end namespace
