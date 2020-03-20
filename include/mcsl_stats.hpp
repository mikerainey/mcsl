#pragma once

#include "mcsl_util.hpp"
#include "mcsl_perworker.hpp"

namespace mcsl {

template <typename Configuration>
class stats_base {
public:

  using counter_id_type = typename Configuration::counter_id_type;
  
  using configuration_type = Configuration;
  
private:

  using private_counters = struct {
    long counters[Configuration::nb_counters];
  };

  static
  perworker::array<private_counters> all_counters;

  static
  clock::time_point_type enter_launch_time;
  
  static
  double launch_duration;
  
  static
  perworker::array<double> all_total_idle_time;
  
public:

  static inline
  void increment(counter_id_type id) {
    if (! Configuration::enabled) {
      return;
    }
    all_counters.mine().counters[id]++;
  }

  static
  void on_enter_launch() {
    enter_launch_time = clock::now();
  }
  
  static
  void on_exit_launch() {
    launch_duration = clock::since(enter_launch_time);
  }

  static
  clock::time_point_type on_enter_acquire() {
    if (! Configuration::enabled) {
      return clock::time_point_type();
    }
    return clock::now();
  }
  
  static
  void on_exit_acquire(clock::time_point_type enter_acquire_time) {
    if (! Configuration::enabled) {
      return;
    }
    all_total_idle_time.mine() += clock::since(enter_acquire_time);
  }

  static
  void report() {
    if (! Configuration::enabled) {
      return;
    }
    auto nb_workers = perworker::unique_id::get_nb_workers();
    for (int counter_id = 0; counter_id < Configuration::nb_counters; counter_id++) {
      long counter_value = 0;
      for (std::size_t i = 0; i < nb_workers; ++i) {
        counter_value += all_counters[i].counters[counter_id];
      }
      const char* counter_name = Configuration::name_of_counter((counter_id_type)counter_id);
      aprintf("%s %ld\n", counter_name, counter_value);
    }
    aprintf("launch_duration %f\n", launch_duration);
    double cumulated_time = launch_duration * nb_workers;
    double total_idle_time = 0.0;
    for (std::size_t i = 0; i < nb_workers; ++i) {
      total_idle_time += all_total_idle_time[i];
    }
    double relative_idle = total_idle_time / cumulated_time;
    double utilization = 1.0 - relative_idle;
    aprintf("total_idle_time %f\n", total_idle_time);
    aprintf("utilization %f\n", utilization);
  }

};

template <typename Configuration>
perworker::array<typename stats_base<Configuration>::private_counters> stats_base<Configuration>::all_counters;

template <typename Configuration>
clock::time_point_type stats_base<Configuration>::enter_launch_time;

template <typename Configuration>
double stats_base<Configuration>::launch_duration;

template <typename Configuration>
perworker::array<double> stats_base<Configuration>::all_total_idle_time;

} // end namespace
