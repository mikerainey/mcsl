#pragma once

#include <cstdio>
#include <vector>
#include <assert.h>

#ifdef MCSL_HAVE_HWLOC
#include <hwloc.h>
#endif

#ifdef MCSL_NAUTILUS
extern "C"
ulong_t nk_detect_cpu_freq(uint32_t);
#endif

#include "mcsl_perworker.hpp"
#include "mcsl_util.hpp"

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Policies for binding workers to hardware resources */

using pinning_policy_type = enum pinning_policy_enum {
  pinning_policy_enabled,
  pinning_policy_disabled
};
  
using resource_packing_type = enum resource_packing_enum {
  resource_packing_sparse,
  resource_packing_dense
};

using resource_binding_type = enum resource_binding_enum {
  resource_binding_all,
  resource_binding_by_core,
  resource_binding_by_numa_node
};

pinning_policy_type pinning_policy = pinning_policy_disabled;  
  
template <typename Resource_id>
std::vector<Resource_id> assign_workers_to_resources(resource_packing_type packing,
                                                     std::size_t nb_workers,
                                                     std::vector<std::size_t>& max_nb_workers_by_resource,
                                                     const std::vector<Resource_id>& resource_ids) {
  assert(resource_ids.size() == max_nb_workers_by_resource.size());
  std::vector<Resource_id> assignments(nb_workers);
  auto nb_resources = resource_ids.size();
  // we start from resources w/ high ids and work down so as to avoid allocating core 0
  std::size_t resource = nb_resources - 1;
  auto next_resource = [&] {
    resource = (resource == 0) ? nb_resources - 1 : resource - 1;
  };
  for (std::size_t i = 0; i != nb_workers; ++i) {
    while (max_nb_workers_by_resource[resource] == 0) {
      next_resource();
    }
    max_nb_workers_by_resource[resource]--;
    assignments[i] = resource_ids[resource];
    if (packing == resource_packing_sparse) {
      next_resource();
    }
  }
  return assignments;
}

#ifdef MCSL_HAVE_HWLOC

using hwloc_coordinate_type = struct hwloc_coordinate_struct {
  int depth;
  int position;
};

static constexpr
hwloc_coordinate_type empty_coordinate = { .depth = -1, .position = -1 };

perworker::array<hwloc_coordinate_type> worker_hwloc_coordinates;

perworker::array<hwloc_cpuset_t> hwloc_cpusets;
  
hwloc_topology_t topology;
hwloc_cpuset_t all_cpus;

void hwloc_assign_cpusets(std::size_t nb_workers,
                          pinning_policy_type _pinning_policy,
                          resource_packing_type resource_packing,
                          resource_binding_type resource_binding) {
  pinning_policy = _pinning_policy;
  all_cpus = hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
  std::vector<std::size_t> max_nb_workers_by_resource;
  std::vector<hwloc_coordinate_type> resource_ids;
  if (resource_binding == resource_binding_by_core) {
    auto core_depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_CORE);
    std::size_t nb_cores = hwloc_get_nbobjs_by_depth(topology, core_depth);
    assert(nb_workers <= nb_cores);
    max_nb_workers_by_resource.resize(nb_cores);
    resource_ids.resize(nb_cores, empty_coordinate);
    for (int core_id = 0; core_id < nb_cores; core_id++) {
      hwloc_obj_t core = hwloc_get_obj_by_depth(topology, core_depth, core_id);
      max_nb_workers_by_resource[core_id] =
        hwloc_get_nbobjs_inside_cpuset_by_type(topology, core->cpuset, HWLOC_OBJ_CORE);
      resource_ids[core_id] = { .depth = core_depth, .position = core_id };
    }
  } else if (resource_binding == resource_binding_by_numa_node) {
    die("todo");
  } else {
    assert(resource_binding == resource_binding_all);
    for (std::size_t worker_id = 0; worker_id != nb_workers; ++worker_id) {
      hwloc_cpusets[worker_id] = hwloc_bitmap_dup(all_cpus);
    }
    return;
  }
  auto assignments = assign_workers_to_resources(resource_packing,
                                                 nb_workers,
                                                 max_nb_workers_by_resource,
                                                 resource_ids);
  for (std::size_t worker_id = 0; worker_id != nb_workers; ++worker_id) {
    hwloc_cpuset_t cpuset;
    auto assignment = assignments[worker_id];
    if (assignment.depth == -1) {
      cpuset = all_cpus;
    } else {
      cpuset = hwloc_get_obj_by_depth(topology, assignment.depth, assignment.position)->cpuset;
    }
    hwloc_cpusets[worker_id] = hwloc_bitmap_dup(cpuset);
  }
}

