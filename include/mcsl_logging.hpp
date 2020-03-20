#pragma once

#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <memory>

#include "cmdline.hpp"
#include "mcsl_util.hpp"

namespace mcsl {

using event_kind_type = enum event_kind_enum {
  phases = 0,
  fibers,
  migration,
  program,
  nb_kinds
};

using event_tag_type = enum event_tag_type_enum {
  enter_launch = 0,   exit_launch,
  enter_algo,         exit_algo,
  enter_wait,         exit_wait,
  worker_communicate, interrupt,
  algo_phase,
  program_point,
  nb_events
};

static inline
std::string name_of(event_tag_type e) {
  switch (e) {
    case enter_launch:   return "enter_launch ";
    case exit_launch:    return "exit_launch ";
    case enter_algo:     return "enter_algo ";
    case exit_algo:      return "exit_algo ";
    case enter_wait:     return "enter_wait ";
    default:             return "unknown_event ";
  }
}

event_kind_type kind_of(event_tag_type e) {
  switch (e) {
    case enter_launch:
    case exit_launch:
    case enter_algo:
    case exit_algo:
    case enter_wait:
    case exit_wait:
    case algo_phase:                return phases;
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
  } extra;
      
  void print_byte(FILE* f) {
    fwrite_int64(f, (int64_t) timestamp);
    fwrite_int64(f, worker_id);
    fwrite_int64(f, tag);
  }
      
  void print_text(FILE* f) {
    fprintf(f, "%lf\t%ld\t%s\t", timestamp, worker_id, name_of(tag).c_str());
    switch (tag) {
      case program_point: {
        fprintf(f, "%s \t %d \t %p",
                extra.ppt.source_fname,
                extra.ppt.line_nb,
                extra.ppt.ptr);
        break;
      }
      default: {
        // nothing to do
      }
    }
    fprintf (f, "\n");
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
  void initialize(bool _real_time, bool log_phases, bool log_fibers, bool pview) {
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
    bool real_time = deepsea::cmdline::parse_or_default_bool("log_stdout", false);
    bool log_phases = deepsea::cmdline::parse_or_default_bool("log_phases", false);
    bool log_fibers = deepsea::cmdline::parse_or_default_bool("log_fibers", false);
    bool pview = deepsea::cmdline::parse_or_default_bool("pview", false);
    initialize(real_time, log_phases, log_fibers, pview);
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

  static
  void output_bytes(buffer_type& b, bool pview, std::string fname) {
    if (fname == "") {
      return;
    }
    FILE* f = fopen(fname.c_str(), "w");
    for (auto e : b) {
      e.print_byte(f);
    }
    fclose(f);
  }

  static
  void output_bytes(buffer_type& b) {
    bool pview = deepsea::cmdline::parse_or_default_bool("pview", false);
    auto dflt = pview ? "LOG_BIN" : "";
    std::string fname = deepsea::cmdline::parse_or_default_string("log_bytes_fname", dflt);
    output_bytes(b, pview, fname);
  }

  static
  void output_text(buffer_type& b, std::string fname) {

    if (fname == "") {
      return;
    }
    FILE* f = fopen(fname.c_str(), "w");
    for (auto e : b) {
      e.print_text(f);
    }
    fclose(f);
  }

  static
  void output_text(buffer_type& b) {
    std::string fname = deepsea::cmdline::parse_or_default_string("log_text_fname", "");
    output_text(b, fname);
  }

  static
  void output() {
    push(event_type(exit_launch));
    for (auto i = 0; i < nb_ppts; i++) {
      event_type e(program_point);
      e.extra.ppt = ppts[i];
      push(e);
    }
    buffer_type b;
    for (auto id = 0; id != perworker::unique_id::get_nb_workers(); id++) {
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
