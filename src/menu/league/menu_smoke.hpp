#ifndef _HPP_MENU_LEAGUE_MENU_SMOKE
#define _HPP_MENU_LEAGUE_MENU_SMOKE

#include <string>

#include "../../main.hpp"

namespace league_menu_smoke {

constexpr unsigned long kAdvanceDelay_ms = 350;
constexpr unsigned long kDialogDelay_ms = 350;
constexpr unsigned long kQuitDelay_ms = 600;

inline std::string GetRoute() {
  return GetConfiguration()->Get("menu_smoke_test_league_route", "");
}

inline bool BootstrapEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_league", false);
}

inline bool HasRoute() {
  return !GetRoute().empty();
}

inline bool AnyEnabled() {
  return BootstrapEnabled() || HasRoute();
}

inline bool RouteEnabled(const char* route) {
  return GetRoute() == route;
}

inline unsigned long Now_ms() {
  return EnvironmentManager::GetInstance().GetTime_ms();
}

}  // namespace league_menu_smoke

#endif
