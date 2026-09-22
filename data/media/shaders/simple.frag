#version 150

#pragma optimize(on)

uniform sampler2D map_albedo;
uniform sampler2D map_normal;
uniform sampler2D map_specular;
uniform sampler2D map_illumination;
uniform sampler2D map_timing;

uniform vec3 materialparams;
uniform vec3 materialbools;

// PES's per-material UV animation (uvscroll/uvstep). The uniforms carry the
// whole time dependence, precomputed in float32 on the CPU: the shader only
// shifts (scroll) or scales+shifts (step) the base UV, and samples the timing
// map at (sweepU, timingV + sweepV) when one is bound. Static materials sit at
// family 0, scale 1, everything 0, so this is their ordinary sample.
uniform int uvanim_family;    // 0 none, 1 scroll, 2 step
uniform vec2 uvanim_scroll;   // (du, dv): the scroll translation
uniform vec2 uvanim_step;     // (du, dv): the step tile offset
uniform vec2 uvanim_scale;    // (su, sv): base-UV scale into tile space
uniform float uvanim_dTime;   // the timing sweep U position
uniform float uvanim_dTimeV;  // the timing sweep V position

// all these are in eye-space
in vec4 frag_position;
in vec3 frag_normal;
in vec3 frag_texcoord;
in vec3 frag_tangent;
in vec3 frag_bitangent;

out vec4 stdout0;
out vec4 stdout1;
out vec4 stdout2;

void main(void) {

  vec2 uv = frag_texcoord.st;
  if (uvanim_family == 2) {
    uv = uv * uvanim_scale + uvanim_step;
  } else if (uvanim_family == 1) {
    uv = uv + uvanim_scroll;
  }
  vec4 base = texture2D(map_albedo, uv);
  if (base.a < 0.12) discard;
  if (uvanim_family == 2) {
    // The timing map's alpha is a binary mask the sweep walks across (the
    // preview gates visibility on it at 0.5): a sampled 0 hides this
    // fragment wherever the sweep has not yet arrived.
    float tAlpha = texture2D(map_timing,
                             vec2(uvanim_dTime, frag_texcoord.p + uvanim_dTimeV)).a;
    if (tAlpha < 0.5) discard;
  }
  vec3 bump;
  if (materialbools.x == 1.0f) {
    bump = normalize(texture2D(map_normal, frag_texcoord.st).xyz * 2.0 - 1.0);
  } else {
    bump = vec3(0, 0, 1);
  }
  float spec;
  if (materialbools.y == 1.0f) {
    spec = texture2D(map_specular, frag_texcoord.st).x * materialparams.y;
  } else {
    spec = materialparams.y;
  }
  float illumination;
  if (materialbools.z == 1.0f) {
    illumination = texture2D(map_illumination, frag_texcoord.st).x;
  } else {
    illumination = materialparams.z;
  }


  // todo: make sure bump mapping is 'correct'; more info @ http://blog.selfshadow.com/publications/blending-in-detail/

  // recently disabled vec3 n = normalize(frag_normal);
  vec3 bumpNormal = bump.x * frag_tangent + bump.y * frag_bitangent + bump.z * frag_normal;//n;

  // partial derivative
  // bumpNormal = normalize(vec3(n.xy * bn.z + bn.xy * n.z, n.z * bn.z));

  // white-out blending
  //bumpNormal = normalize(vec3(n.xy + bn.xy, n.z * bn.z));

  //bn = bn.x * frag_tangent + bn.y * frag_bitangent + bn.z * vec3(0, 0, 1);

  bumpNormal = normalize(bumpNormal);

  stdout0 = vec4(base.rgb, spec);
  stdout1 = vec4(bumpNormal.xyz, materialparams.x);
  //gl_FragData[2] = vec4(frag_position.xyz, illumination); // *fixed* position data is needed for the ssao ambient shader. maybe a bit inefficient? in the other shaders, position is calculated on the fly
  stdout2 = vec4(0, 0, 0, illumination); // free channels! for future use, methinks :)
}
