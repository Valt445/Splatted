#version 450


layout(location = 0)      in vec3  inColor;
layout(location = 1)      in float inOpacity;
layout(location = 2)      in vec2  inCoord;
layout(location = 3) flat in vec3  inConic;
layout(location = 4) flat in float inDebris;
layout(location = 5) flat in vec3  inLight;
layout(location = 6) flat in vec3  inUp;
layout(location = 7) flat in vec4  inShadowInv;
layout(location = 8) flat in vec3  inBall;
layout(location = 9) flat in float inShadow;

layout(location = 0) out vec4 outColor;

const vec3  SUN_COL    = vec3(1.00, 0.95, 0.86);
const vec3  SKY_COL    = vec3(0.86, 0.91, 1.00);
const float AMBIENT    = 0.55;
const float DIFFUSE    = 0.65;
const float SPEC_POWER = 40.0;
const float SPEC_INT   = 0.18;
const float SHADOW_MAX = 0.75;

void main()
{
    if (inDebris < 0.5) {
        vec2  d = inCoord;
        vec3  c = inConic;
        float power = -0.5 * (c.x * d.x * d.x + 2.0 * c.y * d.x * d.y + c.z * d.y * d.y);
        if (power > 0.0) discard;
        power = max(power, -100.0);

        float alpha = min(0.99, inOpacity * exp(power));
        if (alpha < 0.004) discard;

        outColor = vec4(inColor * alpha, alpha);
        return;
    }

    vec2  d   = inCoord;
    float rpx = inBall.z;
    float r   = length(d) / rpx;

    float aa    = 1.5 / rpx;
    float ballA = 1.0 - smoothstep(1.0 - aa, 1.0, r);

    vec3 ballCol = vec3(0.0);
    if (ballA > 0.0) {
        float rc  = min(r, 0.999);
        vec2  dir = (length(d) > 1e-4) ? d / length(d) : vec2(0.0);
        vec3  n   = normalize(vec3(dir.x * rc, -dir.y * rc, sqrt(1.0 - rc * rc)));

        vec3 L  = normalize(inLight);
        vec3 up = normalize(inUp);
        vec3 V  = vec3(0.0, 0.0, 1.0);

        vec3 base = inColor;

        float hemi    = dot(n, up) * 0.5 + 0.5;
        vec3  gndCol  = base * 0.6 + vec3(0.04);
        vec3  ambient = mix(gndCol, SKY_COL, hemi);

        float ao = mix(0.62, 1.0, hemi);

        float ndl  = dot(n, L);
        float diff = clamp((ndl + 0.25) / 1.25, 0.0, 1.0);

        vec3  H    = normalize(L + V);
        float spec = pow(max(dot(n, H), 0.0), SPEC_POWER) * SPEC_INT * step(0.0, ndl);
        float fres = pow(1.0 - max(n.z, 0.0), 4.0);

        ballCol  = base * (AMBIENT * ambient * ao + DIFFUSE * diff * SUN_COL);
        ballCol += spec * SUN_COL;
        ballCol += fres * 0.10 * SKY_COL;

        ballCol *= mix(1.0, 0.78, smoothstep(0.85, 1.0, r));
    }

    float s = 0.0;
    if (inShadow > 0.0) {
        mat2  inv = mat2(inShadowInv.xy, inShadowInv.zw);
        vec2  q   = inv * (d - inBall.xy);
        float k   = length(q);
        s  = (1.0 - smoothstep(0.15, 1.0, k)) * 0.55;
        s += (1.0 - smoothstep(0.00, 0.40, k)) * 0.30;
        s  = min(s, SHADOW_MAX) * inShadow;
    }

    if (ballA <= 0.0 && s <= 0.003) discard;

    outColor = vec4(ballCol * ballA, ballA + s * (1.0 - ballA));
}
