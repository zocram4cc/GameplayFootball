// The lists the pre-match screen offers, derived from what is installed.
//
// The screen sets the weather, the time of day, the kits, the difficulty and the
// duration already. The stadium, the entrance and the post-match presentation are
// all things the engine can do and only a config file could reach. PES's own
// pre-match screen - Strip / Stadium / Kick Off / Game Plan / General Rigging /
// Camera - is the reference; see docs/VGL26_REFERENCE.md.
//
// Discovery is the caller's job, because it needs the filesystem. What counts as
// a choice, what it is called and what order the choices come in lives here.

#ifndef _HPP_MENU_PREMATCHCHOICES
#define _HPP_MENU_PREMATCHCHOICES

#include <string>
#include <vector>

namespace PrematchChoices {

struct Choice {
  std::string label;  // shown to the player, or a translation key
  std::string value;  // what goes into the configuration
};

// Stadiums, from the paths of the .object files under media/objects/stadiums.
std::vector<Choice> Stadiums(const std::vector<std::string>& objectPaths);

// Entrance families, from the directory names under media/cutscenes/ent. "Any"
// leads, so the engine keeps picking by competition and stadium unless told
// otherwise, and "None" trails, to skip the walkout.
std::vector<Choice> Entrances(const std::vector<std::string>& familyNames);

// Post-match families, read out of the flat result pool's file names.
std::vector<Choice> ResultCutscenes(const std::vector<std::string>& camtrackNames);

// "result_001_st000_cam1.camtrack" -> "001"; "" when the name carries no family.
std::string FamilyFromCamtrackName(const std::string& filename);

// A quantized slider's position and the index it stands for.
int IndexFromSlider(float value, int count);
float SliderFromIndex(int index, int count);

// Where a configured value sits in a list; the start of the list when it is not
// installed any more, rather than an index into nothing.
int IndexOfValue(const std::vector<Choice>& choices, const std::string& value);

}  // namespace PrematchChoices

#endif
