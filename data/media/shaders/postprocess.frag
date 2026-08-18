#version 150

#pragma optimize(on)

uniform sampler2D map_accumulation; // 0
uniform sampler2D map_modifier;     // 1
uniform sampler2D map_depth;        // 2
uniform sampler2D map_lut;          // 3

uniform float contextWidth;
uniform float contextHeight;
uniform float contextX;
uniform float contextY;

uniform vec2 cameraClip;

uniform float fogScale;
// How much fog this stadium wants (scenelighting.hpp). The pass washes everything
// distant with up to a quarter of the horizon's colour, which on a green sky
// turned Planet Namek's rock formations flat green - and PES's own atmosphere for
// that ground asks for no fog at all. 1 is what this shader always did.
uniform float fogStrength;
// How near in depth a neighbour must be to count in the edge blur, as a fraction
// of the fragment's own depth. 0 turns the blur off; huge accepts everything, which
// is the flat average that put a dark fringe around every object.
uniform float edgeBlurDepthTolerance;

// Exposure: the gain the engine's adaptation arrived at this frame, 1 for none. What
// it is and why it is not measured here: autoexposure.hpp.
uniform float exposureGain;

// A stadium can supply its own sky (see src/onthepitch/stadiumsky.hpp); these
// default to the constants this shader used to hardcode.
uniform vec3 skyZenithColor;
uniform vec3 skyHorizonColor;
uniform vec3 skyFogColor;
// PES grades its output through a colour lookup table per time of day, which lifts
// and saturates the whole frame; measured against the VGL26 broadcast our picture
// comes out about half as bright (its pitch 23/64/84 against our 8/34/41). Until
// those tables can be decoded (ftex.py cannot read their DXGI format yet), this is
// the knob that stands in for them: "graphics_brightness", 1 leaves the picture
// exactly as it was.
uniform float sceneBrightness;
// PES's own grading table, unrolled into a strip by
// tools/pes21_import/lut_strip.py: lutSize blue slices laid left to right, each
// one lutSize across (red) and lutSize down (green), with lutBands of those
// stacked downwards - one per time of day. lutStrength 0 leaves the picture
// ungraded, which is what happens when the strip was never imported.
uniform float lutSize;
uniform float lutBands;
uniform float lutBand;
uniform float lutStrength;
uniform mat4 inverseProjectionViewMatrix;

out vec4 stdout;

vec3 GetWorldPosition(vec2 texCoord, float depth) {
  vec4 projectedPos = vec4(texCoord.x * 2 - 1, texCoord.y * 2 - 1, depth * 2 - 1, 1.0f);
  vec4 worldPosition = inverseProjectionViewMatrix * projectedPos;
  worldPosition.xyz /= worldPosition.w;
  return worldPosition.xyz;
}

// http://mouaif.wordpress.com/2009/01/05/photoshop-math-with-glsl-shaders/

#define GammaCorrection(color, gamma) pow(color, 1.0 / gamma)

// For all settings: 1.0 = 100% 0.5=50% 1.5 = 150%
vec3 ContrastSaturationBrightness(vec3 color, float brt, float con, float sat) {
  const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);
  // Increase or decrease these values to adjust r, g and b color channels seperately
  vec3 AvgLumin = vec3(0.5, 0.5, 0.5);
  vec3 brtColor = color * brt;
  vec3 intensity = vec3(dot(brtColor, LumCoeff));
  vec3 satColor = mix(intensity, brtColor, sat);
  vec3 conColor = mix(AvgLumin, satColor, con);

  return conColor;
}

vec3 AlternateContrast(vec3 color, float bias) {
  return color * (1.0f - bias) + (-cos(clamp(color, 0.0f, 1.0f) * 3.14159265f) * 0.5f + 0.5f) * bias;
}

// This shader works in linear light - the framebuffer encodes on write, via
// glEnable(GL_FRAMEBUFFER_SRGB) - while PES authored its tables against the
// values that reach the screen. Graded linear, every pixel lands far too low on
// the curve and the picture comes out darker than it started, so the lookup
// happens in display space and the result is turned back.
vec3 LinearToDisplay(vec3 c) {
  c = clamp(c, 0.0f, 1.0f);
  return mix(c * 12.92f, 1.055f * pow(c, vec3(1.0f / 2.4f)) - 0.055f, step(vec3(0.0031308f), c));
}

vec3 DisplayToLinear(vec3 c) {
  c = clamp(c, 0.0f, 1.0f);
  return mix(c / 12.92f, pow((c + 0.055f) / 1.055f, vec3(2.4f)), step(vec3(0.04045f), c));
}

