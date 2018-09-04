#include <cstdio>

#ifdef MCSL_TARGET_MAC_OS
#include <sys/param.h>
#include <sys/sysctl.h>
#endif

#include "mcsl_atomic.hpp"

#ifndef _MCSL_MACHINE_H_
#define _MCSL_MACHINE_H_

namespace mcsl {

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
