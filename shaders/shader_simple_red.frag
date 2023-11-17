#version 450

layout(location = 0) out vec4 outColor;

// main is called for every fragment
// colors are 4-channel RGBA in [0,1] range
//
// unlike the vertex shader there is no built in output variable, but instead we declare
// it with layout(location = 0)
//
// this shader would color the entire triangle in red
void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}