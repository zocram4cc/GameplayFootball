#version 150

/* matrix spaces primer :

  http://stackoverflow.com/questions/8207420/desktop-glsl-without-ftransform

    A model matrix transforms an object from object coordinates to world coordinates.
    A view matrix transforms the world coordinates to eye coordinates.
    A projection matrix converts eye coordinates to clip coordinates.

  Based on standard naming conventions, the mvpMatrix is projection * view * model, in that order.
  There is no other matrices that you need to multiply by.

  Projection is your projection matrix (either ortho or perspective),
  view is the camera transform matrix (NOT the modelview), and
  model is the position, scale, and rotation of your object.
*/

#pragma optimize(on)

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

// Instancing. PES's crowd is one spectator standing at every seat and its 3D turf
// one tuft across the pitch; a stadium's stands come to thousands of copies, which
// as separate meshes would be millions of vertices in the scene. instanceCount is
// 0 for ordinary geometry, and otherwise says how many placements of this batch are
// in instancePlacement: x, y, z and the yaw each copy faces.
const int kMaxInstances = 256;
uniform int instanceCount;
uniform vec4 instancePlacement[kMaxInstances];

in vec4 position;
in vec3 normal;
in vec3 texcoord;
in vec3 tangent;
in vec3 bitangent;

// view space
out vec4 frag_position;
out vec3 frag_normal;
out vec3 frag_texcoord;
out vec3 frag_tangent;
out vec3 frag_bitangent;

void main(void) {
  vec4 localPosition = position;
  vec3 localNormal = normal;
  vec3 localTangent = tangent;
  vec3 localBitangent = bitangent;

  if (instanceCount > 0) {
    // The copy this vertex belongs to: turned about the vertical axis and set
    // down where it stands. Every branch here is decided by a uniform, so the
    // whole batch takes the same path.
    vec4 placement = instancePlacement[gl_InstanceID];
    float s = sin(placement.w);
    float c = cos(placement.w);
    localPosition = vec4(localPosition.x * c - localPosition.y * s,
                         localPosition.x * s + localPosition.y * c,
                         localPosition.z, localPosition.w);
    localPosition.xyz += placement.xyz;
    localNormal = vec3(localNormal.x * c - localNormal.y * s,
                       localNormal.x * s + localNormal.y * c, localNormal.z);
    localTangent = vec3(localTangent.x * c - localTangent.y * s,
                        localTangent.x * s + localTangent.y * c, localTangent.z);
    localBitangent = vec3(localBitangent.x * c - localBitangent.y * s,
                          localBitangent.x * s + localBitangent.y * c, localBitangent.z);
  }

  mat4 modelViewMatrix = viewMatrix * modelMatrix;
  frag_position = modelViewMatrix * localPosition;

  mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
	frag_normal = normalMatrix * localNormal;
	frag_tangent = normalMatrix * localTangent;
	frag_bitangent = normalMatrix * localBitangent;
  frag_texcoord.st = texcoord.st;

  gl_Position = projectionMatrix * frag_position;
}
