#pragma once

#include <atomic>
#include <semaphore.h>

namespace mcsl{

// Status word. 64-bits wide
union status_word {
    uint64_t asUint64; // The order of fields is significant 
                       // Always initializes the first member
    struct {
      uint8_t  busybit  : 1 ;
      uint64_t priority : 56;
      uint8_t  head     : 7 ;  // Supports at most 128 processors
    } bits; 
};

  // A status word that can be operated on atomically
  // 1) clear() will always success in bounded number of steps.
  // 2) setBusyBit() uses atomic fetch_and_AND. It is guaranteed to
  //    succeed in bounded number of steps.
  // 3) updateHead() may fail. It's upto the caller to verify that the
  //    operations succeeded. This is to ensure that the operation completes
  //    in bounded number of steps.
class AtomicStatusWord {
    std::atomic<uint64_t> statusWord;

public:
    // Since no processor can be a child of itself, the thread_id of the 
    // processor itself can be used as the nullary value of the head

    AtomicStatusWord() : statusWord(UINT64_C(0)) {}

    AtomicStatusWord(uint64_t prio, uint8_t nullaryHead) {
      clear(prio, nullaryHead);
    }

    // 1) Unsets the busy bit
    // 2) Hashes and obtain a new priority
    // 3) Resets the head value
    void clear(uint64_t prio, uint8_t nullaryHead) {
      status_word word = {UINT64_C(0)};
      word.bits.busybit  = 0u;   // Not busy
      word.bits.priority = prio; 
      word.bits.head     = nullaryHead;
      statusWord.store(word.asUint64);
    }

    // Sets busy bit and returns the old status word
    status_word setBusyBit() {
      status_word word = {UINT64_C(0)};
      word.bits.busybit = 1u; // I'm going to be busy
      word = {statusWord.fetch_or(word.asUint64)};
      return word;
    }

    // Update the head field while preserving all other fields
    bool casHead(status_word word, uint8_t newHead) {
      uint64_t expected = word.asUint64;
      word.bits.head = newHead; // Update only the head field
      return statusWord.compare_exchange_weak(expected, word.asUint64);
    }

    status_word load() {
      return status_word{statusWord.load()};
    }
};

// Grouping fields for elastic scheduling together for potentiallly
// better cache behavior and easier initialization.
struct ElasticSchedFields {
    AtomicStatusWord     status;
    sem_t                sem;     
    size_t               next;    // Next pointer for the wake-up list

    ElasticSchedFields() {}
};

} // namespace mcsl