#version 450
// The pattern of Probe.h's PatternPixel, from window coordinates: red = x & 255, green = y & 255,
// blue = a 32-pixel checkerboard, alpha 1. Every value is k/255, which an RGBA8 target stores exactly.
layout(location = 0) out vec4 outColor;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    float checker = float(((p.x >> 5) ^ (p.y >> 5)) & 1);
    outColor = vec4(float(p.x & 255) / 255.0, float(p.y & 255) / 255.0, checker, 1.0);
}
