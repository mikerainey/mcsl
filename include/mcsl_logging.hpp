#pragma once

#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <memory>

#if defined(MCSL_LINUX)
#include "cmdline.hpp"
#endif

#include "mcsl_util.hpp"
#include "mcsl_scheduler.hpp"

namespace mcsl {

// logging events defined in mcsl_scheduler.hpp

static inline
std::string name_of(event_tag_type e) {
  switch (e) {
    case enter_launch:      return "enter_launch ";
    case exit_launch:       return "exit_launch ";
    case enter_algo:        return "enter_algo ";
    case exit_algo:         return "exit_algo ";
    case enter_wait:        return "enter_wait ";
    case exit_wait:         return "exit_wait ";
    case enter_sleep:       return "enter_sleep ";
    case failed_to_sleep:   return "failed_to_sleep ";
    case exit_sleep:        return "exit_sleep ";
    case wake_child:        return "wake_child ";
    case worker_exit:       return "worker_exit ";
    case initiate_teardown: return "initiate_teardown";
    case algo_phase:        return "algo_phase ";
    default:                return "unknown_event ";
  }
}

event_kind_type kind_of(event_tag_type e) {
  switch (e) {
    // important: the following kind assignments cannot change due to compatibility w/ pview
    case enter_launch:
    case exit_launch:
    case enter_algo:
    case exit_algo:
    case enter_wait:
    case exit_wait:                return phases;
    // all of the remaining kind assignments can change
    case enter_sleep:
    case failed_to_sleep:
    case exit_sleep:
    case wake_child:
    case algo_phase:                
    case worker_exit:
    case initiate_teardown:
    case program_point:             return program;
    default:                        return nb_kinds;
  }
}

static inline
void fwrite_double (FILE* f, double v) {
  fwrite(&v, sizeof(v), 1, f);
}

static inline
void fwrite_int64 (FILE* f, int64_t v) {
  fwrite(&v, sizeof(v), 1, f);
}

using program_point_type = struct program_point_struct {
  
  int line_nb;
  
  const char* source_fname;

  void* ptr;
      
};

static constexpr
program_point_type dflt_ppt = { .line_nb = -1, .source_fname = nullptr, .ptr = nullptr };

class event_type {
public:
  
  double timestamp;
  
  event_tag_type tag;
  
  size_t worker_id;
  
  event_type() { }
  
  event_type(event_tag_type tag)
  : tag(tag) { }
  
  union extra_union {
    program_point_type ppt;
    size_t child_id;
    struct enter_sleep_struct {
      size_t parent_id;
      size_t prio_child;
      size_t prio_parent;
    } enter_sleep;
    struct failed_to_sleep_struct {
      size_t parent_id;
      size_t busy_child;
      size_t prio_child;
      size_t prio_parent;
    } failed_to_sleep;
  } extra;
      
  void print_byte(FILE* f) {
    fwrite_int64(f, (int64_t) timestamp);
    fwrite_int64(f, worker_id);
    fwrite_int64(f, tag);
  }
      
  void print_text(FILE* f) {
#if defined(MCSL_LINUX)
    fprintf(f, "%lf\t%ld\t%s\t", timestamp, worker_id, name_of(tag).c_str());
    switch (tag) {
      case program_point: {
        fprintf(f, "%s \t %d \t %p",
                extra.ppt.source_fname,
                extra.ppt.line_nb,
                extra.ppt.ptr);
        break;
      }
      case wake_child: {
        fprintf(f, "%ld", extra.child_id);
        break;
      }
      case enter_sleep: {
        fprintf(f, "%ld \t %ld \t %ld",
                extra.enter_sleep.parent_id,
                extra.enter_sleep.prio_child,
                extra.enter_sleep.prio_parent);
        break;
      }
      case failed_to_sleep: {
        fprintf(f, "%ld \t Busy[%ld] \t %ld \t %ld",
                extra.failed_to_sleep.parent_id,
                extra.failed_to_sleep.busy_child,
                extra.failed_to_sleep.prio_child,
                extra.failed_to_sleep.prio_parent);
        break;
      }
      default: {
        // nothing to do
      }
    }
    fprintf (f, "\n");
#elif defined(MCSL_NAUTILUS)
    // later
#endif
  }
    
};

/*---------------------------------------------------------------------*/
/* Log buffer */
  
using buffer_type = std::vector<event_type>;
  
static constexpr
int max_nb_ppts = 50000;

template <bool enabled>
class logging_base {
public:
  
  static
  bool real_time;
  
  static
  perworker::array<buffer_type> buffers;
  
  static
  bool tracking_kind[nb_kinds];
  
  static
  clock::time_point_type basetime;

  static
  program_point_type ppts[max_nb_ppts];

  static
  int nb_ppts;
  
  static
  void _initialize(bool _real_time=false, bool log_phases=false, bool log_fibers=false, bool pview=false) {
    if (! enabled) {
      return;
    }
    real_time = _real_time;
    tracking_kind[phases] = log_phases;
    tracking_kind[fibers] = log_fibers;
    if (pview) {
      tracking_kind[phases] = true;
    }
    basetime = clock::now();
    push(event_type(enter_launch));
  }

