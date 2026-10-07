#version 450
// libfafdeviceprobe.so's Vulkan test pattern: one triangle that covers the 256x256 target, its
// corners from gl_VertexIndex (no vertex buffer). Compiled at build time by build_runner.py with the
// NDK's glslc into pattern.vert.inc (SPIR-V words).
void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
