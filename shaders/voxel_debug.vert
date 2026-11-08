#version 450
layout(set = 0, binding = 0) uniform UBO { mat4 proj; mat4 view; vec2 viewport; vec2 focal; } ubo;
layout(std430, set = 0, binding = 11) readonly buffer Solid { uint solid[]; };
layout(std430, set = 0, binding = 13) readonly buffer Grid { vec4 origin_size; uvec4 dims; } grid;

layout(location = 0) out vec3 outColor;

// the 8 corners of a unit cube, and the 12 edges as 24 corner indices (pairs)
// 6 faces, 2 triangles each, as corner indices into the 8-corner cube
const vec3 corners[8] = vec3[](vec3(0,0,0), vec3(1,0,0), vec3(1,1,0), vec3(0,1,0),
                               vec3(0,0,1), vec3(1,0,1), vec3(1,1,1), vec3(0,1,1));
const int  tris[36] = int[](
    0,2,1, 0,3,2,     // -Z face
    4,5,6, 4,6,7,     // +Z face
    0,4,7, 0,7,3,     // -X face
    1,2,6, 1,6,5,     // +X face
    0,1,5, 0,5,4,     // -Y face (bottom)
    3,7,6, 3,6,2);    // +Y face (top)
const vec3 normals[6] = vec3[](vec3(0,0,-1), vec3(0,0,1), vec3(-1,0,0),
                               vec3(1,0,0),  vec3(0,-1,0), vec3(0,1,0));

void main()
{
    uint  v  = solid[gl_InstanceIndex];
    uint  nx = grid.dims.x, ny = grid.dims.y;
    uvec3 c  = uvec3(v % nx, (v / nx) % ny, v / (nx * ny));

    vec3 corner = corners[tris[gl_VertexIndex]];
    vec3 p = grid.origin_size.xyz + (vec3(c) + corner) * grid.origin_size.w;
    gl_Position = ubo.proj * ubo.view * vec4(p, 1.0);

    // simple lighting: faces pointing at the light are brighter
    vec3  n     = normals[gl_VertexIndex / 6];             // 6 vertices per face
    float light = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 1.0, 0.3))), 0.0);
    vec3  base  = mix(vec3(0.1, 0.4, 1.0), vec3(0.2, 1.0, 0.3), float(c.y) / float(grid.dims.y));
    outColor = base * light;
}