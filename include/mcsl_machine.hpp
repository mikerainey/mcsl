#include <cstdio>
#include <vector>
#include <assert.h>

#ifdef MCSL_TARGET_MAC_OS
#include <sys/param.h>
#include <sys/sysctl.h>
#endif

#ifdef MCSL_HAVE_HWLOC
#include <hwloc.h>
#endif

#include "mcsl_atomic.hpp"
#include "mcsl_perworker.hpp"

#ifndef _MCSL_MACHINE_H_
#define _MCSL_MACHINE_H_

namespace mcsl {

/*---------------------------------------------------------------------*/
/* Policies for binding workers to hardware resources */
  
using resource_packing_type = enum resource_packing_enum {
  resource_packing_sparse,
  resource_packing_dense
};

using resource_binding_type = enum resource_binding_enum {
  resource_binding_all,
  resource_binding_by_core,
  resource_binding_by_numa_node
};
  
template <typename Resource_id>
std::vector<Resource_id> assign_workers_to_resources(resource_packing_type packing,
                                                     std::size_t nb_workers,
                                                     std::vector<std::size_t>& max_nb_workers_by_resource,
                                                     const std::vector<Resource_id>& resource_ids) {
  assert(resource_ids.size() == max_nb_workers_by_resource.size());
  std::vector<Resource_id> assignments(nb_workers);
  auto nb_resources = resource_ids.size();
  std::size_t resource = 0;
  for (std::size_t i = 0; i != nb_workers; ++i) {
    while (max_nb_workers_by_resource[resource] == 0) {
      // advance resource to the next nonempty resource cell
      resource = (resource + 1) % nb_resources;
    }
    max_nb_workers_by_resource[resource]--;
    assignments[i] = resource_ids[resource];
    if (packing == resource_packing_sparse) {
      resource = (resource + 1) % nb_resources;
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

void hwloc_assign_cpusets(std::size_t nb_workers,
                          resource_packing_type resource_packing,
                          resource_binding_type resource_binding) {
  hwloc_cpuset_t all_cpus =
    hwloc_bitmap_dup (hwloc_topology_get_topology_cpuset(topology));
  
  std::vector<std::size_t> max_nb_workers_by_resource;
  std::vector<hwloc_coordinate_type> resource_ids;
  if (resource_binding == resource_binding_by_core) {
    auto core_depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_CORE);
    std::size_t nb_cores = hwloc_get_nbobjs_by_depth(topology, core_depth);
    max_nb_workers_by_resource.resize(nb_cores);
    resource_ids.resize(nb_cores, empty_coordinate);
    for (int core_id = 0; core_id < nb_cores; core_id++) {
      hwloc_obj_t core = hwloc_get_obj_by_depth(topology, core_depth, core_id);
      max_nb_workers_by_resource[core_id] =
        hwloc_get_nbobjs_inside_cpuset_by_type(topology, core->cpuset, HWLOC_OBJ_CORE);
      resource_ids[core_id] = { .depth = core_depth, .position = core_id };
    }
  } else if (resource_binding == resource_binding_by_numa_node) {
    atomic::die("todo");
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
                    resource_packing_type resource_packing,
                    resource_binding_type resource_binding) {
#ifdef MCSL_HAVE_HWLOC
  hwloc_assign_cpusets(nb_workers, resource_packing, resource_binding);
#endif
}

void pin_calling_worker() {
#ifdef MCSL_HAVE_HWLOC
  hwloc_pin_calling_worker();
#endif
}

void initialize_hwloc(std::size_t nb_workers, bool& numa_alloc_interleaved) {
#ifdef MCSL_HAVE_HWLOC
  hwloc_topology_init (&topology);
  hwloc_topology_load (topology);
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

void destroy_hwloc() {
#ifdef MCSL_HAVE_HWLOC
  hwloc_topology_destroy(topology);
#endif
}
  
/*---------------------------------------------------------------------*/
/* Loader for CPU frequency */
  
double load_cpu_frequency_ghz() {
  double cpu_frequency_ghz = 0.0;
  float cpu_frequency_mhz = 0.0;
#ifdef MCSL_TARGET_LINUX
  /* Get information from /proc/cpuinfo.     *
   * cpu MHz         : <float>             # cpu frequency in MHz
   */
  FILE *cpuinfo_file = fopen("/proc/cpuinfo", "r");
  char buf[1024];
  int cache_line_szb;
  if (cpuinfo_file != nullptr) {
    while (fgets(buf, sizeof(buf), cpuinfo_file) != 0) {
      sscanf(buf, "cpu MHz : %f", &(cpu_frequency_mhz));
    }
    fclose (cpuinfo_file);
  }
#endif
#ifdef MCSL_TARGET_MAC_OS
  uint64_t freq = 0;
  size_t size;
  size = sizeof(freq);
  if (sysctlbyname("hw.cpufrequency", &freq, &size, nullptr, 0) < 0) {
    perror("sysctl");
  }
  cpu_frequency_mhz = (float)freq / 1000000.;
#endif
  if (cpu_frequency_mhz == 0.) {
    atomic::die("Failed to read CPU frequency\n");
  }
  cpu_frequency_ghz = (double) (cpu_frequency_mhz / 1000.0);
  return cpu_frequency_ghz;
}

} // end namespace

#endif /*! _MCSL_MACHINE_H_ */