  static
  void initialize() {
#if defined(MCSL_LINUX)
    bool real_time  = deepsea::cmdline::parse_or_default_bool("log_stdout", false);
    bool log_phases = deepsea::cmdline::parse_or_default_bool("log_phases", false);
    bool log_fibers = deepsea::cmdline::parse_or_default_bool("log_fibers", false);
    bool pview      = deepsea::cmdline::parse_or_default_bool("pview", false);
    _initialize(real_time, log_phases, log_fibers, pview);
#elif defined(MCSL_NAUTILUS)
    _initialize();
#endif
  }
  
  static inline
  void push(event_type e) {
    if (! enabled) {
      return;
    }
    auto k = kind_of(e.tag);
    assert(k != nb_kinds);
    if (! tracking_kind[k]) {
      return;
    }
    e.timestamp = clock::since(basetime) * 1000000;
    e.worker_id = perworker::unique_id::get_my_id();
    if (real_time) {
      acquire_print_lock();
      e.print_text(stdout);
      release_print_lock();
    }
    buffers.mine().push_back(e);
  }

  static inline
  void log_event(event_tag_type tag) {
    push(event_type(tag));
  }

  static inline
  void log_wake_child(size_t child_id) {
    event_type e(wake_child);
    e.extra.child_id = child_id;
    push(e);
  }

  static inline
  void log_enter_sleep(size_t parent_id, size_t prio_child, size_t prio_parent) {
    event_type e(enter_sleep);
    e.extra.enter_sleep.parent_id = parent_id;
    e.extra.enter_sleep.prio_child = prio_child;
    e.extra.enter_sleep.prio_parent = prio_parent;
    push(e);
  }

  static inline
  void log_failed_to_sleep(size_t parent_id, size_t busybit, size_t prio_child, size_t prio_parent) {
    event_type e(failed_to_sleep);
    e.extra.failed_to_sleep.parent_id = parent_id;
    e.extra.failed_to_sleep.busy_child = busybit;
    e.extra.failed_to_sleep.prio_child = prio_child;
    e.extra.failed_to_sleep.prio_parent = prio_parent;
    push(e);
  }

  static constexpr
  const char* dflt_log_bytes_fname = "LOG_BIN";

  static
  void _output_bytes(buffer_type& b, bool pview=false, std::string fname=dflt_log_bytes_fname) {
#if defined(MCSL_LINUX)
    if (fname == "") {
      return;
    }
    FILE* f = fopen(fname.c_str(), "w");
    for (auto e : b) {
      e.print_byte(f);
    }
    fclose(f);
#elif defined(MCSL_NAUTILUS)
    // later
#endif
  }

  static
  void output_bytes(buffer_type& b) {
#if defined(MCSL_LINUX)
    bool pview = deepsea::cmdline::parse_or_default_bool("pview", false);
    auto dflt = pview ? dflt_log_bytes_fname : "";
    std::string fname = deepsea::cmdline::parse_or_default_string("log_bytes_fname", dflt);
    _output_bytes(b, pview, fname);
#elif defined(MCSL_NAUTILUS)
    _output_bytes(b);
#endif
  }

  static
  void _output_text(buffer_type& b, std::string fname="") {
#if defined(MCSL_LINUX)
    if (fname == "") {
      return;
    }
    FILE* f = fopen(fname.c_str(), "w");
    for (auto e : b) {
      e.print_text(f);
    }
    fclose(f);
#elif defined(MCSL_NAUTILUS)
    // later
#endif
  }

  static
  void output_text(buffer_type& b) {
#if defined(MCSL_LINUX)
    std::string fname = deepsea::cmdline::parse_or_default_string("log_text_fname", "");
    _output_text(b, fname);
#elif defined(MCSL_NAUTILUS)
    _output_text(b);
#endif
  }

  static
  void output(std::size_t nb_workers) {
    if (! enabled) {
      return;
    }
    push(event_type(exit_launch));
    for (auto i = 0; i < nb_ppts; i++) {
      event_type e(program_point);
      e.extra.ppt = ppts[i];
      push(e);
    }
    buffer_type b;
    for (auto id = 0; id != nb_workers; id++) {
      buffer_type& b_id = buffers[id];
      for (auto e : b_id) {
        b.push_back(e);
      }
    }
    std::stable_sort(b.begin(), b.end(), [] (const event_type& e1, const event_type& e2) {
      return e1.timestamp < e2.timestamp;
    });
    output_bytes(b);
    output_text(b);
  }
  
};

template <bool enabled>
perworker::array<buffer_type> logging_base<enabled>::buffers;

template <bool enabled>
bool logging_base<enabled>::tracking_kind[nb_kinds];

template <bool enabled>
bool logging_base<enabled>::real_time;

template <bool enabled>
int logging_base<enabled>::nb_ppts = 0;

template <bool enabled>
program_point_type logging_base<enabled>::ppts[max_nb_ppts];

template <bool enabled>
clock::time_point_type logging_base<enabled>::basetime;
  
  /*
static inline
void log_program_point(int line_nb,
                        const char* source_fname,
                        void* ptr) {
  if ((line_nb == -1) || (log_buffer::nb_ppts >= max_nb_ppts)) {
    return;
  }
  program_point_type ppt;
  ppt.line_nb = line_nb;
  ppt.source_fname = source_fname;
  ppt.ptr = ptr;
  log_buffer::ppts[log_buffer::nb_ppts++] = ppt;
}
  */  
} // end namespace
