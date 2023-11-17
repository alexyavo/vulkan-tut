#version 450

// input passed on from vertex shader
layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;


// main is called for every fragment
// colors are 4-channel RGBA in [0,1] range
//
// unlike the vertex shader there is no built in output variable, but instead we declare
// it with layout(location = 0)
//
void main() {
    // color with red all vertices
    // outColor = vec4(1.0, 0.0, 0.0, 1.0);

    outColor = vec4(fragColor, 1.0);
}