// Look a colour up in the strip. Red and green interpolate for free through the
// texture's own filtering - both axes stay inside their slice, since a channel of
// 1 lands on the centre of the last texel rather than its edge - and the blue
// axis is interpolated here between the two slices either side of it.
vec3 GradeThroughLut(vec3 color) {
  color = clamp(color, 0.0f, 1.0f);
  float last = lutSize - 1.0f;
  float slice = color.b * last;
  float slice0 = floor(slice);
  float slice1 = min(slice0 + 1.0f, last);

  float within = color.r * last + 0.5f;
  float row = lutBand * lutSize + color.g * last + 0.5f;

  vec2 texel = vec2(1.0f / (lutSize * lutSize), 1.0f / (lutSize * lutBands));
  vec3 low = texture2D(map_lut, vec2((slice0 * lutSize + within) * texel.x, row * texel.y)).rgb;
  vec3 high = texture2D(map_lut, vec2((slice1 * lutSize + within) * texel.x, row * texel.y)).rgb;
  return mix(low, high, slice - slice0);
}

// The engine's own grade: its brightness, and the contrast it raises around a
// fixed mid grey. Named so the exposure below can measure a tap through the same
// chain the picture goes through, instead of measuring the frame as lit and
// judging it against a target read off the graded picture.
const float kContrastBias = 0.3f;  // 0 == normal .. 1 == 'fake hdri'

vec3 EngineGrade(vec3 color, float brightness, float saturation) {
  return AlternateContrast(ContrastSaturationBrightness(color, brightness, 1.0f, saturation),
                           kContrastBias);
}

// PES's grade, through the LUT it ships. Lifts the midtones and rolls the top end
// off, which is most of why the frame as lit reads so much darker than the frame
// as shown.
vec3 LutGrade(vec3 color) {
  if (lutStrength <= 0.0f || lutSize < 2.0f) return color;
  vec3 graded = DisplayToLinear(GradeThroughLut(LinearToDisplay(color)));
  return mix(color, graded, clamp(lutStrength, 0.0f, 1.0f));
}

vec3 Compress(vec3 color, float startThreshold, float endThreshold) {
  float range = endThreshold - startThreshold;
  float compressedRange = 1.0f - startThreshold;
  for (int c = 0; c < 3; c++) {
    if (color[c] > startThreshold) {
      color[c] = (color[c] - startThreshold) / range; // 0 to 1
      color[c] = pow(color[c], 0.5f);
      color[c] = startThreshold + color[c] * compressedRange;
    }
  }
  return color;
}


