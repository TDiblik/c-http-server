#ifndef SYS_H_
#define SYS_H_

// ============================
// |        DEFINITIONS       |
// ============================
#include <stdint.h>
#include <stdbool.h>

int sys_get_cpu_stats(float* user_usage_percentage, float* system_usage_percentage, float* idle_usage_percentage, float* nice_usage_percentage, float* total_usage_percentage);
int sys_get_mem_stats(uint64_t* total, uint64_t* used, uint64_t* app, uint64_t* wired, uint64_t* compressed, uint64_t* cached, uint64_t* swap_used);
int sys_get_network_stats(double* rx_bps, double* tx_bps);
int sys_get_disk_stats(uint64_t* total_bytes, uint64_t* free_bytes, float* percentage_used);
int sys_get_battery_stats(int* percent, bool* is_charging);
int sys_get_uptime(long* uptime);

// ================================
// |        IMPLEMENTATION        |
// ================================
#ifdef SYS_IMPLEMENTATION
#include <pthread.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

// inspiration taken from: https://www.green-coding.io/blog/cpu-utilization-mac/
int sys_get_cpu_stats(float* user_usage_percentage, float* system_usage_percentage, float* idle_usage_percentage, float* nice_usage_percentage, float* total_usage_percentage) {
  static host_cpu_load_info_data_t prev_load = {0};
  static bool has_prev = false;
  static pthread_mutex_t cpu_mutex = PTHREAD_MUTEX_INITIALIZER;

  host_cpu_load_info_data_t cpu_load;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

  mach_port_t host_port = mach_host_self();
  kern_return_t kr = host_statistics(host_port, HOST_CPU_LOAD_INFO, (host_info_t)&cpu_load, &count);
  mach_port_deallocate(mach_task_self(), host_port);

  if (kr != KERN_SUCCESS) return -1;
  pthread_mutex_lock(&cpu_mutex);
  if (!has_prev) {
    prev_load = cpu_load;
    has_prev = true;
    pthread_mutex_unlock(&cpu_mutex);
    return sys_get_cpu_stats(user_usage_percentage, system_usage_percentage, idle_usage_percentage, nice_usage_percentage, total_usage_percentage);
  }
  natural_t user_diff = cpu_load.cpu_ticks[CPU_STATE_USER] - prev_load.cpu_ticks[CPU_STATE_USER];
  natural_t sys_diff  = cpu_load.cpu_ticks[CPU_STATE_SYSTEM] - prev_load.cpu_ticks[CPU_STATE_SYSTEM];
  natural_t idle_diff = cpu_load.cpu_ticks[CPU_STATE_IDLE] - prev_load.cpu_ticks[CPU_STATE_IDLE];
  natural_t nice_diff = cpu_load.cpu_ticks[CPU_STATE_NICE] - prev_load.cpu_ticks[CPU_STATE_NICE];
  prev_load = cpu_load;
  pthread_mutex_unlock(&cpu_mutex);

  float total_diff = (float)(user_diff + sys_diff + idle_diff + nice_diff);

  *user_usage_percentage = 0.f;
  *system_usage_percentage = 0.f;
  *idle_usage_percentage = 0.f;
  *nice_usage_percentage = 0.f;
  *total_usage_percentage = 0.f;
  if (total_diff == 0) return 0;

  *user_usage_percentage = (float)user_diff / total_diff * 100.0f;
  *system_usage_percentage  = (float)sys_diff / total_diff * 100.0f;
  *idle_usage_percentage = (float)idle_diff / total_diff * 100.0f;
  *nice_usage_percentage = (float)nice_diff / total_diff * 100.0f;
  *total_usage_percentage = *user_usage_percentage + *system_usage_percentage + *nice_usage_percentage;

  return 0;
}

// inspiration taken from: https://github.com/exelban/stats
int sys_get_mem_stats(uint64_t* total, uint64_t* used, uint64_t* app, uint64_t* wired, uint64_t* compressed, uint64_t* cached, uint64_t* swap_used) {
  size_t sz = sizeof(*total);
  if (sysctlbyname("hw.memsize", total, &sz, NULL, 0) != 0) return -1;

  mach_port_t host_port = mach_host_self();
  vm_size_t pagesize;
  host_page_size(host_port, &pagesize);

  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  kern_return_t kr = host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count);
  mach_port_deallocate(mach_task_self(), host_port);

  if (kr != KERN_SUCCESS) return -2;

  *app = (vm_stats.active_count) * pagesize;
  *wired = (vm_stats.wire_count) * pagesize;
  *compressed = (vm_stats.compressor_page_count) * pagesize;
  *cached = (vm_stats.inactive_count + vm_stats.purgeable_count + vm_stats.speculative_count) * pagesize;
  *used = *app + *wired + *compressed;

  struct xsw_usage vmusage;
  size_t vmusage_size = sizeof(vmusage);
  *swap_used = 0;
  if (sysctlbyname("vm.swapusage", &vmusage, &vmusage_size, NULL, 0) == 0) *swap_used = vmusage.xsu_used;

  return 0;
}

