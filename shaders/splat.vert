#version 450


layout(set = 0, binding = 0) uniform UBO { mat4 proj; mat4 view; vec2 viewport; vec2 focal; } ubo;

layout(std430, set = 0, binding = 1) readonly buffer Positions { float positions[]; };
layout(std430, set = 0, binding = 2) readonly buffer Rotations { vec4  rotations[]; };
layout(std430, set = 0, binding = 3) readonly buffer Scales    { float scales[]; };
layout(std430, set = 0, binding = 4) readonly buffer Opacities { float opacities[]; };
layout(std430, set = 0, binding = 5) readonly buffer Colors    { float colors[]; };
layout(std430, set = 0, binding = 7) readonly buffer Indices   { uint  sorted[]; };
layout(std430, set = 0, binding = 8) readonly buffer Prev      { float prev[]; };
layout(std430, set = 0, binding = 9) readonly buffer Awake     { uint  awake[]; };

layout(location = 0)      out vec3  outColor;
layout(location = 1)      out float outOpacity;
layout(location = 2)      out vec2  outCoord;
layout(location = 3) flat out vec3  outConic;
layout(location = 4) flat out float outDebris;
layout(location = 5) flat out vec3  outLight;
layout(location = 6) flat out vec3  outUp;
layout(location = 7) flat out vec4  outShadowInv;
layout(location = 8) flat out vec3  outBall;
layout(location = 9) flat out float outShadow;

const float BALL_MIN    = 0.012;
const float BALL_MAX    = 0.040;
const vec3  WORLD_LIGHT = vec3(0.35, 1.0, 0.25);

float hash(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / float(0x7fffffff);
}

mat3 quat_to_mat(vec4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return mat3(1.0 - 2.0*(y*y + z*z), 2.0*(x*y + w*z),       2.0*(x*z - w*y),
                2.0*(x*y - w*z),       1.0 - 2.0*(x*x + z*z), 2.0*(y*z + w*x),
                2.0*(x*z + w*y),       2.0*(y*z - w*x),       1.0 - 2.0*(x*x + y*y));
}

void cull() { gl_Position = vec4(0.0, 0.0, 2.0, 1.0); }

vec2 to_px(vec3 world, vec2 ndc_center)
{
    vec4 c = ubo.proj * (ubo.view * vec4(world, 1.0));
    if (c.w <= 0.0001) return vec2(1e6);
    return (c.xy / c.w - ndc_center) * 0.5 * ubo.viewport;
}

void main()
{
    uint  idx = sorted[gl_InstanceIndex];
    vec3  pos = vec3(positions[3*idx], positions[3*idx+1], positions[3*idx+2]);
    vec4  rot = rotations[idx];
    vec3  scl = vec3(scales[3*idx], scales[3*idx+1], scales[3*idx+2]);
    float opa = opacities[idx];
    vec3  col = vec3(colors[3*idx], colors[3*idx+1], colors[3*idx+2]);
    uint  age = awake[idx];

    outConic = vec3(0.0);  outDebris = 0.0;  outLight = vec3(0.0);  outUp = vec3(0.0);
    outShadowInv = vec4(0.0);  outBall = vec3(0.0);  outShadow = 0.0;

    vec4 viewPos = ubo.view * vec4(pos, 1.0);
    if (viewPos.z > -0.2) { cull(); return; }

    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));

    vec4 clipC = ubo.proj * viewPos;
    vec2 ndcC  = clipC.xy / clipC.w;
    float ndcZ = clipC.z / clipC.w;

    if (age > 0u) {
        float orig = max(scl.x, max(scl.y, scl.z));
        float R    = clamp(orig * 0.5, BALL_MIN, BALL_MAX) * mix(0.85, 1.15, hash(idx));

        float depth = -viewPos.z;
        float rpx   = max(R * ubo.focal.y / depth, 0.75);

        vec3  Lw      = normalize(WORLD_LIGHT);
        vec3  contact = pos + vec3(0.0, -R, 0.0) + vec3(-Lw.x, 0.0, -Lw.z) * (R * 0.6);
        float SR      = R * 1.35;

        vec2  sC = to_px(contact, ndcC);
        vec2  a  = to_px(contact + vec3(SR, 0.0, 0.0), ndcC) - sC;
        vec2  b  = to_px(contact + vec3(0.0, 0.0, SR), ndcC) - sC;
        float det = a.x * b.y - a.y * b.x;

        vec3  pv      = vec3(prev[3*idx], prev[3*idx+1], prev[3*idx+2]);
        float speed   = length(pos - pv);
        float shadowK = 1.0 - smoothstep(0.003, 0.015, speed);

        vec2 e = abs(a) + abs(b);
        bool shadow_ok = shadowK > 0.0 && abs(det) > 1.0 &&
                         all(lessThan(e, vec2(20.0 * rpx + 50.0))) && sC.x < 1e5;

        vec2 lo = vec2(-rpx), hi = vec2(rpx);
        if (shadow_ok) { lo = min(lo, sC - e); hi = max(hi, sC + e); }
        lo -= 1.5;  hi += 1.5;

        vec2 offPx  = mix(lo, hi, corner);
        gl_Position = vec4(ndcC + offPx * 2.0 / ubo.viewport, ndcZ, 1.0);

        mat2 inv = shadow_ok ? inverse(mat2(a, b)) : mat2(0.0);

        outColor     = col * mix(0.92, 1.08, hash(idx * 13u + 5u));
        outOpacity   = 1.0;
        outCoord     = offPx;
        outDebris    = 1.0;
        outLight     = normalize(mat3(ubo.view) * Lw);
        outUp        = normalize(mat3(ubo.view) * vec3(0.0, 1.0, 0.0));
        outShadowInv = vec4(inv[0], inv[1]);
        outBall      = vec3(sC, rpx);
        outShadow    = shadow_ok ? shadowK : 0.0;
        return;
    }

    vec3  t = viewPos.xyz;
    float d = -t.z;

    float limX = 1.3 * 0.5 * ubo.viewport.x / ubo.focal.x;
    float limY = 1.3 * 0.5 * ubo.viewport.y / ubo.focal.y;
    t.x = clamp(t.x / d, -limX, limX) * d;
    t.y = clamp(t.y / d, -limY, limY) * d;

    mat3 J = mat3(ubo.focal.x / d,               0.0,                            0.0,
                  0.0,                           -ubo.focal.y / d,               0.0,
                  ubo.focal.x * t.x / (d * d),   -ubo.focal.y * t.y / (d * d),   0.0);

    mat3 W = mat3(ubo.view);
    mat3 T = J * W;

    mat3 R = quat_to_mat(normalize(rot));
    mat3 S = mat3(scl.x, 0.0, 0.0,  0.0, scl.y, 0.0,  0.0, 0.0, scl.z);
    mat3 M = R * S;
    mat3 cov3D = M * transpose(M);
    mat3 cov   = T * cov3D * transpose(T);

    float ca = cov[0][0] + 0.3;
    float cb = cov[0][1];
    float cc = cov[1][1] + 0.3;
    float det = ca * cc - cb * cb;
    if (det <= 0.0) { cull(); return; }

    float mid    = 0.5 * (ca + cc);
    float lambda = mid + sqrt(max(0.1, mid * mid - det));
    float radius = ceil(3.0 * sqrt(lambda));

    vec2 offPx  = (corner * 2.0 - 1.0) * radius;
    gl_Position = vec4(ndcC + offPx * 2.0 / ubo.viewport, ndcZ, 1.0);

    outColor   = col;
    outOpacity = opa;
    outCoord   = offPx;
    outConic   = vec3(cc, -cb, ca) / det;
}
