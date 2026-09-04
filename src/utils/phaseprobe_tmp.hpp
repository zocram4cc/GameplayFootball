// TEMPORARY PROBE - PHASELOG - strip before committing
#ifndef _HPP_PHASEPROBE_TMP
#define _HPP_PHASEPROBE_TMP
#include <chrono>
#include <cstdio>
#include <cstdlib>
struct PhaseProbe {
  const char* name;
  double sum = 0, mx = 0;
  int n = 0;
  std::chrono::steady_clock::time_point t0;
  bool on = getenv("GF_FPSLOG") != nullptr;
  explicit PhaseProbe(const char* name) : name(name) {}
  void Begin() { if (on) t0 = std::chrono::steady_clock::now(); }
  void End() {
    if (!on) return;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    sum += ms; n++; if (ms > mx) mx = ms;
    if (n == 500) {
      printf("PHASELOG %s mean=%.2fms max=%.1f\n", name, sum / n, mx);
      fflush(stdout);
      sum = 0; mx = 0; n = 0;
    }
  }
};
#endif
