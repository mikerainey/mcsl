#include <random>
#include <assert.h>

#include "mcsl_perworker.hpp"

#ifndef _MCSL_RANDOM_H_
#define _MCSL_RANDOM_H_

namespace mcsl {
namespace random {

template <typename Rngs, typename Distribution=std::uniform_int_distribution<std::size_t>>
std::size_t other_worker(std::size_t my_id, Rngs& random_number_generators) {
  auto nb_workers = perworker::unique_id::get_nb_workers();
  assert(nb_workers != 1);
  Distribution distribution(0, nb_workers - 2);
  auto i = distribution(random_number_generators[my_id]);
  if (i >= my_id) {
    i++;
  }
  assert(i >= 0);
  assert(i < nb_workers);
  assert(i != my_id);
  return i;  
}
  
} // end namespace
} // end namespace

#endif /*! _MCSL_RANDOM_H_ */
