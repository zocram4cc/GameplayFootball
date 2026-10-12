// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_TEAMDATA
#define _HPP_TEAMDATA

#include <memory>

#include "../gamedefines.hpp"
#include "base/properties.hpp"
#include "defines.hpp"
#include "playerdata.hpp"

// Hardcoded reference position for a role; custom formation positions are
// blended with it so extreme settings stay sane.
blunted::Vector3 GetDefaultRolePosition(e_PlayerRole role);

struct TeamTactics {
  TeamTactics() {}

  Properties factoryProperties;
  Properties userProperties;

  Properties humanReadableNames;
  Properties descriptions;
};

class TeamData {
public:
  TeamData(int teamDatabaseID);
  virtual ~TeamData();

  std::string GetName() const { return name; }
  std::string GetShortName() const { return shortName; }
  std::string GetLogoUrl() const { return logo_url; }
  std::string GetKitUrl() const { return kit_url; }
  Vector3 GetColor1() const { return color1; }
  Vector3 GetColor2() const { return color2; }

  int GetDatabaseID() const { return databaseID; }

  const TeamTactics& GetTactics() const { return tactics; }
  TeamTactics& GetTacticsWritable() { return tactics; }

  FormationEntry GetFormationEntry(int num);
  void SetFormationEntry(int num, FormationEntry entry);

  void SwitchPlayers(int databaseID1, int databaseID2);

  // vector index# is entry in formation[index#]
  const std::vector<std::unique_ptr<PlayerData>>& GetPlayerData() { return playerData; }
  int GetPlayerNum() { return static_cast<int>(playerData.size()); }
  // nullptr rather than a throw for an index nobody has: the game plan's map
  // asks about a slot the squad may not have (a bench card past the end), and
  // it checks the answer. .at() made those checks unreachable.
  PlayerData* GetPlayerData(int num) {
    return (num >= 0 && num < (int)playerData.size()) ? playerData.at(num).get() : nullptr;
  }
  PlayerData* GetPlayerDataByDatabaseID(int id);

  void SaveLineup();
  void SaveTactics();
  void Save();

protected:
  int databaseID;

  std::string name;
  std::string shortName;
  std::string logo_url;
  std::string kit_url;
  Vector3 color1, color2;

  TeamTactics tactics;

  FormationEntry formation[playerNum];

  std::vector<std::unique_ptr<PlayerData>> playerData;
};

#endif
