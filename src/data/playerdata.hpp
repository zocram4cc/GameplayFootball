// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_PLAYERDATA
#define _HPP_PLAYERDATA

#include "../gamedefines.hpp"
#include "../onthepitch/matchpressure.hpp"
#include "../utils.hpp"
#include "base/properties.hpp"
#include "defines.hpp"
#include "playerskills.hpp"
#include "playingstyles.hpp"

class PlayerData {
public:
  PlayerData(int playerDatabaseID);
  PlayerData();
  virtual ~PlayerData();

  std::string GetFirstName() const { return firstName; }
  std::string GetLastName() const { return lastName; }
  int GetDatabaseID() const { return databaseID; }
  // The positions this player is registered for: the first is the one he is
  // listed as, the rest are the ones he can also play. The game plan's line-up
  // shows them on his card and the secondary button adds or removes one, the
  // way PES's registered positions work.
  const std::vector<e_PlayerRole>& GetRoles() const;
  // Adds the role if he does not have it, removes it if he does; -> whether he
  // has it afterwards. His listed position (the first) is never removed - a
  // player with no position at all is not a thing the rest of the engine
  // expects (PlayerSkills::Infer, the plan card's aptitude colours).
  bool ToggleRole(e_PlayerRole role);

  float GetStat(const char* name) const;

  // PES's overall: the mean of the outfield skill ratings, or of the gk_* ones
  // for a player listed as a keeper. Used for substitution decisions and printed
  // on the game plan's cards. It lived inside PlayerBase, which the menus have no
  // access to before kick-off.
  float GetAverageStat() const;

  // PES 2021 Playing Style and COM Playing Styles ("playing cards"): from
  // profile_xml's <playing_style>/<com_styles>, inferred when the profile
  // carries neither tag.
  PlayingStyles::Player GetPlayingStyle() const { return playingStyle; }
  PlayingStyles::ComMask GetComStyles() const { return comStyles; }

  // PES 2021 Player Skills (profile_xml <skills>, or the older <traits> tag).
  PlayerSkills::Mask GetSkills() const { return skills; }
  // MatchPressure::unknownAge when the player was not loaded from the database.
  int GetAge() const { return playerAge; }

  int GetSkinColor() { return skinColor; }
  std::string GetHairStyle() { return hairStyle; }
  std::string GetHairColor() { return hairColor; }
  float GetHeight() { return height; }

protected:
  // Profiles from before PES's remaining attributes had keys get each missing
  // one from its nearest older neighbour, so GetStat never asserts on them.
  void FillMissingStats();
  int databaseID;
  std::string firstName;
  std::string lastName;
  std::vector<e_PlayerRole> roles;

  Properties stats;
  PlayerSkills::Mask skills;
  PlayingStyles::Player playingStyle;
  PlayingStyles::ComMask comStyles;
  int playerAge;

  int skinColor;
  std::string hairStyle;
  std::string hairColor;
  float height;
};

#endif
