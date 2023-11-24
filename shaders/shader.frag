#version 450

// TODO(vulkan) location here is what? 0? 1?
layout(binding = 1) uniform sampler2D in_texSampler;

// input passed on from vertex shader
layout(location = 0) in vec3 in_fragColor;
layout(location = 1) in vec2 in_fragTexCoord;

layout(location = 0) out vec4 out_color;


// main is called for every fragment
// colors are 4-channel RGBA in [0,1] range
//
// unlike the vertex shader there is no built in output variable, but instead we declare
// it with layout(location = 0)
//
void main() {
    // color with red all vertices
    // outColor = vec4(1.0, 0.0, 0.0, 1.0);

//    out_color = vec4(in_fragColor, 1.0);

    // visualizing data using colors is the shader programming equivalent of priontf debugging, for lack
    // of a better option
//    out_color = vec4(in_fragTexCoord, 0.0, 1.0);

    // texture is a built-in function
    // takes care of filtering and transformations in the background
    out_color = texture(in_texSampler, in_fragTexCoord * 1.0);
}
