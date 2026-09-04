#ifndef _HPP_GUI2_VIEW_PLANMAPCARD
#define _HPP_GUI2_VIEW_PLANMAPCARD

#include <string>
#include <vector>

#include "gametypes.hpp"

// What a game-plan card says about one player.
//
// PES draws each starter on the pitch as a card: his portrait, a dark strip under it
// with his position on the left and his rating on the right, and his name beneath.
// The position is coloured by line - measured off the /vg/ League 26 broadcast, where
// GK reads yellow, LB and CB cyan, DMF and AMF green, CF and SS red - and the
// captain carries a small badge on the portrait.
//
// Ours drew two lines of text on a green rectangle and nothing else, so the game plan
// said less about a squad than the team sheet it was supposed to summarise.
namespace PlanMapCard {

// Three channels, 0..255. Its own type rather than Vector3: that pulls in the whole
// math and logging stack for what is a colour constant, and this module is worth
// testing on its own.
struct Colour {
  float r = 0.0f, g = 0.0f, b = 0.0f;
};

// How far apart two colours look, for telling one line's label from another's.
float Distance(const Colour& a, const Colour& b);

// The four lines a position falls in, which is what the colour encodes.
enum e_Line {
  e_Line_Keeper = 0,
  e_Line_Defence,
  e_Line_Midfield,
  e_Line_Attack,
};

e_Line LineOf(e_PlayerRole role);

// The colour PES puts on the position abbreviation, 0..255 per channel.
Colour LineColour(e_Line line);

// Whether a player is in a position he plays. PES tints the name and frame by this,
// and the roles a player carries are the only evidence we have of it: his own
// position, one on the same line, or neither.
enum e_Aptitude {
  e_Aptitude_Natural = 0,   // the slot is one of his own positions
  e_Aptitude_SameLine,      // a different position on the same line
  e_Aptitude_OutOfPosition, // another line entirely
};

e_Aptitude AptitudeFor(e_PlayerRole slot, const std::vector<e_PlayerRole>& playerRoles);

Colour AptitudeColour(e_Aptitude aptitude);

// How many positions besides this slot the player is registered for.
int OtherRegisteredRoles(e_PlayerRole slot, const std::vector<e_PlayerRole>& playerRoles);

// The position as the card prints it. A player can be registered for several
// positions (the map's secondary button toggles them), and PES shows that in the
// player's own info panel - on the pitch card there is room for a count, so a
// centre back who also plays right back reads "CB+1" and the toggle is visible
// where it is used rather than only in a submenu.
std::string SlotRoleText(const std::string& roleName, int otherRegisteredRoles);

// The rating as the card prints it. PES shows an integer, and a squad whose stats have
// never been rated must not print "0" over every card.
std::string RatingText(float stat);

// A name as the card prints it. Cards stand a card's width apart, so a long name
// runs into its neighbours - "THE FAULT LIES WITH YOU" covers three of them - and is
// cut with a stop rather than allowed to.
std::string NameText(const std::string& name, unsigned int budget);

}  // namespace PlanMapCard

#endif