void hwloc_pin_calling_worker() {
  if (pinning_policy == pinning_policy_disabled) {
    return;
  }
  auto& cpuset = hwloc_cpusets.mine();
  int flags = HWLOC_CPUBIND_STRICT | HWLOC_CPUBIND_THREAD;
  if (hwloc_set_cpubind(topology, cpuset, flags)) {
    char *str;
    int error = errno;
    hwloc_bitmap_asprintf(&str, cpuset);
    printf("Couldn't bind to cpuset %s: %s\n", str, strerror(error));
    free(str);
  }
}
  
#endif

void assign_cpusets(std::size_t nb_workers,
                    pinning_policy_type _pinning_policy,
                    resource_packing_type resource_packing,
                    resource_binding_type resource_binding) {
#ifdef MCSL_HAVE_HWLOC
  hwloc_assign_cpusets(nb_workers, _pinning_policy, resource_packing, resource_binding);
#endif
}

void pin_calling_worker() {
#ifdef MCSL_HAVE_HWLOC
  hwloc_pin_calling_worker();
#endif
}

void initialize_hwloc(std::size_t nb_workers, bool& numa_alloc_interleaved) {
#ifdef MCSL_HAVE_HWLOC
  hwloc_topology_init(&topology);
  hwloc_topology_load(topology);
  if (numa_alloc_interleaved) {
    hwloc_cpuset_t all_cpus =
      hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
    int err = hwloc_set_membind(topology, all_cpus, HWLOC_MEMBIND_INTERLEAVE, 0);
    if (err < 0) {
      printf("Warning: failed to set NUMA round-robin allocation policy\n");
    } else {
      numa_alloc_interleaved = true;
    }
  }
#else
  numa_alloc_interleaved = false;  
#endif
}

void destroy_hwloc(std::size_t nb_workers) {
#ifdef MCSL_HAVE_HWLOC
  hwloc_bitmap_free(all_cpus);
  for (std::size_t worker_id = 0; worker_id != nb_workers; ++worker_id) {
    hwloc_bitmap_free(hwloc_cpusets[worker_id]);
  }
  hwloc_topology_destroy(topology);
#endif
}
  
/*---------------------------------------------------------------------*/
/* Loader for CPU frequency */

uint64_t load_cpu_frequency_khz() {
  uint64_t cpu_frequency_khz = 0;
#if defined(MCSL_LINUX)
  FILE *f;
  f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", "r");
  if (f == nullptr) {
    f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/bios_limit", "r");
  }
  if (f != nullptr) {
    char buf[1024];
    while (fgets(buf, sizeof(buf), f) != 0) {
      sscanf(buf, "%lu", &(cpu_frequency_khz));
    }
    fclose(f);
  }
#elif defined(MCSL_NAUTILUS)
  cpu_frequency_khz = nk_detect_cpu_freq(0);
#endif
  return cpu_frequency_khz;
}
  
double load_cpu_frequency_ghz() {
  return ((double)load_cpu_frequency_khz()) / 1000.0 / 1000.0;
}

/*---------------------------------------------------------------------*/
/* Setup and teardown */

std::size_t nb_workers = 0;

void initialize_machine() {
#if defined(MCSL_LINUX)
  init_print_lock();
  nb_workers = deepsea::cmdline::parse_or_default_int("proc", 1);
  if (nb_workers > perworker::default_max_nb_workers) {
    die("Requested too many worker threads: %lld, should be maximum %lld\n",
        nb_workers, perworker::default_max_nb_workers);
  }
  perworker::unique_id::initialize(nb_workers);
  // just hint to the OS that we'll be using this many threads
  pthread_setconcurrency((int)nb_workers);
  { // assign the NUMA-allocation policy
    bool numa_alloc_interleaved = deepsea::cmdline::parse_or_default_bool("numa_round_robin", nb_workers != 1);
    mcsl::initialize_hwloc(nb_workers, numa_alloc_interleaved);
  }
  { // assign the CPU-pinning policy
    pinning_policy_type pinning_policy =
      deepsea::cmdline::parse_or_default_bool("pinning_enabled", false) ? pinning_policy_enabled : pinning_policy_disabled;
    resource_packing_type resource_packing = resource_packing_sparse;
    { // resource packing
      std::string s = deepsea::cmdline::parse_or_default_string("resource_packing", "sparse");
      if (s == "sparse") {
        resource_packing = resource_packing_sparse;
      } else if (s == "dense") {
        resource_packing = resource_packing_dense;
      } else {
        die("bogus resource packing\n");
      }
    }
    resource_binding_type resource_binding = resource_binding_all;
    { // resource binding
      std::string s = deepsea::cmdline::parse_or_default_string("resource_binding", "all");
      if (s == "all") {
        resource_binding = resource_binding_all;
      } else if (s == "by_core") {
        resource_binding = resource_binding_by_core;
      } else if (s == "by_numa_node") {
        resource_binding = resource_binding_by_numa_node;
      } else {
        die("bogus resource binding\n");
      }
    }
    assign_cpusets(nb_workers, pinning_policy, resource_packing, resource_binding);
  }
#endif
}

void teardown_machine() {
  destroy_hwloc(nb_workers);
}
  
} // end namespace
