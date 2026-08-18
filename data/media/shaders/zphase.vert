#version 150

#pragma optimize(on)

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

// Instancing, the same as simple.vert: 0 for ordinary geometry, otherwise the
// number of placements of this batch in instancePlacement (x, y, z, yaw). The
// depth pass has to agree with the colour pass about where a copy stands, or the
// crowd fails its own depth test.
const int kMaxInstances = 256;
uniform int instanceCount;
uniform vec4 instancePlacement[kMaxInstances];

in vec4 position;

void main(void) {
  vec4 localPosition = position;
  if (instanceCount > 0) {
    vec4 placement = instancePlacement[gl_InstanceID];
    float s = sin(placement.w);
    float c = cos(placement.w);
    localPosition = vec4(localPosition.x * c - localPosition.y * s,
                         localPosition.x * s + localPosition.y * c,
                         localPosition.z, localPosition.w);
    localPosition.xyz += placement.xyz;
  }
  gl_Position = projectionMatrix * viewMatrix * modelMatrix * localPosition;
}
