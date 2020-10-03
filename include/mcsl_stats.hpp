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

  using timestamp_type = uint64_t;

  static inline
  timestamp_type now() {
    return cycles::now();
  }

  static inline
  timestamp_type since(timestamp_type start) {
    return cycles::since(start);
  }

  using private_counters = struct {
    long counters[Configuration::nb_counters];
  };

  static
  perworker::array<private_counters> all_counters;

  static
  timestamp_type enter_launch_time;
  
  static
  timestamp_type launch_duration;

  using private_timers = struct private_timers_struct {
    timestamp_type start_work;
    timestamp_type total_work_time;
    timestamp_type start_idle;
    timestamp_type total_idle_time;
  };

  static
  perworker::array<private_timers> all_timers;
  
public:

  static inline
  void increment(counter_id_type id) {
    if (! Configuration::enabled) {
      return;
    }
    all_counters.mine().counters[id]++;
  }

  static
  void on_enter_acquire() {
    if (! Configuration::enabled) {
      return;
    }
    all_timers.mine().start_idle = now();
  }
  
  static
  void on_exit_acquire() {
    if (! Configuration::enabled) {
      return;
    }
    auto& t = all_timers.mine();
    t.total_idle_time += since(t.start_idle);
  }

  static
  void on_enter_work() {
    if (! Configuration::enabled) {
      return;
    }
    all_timers.mine().start_work = now();
  }
  
  static
  void on_exit_work() {
    if (! Configuration::enabled) {
      return;
    }
    auto& t = all_timers.mine();
    t.total_work_time += since(t.start_work);
  }

  static
  void start_collecting() {
    enter_launch_time = now();
    for (int i = 0; i < all_counters.size(); i++) {
      for (int j = 0; j < Configuration::nb_counters; j++) {
        all_counters[i].counters[j] = 0;
      }
    }
    for (int i = 0; i < all_timers.size(); i++) {
      auto& t = all_timers[i];
      t.start_work = now();
      t.total_work_time = 0;
      t.start_idle = now();
      t.total_idle_time = 0;
    }
  }

  static
  void report(std::size_t nb_workers) {
    if (! Configuration::enabled) {
      return;
    }
    launch_duration = since(enter_launch_time);
    for (int counter_id = 0; counter_id < Configuration::nb_counters; counter_id++) {
      long counter_value = 0;
      for (std::size_t i = 0; i < nb_workers; ++i) {
        counter_value += all_counters[i].counters[counter_id];
      }
      const char* counter_name = Configuration::name_of_counter((counter_id_type)counter_id);
      aprintf("%s %ld\n", counter_name, counter_value);
    }
    aprintf("launch_duration %lu\n", launch_duration);
    timestamp_type cumulated_time = launch_duration * nb_workers;
    timestamp_type total_work_time = 0;
    timestamp_type total_idle_time = 0;
    auto my_id = perworker::unique_id::get_my_id();
    for (std::size_t i = 0; i < nb_workers; ++i) {
      auto& t = all_timers[i];
      if (i == my_id) {
        t.total_work_time += since(t.start_work);
      }
      total_work_time += t.total_work_time;
      if (i != my_id) {
        t.total_idle_time += since(t.start_idle);
      }
      total_idle_time += t.total_idle_time;
    }
    double relative_idle = (double)total_idle_time / (double)cumulated_time;
    double utilization = 1.0 - relative_idle;
    aprintf("total_work_time %lu\n", total_work_time);
    aprintf("total_idle_time %lu\n", total_idle_time);
    aprintf("total_time %lu\n", cumulated_time);
    aprintf("utilization %.3f\n", utilization);
  }

};

template <typename Configuration>
perworker::array<typename stats_base<Configuration>::private_counters> stats_base<Configuration>::all_counters;

template <typename Configuration>
typename stats_base<Configuration>::timestamp_type stats_base<Configuration>::enter_launch_time;

template <typename Configuration>
typename stats_base<Configuration>::timestamp_type stats_base<Configuration>::launch_duration;

template <typename Configuration>
perworker::array<typename stats_base<Configuration>::private_timers> stats_base<Configuration>::all_timers;

} // end namespace
