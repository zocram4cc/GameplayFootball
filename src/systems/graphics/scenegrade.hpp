// Which of PES's colour grading tables a match is graded through.
//
// PES runs every frame through a 33-cubed colour lookup table chosen by time of
// day and weather. Its absence is why an imported stadium looks flat beside the
// broadcast: measured against the VGL26 reference our midtones sat 1.68x low
// (median 74 against 124) while our highlights were already hotter and our
// shadows 13x more crushed, which is a missing tone curve rather than a missing
// light. PES's day table lifts mid grey 0.5 to 0.616 and rolls 0.75..1.0 into
// 0.666..0.689; run our own pitch pixel through it and 26/92/110 becomes
// 19/132/152, against the reference's 32/139/197.
//
// tools/pes21_import/lut_strip.py unrolls the tables into one PNG - 33 blue
// slices across, one band per condition down - and postprocess.frag samples it.
// The band order here has to match that script's BAND_ORDER.

#ifndef _HPP_SYSTEMS_GRAPHICS_SCENEGRADE
#define _HPP_SYSTEMS_GRAPHICS_SCENEGRADE

namespace SceneGrade {

// lut_strip.py BAND_ORDER: day, cloudy, evening, night.
enum e_Band {
  e_Band_Day = 0,
  e_Band_Cloudy = 1,
  e_Band_Evening = 2,
  e_Band_Night = 3,
};
constexpr int kBandCount = 4;

// Where the importer writes the strip, relative to the data root. Absent, the
// engine simply does not grade.
extern const char* const kStripPath;

// `timeOfDay` and `weather` are the config sliders ("match_time_of_day": 0 day,
// 0.5 evening, 1 night; "match_weather": 0 dry, 1 wet), clamped here. PES's
// cloudy table is a daylight grade, so weather only matters while the sun is up.
int BandForConditions(float timeOfDay, float weather);

// The strip's shape, read off the image itself rather than configured: a table of
// `size` cubed is `size * size` wide, and the bands stack downwards. False when
// the picture is not a strip at all, in which case nothing should be graded -
// sampling an unrelated PNG as a table would tint the whole match.
bool StripDimensions(int width, int height, int& size, int& bands);

}  // namespace SceneGrade

#endif