// inspiration taken from: https://github.com/exelban/stats
int sys_get_network_stats(double* rx_bps, double* tx_bps) {
  static uint64_t prev_rx = 0, prev_tx = 0;
  static struct timeval prev_time = {0};
  static pthread_mutex_t network_mutex = PTHREAD_MUTEX_INITIALIZER;

  struct ifaddrs *ifa_list = NULL, *ifa;
  uint64_t rx = 0, tx = 0;

  if (getifaddrs(&ifa_list) != 0) return -1;
  for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
    if (!(ifa->ifa_flags & IFF_UP) || !ifa->ifa_data) continue;
    if (ifa->ifa_addr->sa_family != AF_LINK) continue;
    if (strncmp(ifa->ifa_name, "lo", 2) == 0) continue;

    struct if_data *ifd = (struct if_data *)ifa->ifa_data;
    rx += ifd->ifi_ibytes;
    tx += ifd->ifi_obytes;
  }
  freeifaddrs(ifa_list);

  struct timeval now;
  if (gettimeofday(&now, NULL) != 0) return -2;

  *rx_bps = 0;
  *tx_bps = 0;

  pthread_mutex_lock(&network_mutex);
  if (prev_time.tv_sec != 0) {
    double diff_time = (double)(now.tv_sec - prev_time.tv_sec) + (double)(now.tv_usec - prev_time.tv_usec) / 1000000.0;
    if (diff_time > 0) {
      *rx_bps = (double)(rx - prev_rx) / diff_time;
      *tx_bps = (double)(tx - prev_tx) / diff_time;
    }
  }
  prev_rx = rx;
  prev_tx = tx;
  prev_time = now;
  pthread_mutex_unlock(&network_mutex);

  return 0;
}

// refer to `man statvfs`
int sys_get_disk_stats(uint64_t* total_bytes, uint64_t* free_bytes, float* percentage_used) {
  struct statvfs stats;
  if (statvfs("/", &stats) != 0) return -1;

  *total_bytes = (uint64_t)stats.f_blocks * stats.f_frsize;
  *free_bytes = (uint64_t)stats.f_bfree * stats.f_frsize;
  *percentage_used = (float)(*total_bytes - *free_bytes) / (float)(*total_bytes) * 100.0f;
  return 0;
}

// inspiration taken from: https://www.programmersought.com/article/5314588404/
int sys_get_battery_stats(int* percent, bool* is_charging) {
  CFTypeRef blob = IOPSCopyPowerSourcesInfo();
  if (!blob) return -1;
  CFArrayRef sources = IOPSCopyPowerSourcesList(blob);
  #define free_resources do {if (sources) CFRelease(sources); if (blob) CFRelease(blob);} while (0)

  if (!sources) { free_resources; return -2; }
  if (CFArrayGetCount(sources) <= 0) { free_resources; return -3; }

  CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, 0));
  if (!desc) { free_resources; return -4; }

  CFNumberRef cap = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey));
  if (cap) CFNumberGetValue(cap, kCFNumberIntType, percent);

  CFStringRef state = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSPowerSourceStateKey));
  if (state) *is_charging = CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo;

  free_resources;
  #undef free_resources

  return 0;
}

// inspiration taken from: https://stackoverflow.com/a/11676260/16638833
int sys_get_uptime(long* uptime) {
  struct timeval boottime;
  size_t len = sizeof(boottime);
  int mib[2] = {CTL_KERN, KERN_BOOTTIME};

  if (sysctl(mib, 2, &boottime, &len, NULL, 0) != 0) return -1;
  if (boottime.tv_sec <= 0) return -2;

  struct timeval now;
  if (gettimeofday(&now, NULL) != 0) return -3;

  *uptime = now.tv_sec - boottime.tv_sec;
  return 0;
}
#endif // SYS_IMPLEMENTATION
#endif // SYS_H_
