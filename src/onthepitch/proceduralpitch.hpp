// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_PROCEDURALPITCH
#define _HPP_PROCEDURALPITCH

#include <string>

#include "base/sdl_surface.hpp"
#include "defines.hpp"

using namespace blunted;

Uint32 GetPitchDiffuseColor(SDL_Surface* pitchSurf, float xCoord, float yCoord);
Uint32 GetPitchSpecularColor(SDL_Surface* pitchSurf, float xCoord, float yCoord);
Uint32 GetPitchNormalColor(SDL_Surface* pitchSurf, float xCoord, float yCoord);
void DrawLines(SDL_PixelFormat* pixelFormat, Uint32* diffuseBitmap, int resX, int resY,
               signed int offsetW, signed int offsetH);
// grassTexture is the seamless tile the pitch colour is built from; a converted
// stadium can supply its own (see pitchturf.hpp), which is how a PES stadium keeps
// its ground colour instead of wearing GF's green.
void GeneratePitch(int resX, int resY, int resSpecularX, int resSpecularY, int resNormalX,
                   int resNormalY, const std::string& grassTexture);

#endif
