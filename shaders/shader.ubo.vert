#version 450

// alignment requirements: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/chap15.html#interfaces-resources-layout
// nested structs may cause problems

// its possible to bind multiple descriptor sets simultaneously
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_texCoord;

// ****************************************

layout(location = 0) out vec3 out_fragColor;
layout(location = 1) out vec2 out_fragTexCoord;

// ****************************************

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(in_position, 0.0, 1.0);
    out_fragColor = in_color;
    out_fragTexCoord = in_texCoord;
}
