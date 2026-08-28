// Remote-control mode: the engine goes limp and does only what the attached
// control panel tells it. Entered from the main menu (or headlessly via the
// "remote_control_mode" config key), guarded by a streamer key the operator
// types in - the panel must authenticate every connection with it.
//
// While the mode is active it owns the one control server; matches pick it up
// from here instead of running their own. At half time and before extra time
// the match holds for tactical changes until the streamer releases it, from
// the panel (resume) or the game's own phase menu.

#ifndef _HPP_REMOTE_CONTROL_MODE
#define _HPP_REMOTE_CONTROL_MODE

#include <string>

namespace RemoteControl {
class Server;
}

namespace RemoteControlMode {

// Starts the control server on 127.0.0.1:<"remote_control_port", 44700>.
// An empty key leaves the channel open (a closed rig may choose that).
// Returns false - and stays inactive - when the port cannot be bound.
bool Enter(const std::string& streamerKey);
void Leave();
bool IsActive();
RemoteControl::Server* GetServer();

// The half-time / extra-time hold. The phase menu raises and lowers it; the
// panel's resume command requests its release.
void SetHolding(bool holding);
bool IsHolding();
void RequestResume();
bool ConsumeResumeRequest();

}  // namespace RemoteControlMode

#endif
