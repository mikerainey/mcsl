#include <atomic>

#include "mcsl_aligned.hpp"

#ifndef _MCSL_PERWORKER_H_
#define _MCSL_PERWORKER_H_

#ifndef MCSL_MAX_NB_WORKERS_LG
#define MCSL_MAX_NB_WORKERS_LG 7
#endif

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Per-worker, unique id */

class unique_id {
private:

  static constexpr
  int uninitialized_id = -1;

  static
  std::atomic<int> fresh_id;

  static __thread
  int my_id;

public:

  static
  int get_my_id() {
    if (my_id == uninitialized_id) {
      my_id = fresh_id++;
    }
    return my_id;
  }

  static
  int get_nb_workers() {
    return fresh_id.load();
  }

};

std::atomic<int> unique_id::fresh_id(0);
  
__thread int unique_id::my_id = uninitialized_id;

static constexpr
int default_max_nb_workers_lg = MCSL_MAX_NB_WORKERS_LG;

static constexpr
int default_max_nb_workers = 1 << default_max_nb_workers_lg;

} // end namespace

#undef MCSL_MAX_NB_WORKERS_LG

#endif /*! _MCSL_PERWORKER_H_ */
