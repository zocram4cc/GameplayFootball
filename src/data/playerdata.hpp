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
#include "playertraits.hpp"

class PlayerData {
public:
  PlayerData(int playerDatabaseID);
  PlayerData();
  virtual ~PlayerData();

  std::string GetFirstName() const { return firstName; }
  std::string GetLastName() const { return lastName; }
  int GetDatabaseID() const { return databaseID; }
  const std::vector<e_PlayerRole>& GetRoles() const;

  float GetStat(const char* name);

  // The mean of the twenty-two stats: the engine's own notion of how good a player
  // is, used for substitution decisions and printed on the game plan's cards. It
  // lived inside PlayerBase, which the menus have no access to before kick-off.
  float GetAverageStat() const;

  // Gives the player a style of his own when the database defines none.
  void AssignPlayingStyles();

  // Traits / specialties this player carries.
  PlayerTraits::TraitMask GetTraits() const { return traits; }
  // MatchPressure::unknownAge when the player was not loaded from the database.
  int GetAge() const { return playerAge; }

  int GetSkinColor() { return skinColor; }
  std::string GetHairStyle() { return hairStyle; }
  std::string GetHairColor() { return hairColor; }
  float GetHeight() { return height; }

protected:
  int databaseID;
  std::string firstName;
  std::string lastName;
  std::vector<e_PlayerRole> roles;

  Properties stats;
  PlayerTraits::TraitMask traits;
  int playerAge;

  int skinColor;
  std::string hairStyle;
  std::string hairColor;
  float height;
};

#endif
