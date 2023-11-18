#version 450

// alignment requirements: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/chap15.html#interfaces-resources-layout
// nested structs may cause problems

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

// ****************************************

layout(location = 0) out vec3 out_frag_color;

// ****************************************

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(in_position, 0.0, 1.0);
    out_frag_color = in_color;
}