void main(void) {
  vec2 texCoord = gl_FragCoord.xy;
  texCoord.x -= contextX;
  texCoord.y -= contextY;
  texCoord.x /= contextWidth;
  texCoord.y /= contextHeight;

  // Keep color channels aligned to avoid red/cyan ghosting on high-contrast edges.
  vec4 accum = texture2D(map_accumulation, texCoord);
  accum.a = 1.0;

  vec3 base = accum.rgb;

  vec4 modifier = texture2D(map_modifier, texCoord);

  // Edge blur: the anti-aliasing this path has instead of MSAA. ambient.frag's
  // GetEdge finds the silhouettes (a Sobel over depth and normals) and this softens
  // them.
  //
  // It used to average all four neighbours flat, which on an edge means averaging
  // *across* it: an object against a bright sky had its own dark pixels pulled onto
  // the sky side, so everything wore a one-pixel dark fringe. That is the "black
  // outline" the engine appeared to draw, and the same block still carries the
  // original author's explicitly black version, commented out below - the effect was
  // a deliberate style once and then abandoned, leaving the accident behind.
  //
  // So a neighbour only counts when it is on the same surface, judged by depth.
  // edgeBlurDepthTolerance is that test as a fraction of the fragment's own depth,
  // and it spans every behaviour worth having: 0 accepts nothing and turns the blur
  // off, a small value blurs along an edge but never across it, and a huge value
  // accepts everything, which is the old flat average.
  if (modifier.r > 0.0 && edgeBlurDepthTolerance > 0.0f) {
    float centreDepth = cameraClip.y / (texture2D(map_depth, texCoord).x - cameraClip.x);
    float limit = abs(centreDepth) * edgeBlurDepthTolerance;
    vec2 taps[4];
    taps[0] = vec2(0, 1 / contextHeight);
    taps[1] = vec2(1 / contextWidth, 0);
    taps[2] = vec2(0, -1 / contextHeight);
    taps[3] = vec2(-1 / contextWidth, 0);
    vec3 smoothPixel = vec3(0);
    float taken = 0.0f;
    for (int i = 0; i < 4; ++i) {
      float neighbourDepth =
          cameraClip.y / (texture2D(map_depth, texCoord + taps[i]).x - cameraClip.x);
      if (abs(neighbourDepth - centreDepth) > limit) continue;   // across the edge
      smoothPixel += texture2D(map_accumulation, texCoord + taps[i]).xyz;
      taken += 1.0f;
    }
    if (taken > 0.0f) {
      smoothPixel /= taken;
      base = base * (1.0 - modifier.r) + smoothPixel * modifier.r;
    }
    //base = base * (1.0 - modifier.r) + vec3(0, 0, 0) * modifier.r * 0.5 + smoothPixel * modifier.r * 0.5; // cartooney effect
  }


  // SSAO blur

  int SSAO_blurSize = 4;

  float SSAO = 0.0f; // texture2D(map_modifier, texCoord).g;

  vec2 texelSize = 1.0f / vec2(textureSize(map_modifier, 0));
  vec2 hlim = vec2(float(-SSAO_blurSize) * 0.5f + 0.5f);
  for (int x = 0; x < SSAO_blurSize; ++x) {
    for (int y = 0; y < SSAO_blurSize; ++y) {
      vec2 offset = (hlim + vec2(float(x), float(y))) * texelSize;
      SSAO += texture(map_modifier, texCoord + offset).g;
    }
  }
  SSAO = SSAO / float(SSAO_blurSize * SSAO_blurSize);
  //SSAO = clamp(SSAO * 3.0f - 2.0f, 0.0f, 1.0f);

  //vec3 fragColor = vec3(SSAO);
  vec3 fragColor = base * SSAO;

  // The exposure, as one number worked out on the engine side
  // (src/systems/graphics/rendering/autoexposure.hpp). It used to be measured right
  // here, from sixteen taps of this very buffer, and applied the same frame - which
  // cannot work: a fragment shader remembers nothing, so the gain was recomputed
  // from scratch every frame and jumped with every pan. Off a recorded match the
  // picture's mean brightness moved 0.02 frame to frame through the opening
  // cutscene, with single-frame jumps of -0.073 and +0.038. It read as a flicker.
  //
  // Now the frame that was just presented is measured on the engine side and the
  // gain walks toward what that asks for, so this is a multiply.
  fragColor *= exposureGain;

  // fog

  float depth = texture2D(map_depth, texCoord).x;

  // convert from non-linear to linear
  float fragDepth = cameraClip.y / (depth - cameraClip.x);

//  vec3 fogColor = vec3(0.84, 0.98, 1.0);
//  vec3 fogColor = vec3(1.0, 0.9, 0.86);
//  vec3 fogColor = vec3(0.85, 0.65, 1.0);
  vec3 fogColor = skyFogColor;

  float fogFactor = clamp(fragDepth * 0.01f * (1.0f - fogScale) - 0.16f * fogScale, 0.0f, 0.25f) *
                    clamp(fogStrength, 0.0f, 1.0f);

  fragColor = fragColor * (1.0f - fogFactor) + fogColor * fogFactor;

  float brightness = sceneBrightness;
  float saturation = 0.95f * (0.4f + SSAO * 0.6f); // SSAO shadows are less saturated

  // now happens automagically because of glEnable(GL_FRAMEBUFFER_SRGB)
/*
  fragColor.r = GammaCorrection(fragColor.r, gamma);
  fragColor.g = GammaCorrection(fragColor.g, gamma);
  fragColor.b = GammaCorrection(fragColor.b, gamma);
*/

  fragColor = EngineGrade(fragColor, brightness, saturation);

  // sky: color the empty background (cleared depth) with a view-direction
  // gradient instead of a flat fill - kills the white void behind open
  // stadiums without needing sky geometry. Applied after grading so the sky
  // keeps its saturation.
  if (depth > 0.999999f) {
    vec3 viewDir = normalize(GetWorldPosition(texCoord, 1.0f) -
                             GetWorldPosition(texCoord, 0.0f));
    vec3 skyZenith = skyZenithColor;
    vec3 skyHorizon = skyHorizonColor;
    float elevation = clamp(viewDir.z, 0.0f, 1.0f);
    vec3 sky = mix(skyHorizon, skyZenith, pow(elevation, 0.55f));
    // below the horizon fade to the graded fog fill so stadium gaps stay hazy
    vec3 gradedFog = EngineGrade(fogColor, brightness, saturation);
    fragColor = mix(gradedFog, sky, smoothstep(-0.06f, 0.02f, viewDir.z));
  }

  // PES's grade, over the whole picture including the sky - which is where it
  // goes in PES too. This is the tone curve the engine never had: it lifts the
  // midtones and rolls the top end off, rather than raising contrast around a
  // fixed mid grey the way AlternateContrast above does.
  fragColor = LutGrade(fragColor);

  // Cinematic Vignette
  vec2 uv = texCoord * 2.0 - 1.0;
  float vignette = max(0.0, 1.0 - dot(uv, uv) * 0.18);
  fragColor *= pow(vignette, 1.2);

  fragColor = clamp(fragColor, 0.0, 1.0);

  //gl_FragColor = vec4(fragColor, 0);
  //fragColor = vec3(0, 0.5, 1.0);
  // Opaque. Nothing composites the presented frame, so the alpha was never looked
  // at on screen - but the frame recorder writes this buffer straight out as rgba,
  // and a zero here made every captured still invisible in anything that honours
  // alpha while looking perfectly fine in anything that drops it.
  stdout = vec4(fragColor, 1.0f);
}
