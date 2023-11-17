#version 450

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[] (
        vec2(0.0, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5)
);

// passed on to the fragment shader
vec3 colors[3] = vec3[] (
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

// the main function is invoked for every vertex
// gl_VertexIndex is the index of the current vertex
// z == 0.0, w = 1.0 (dummy coordinates)
// built-in variable gl_Position functions as the output
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);

    // this is matched by
    // layout(location = 0) in vec3 fragColor
    // in the fragment shader
    fragColor = colors[gl_VertexIndex];
}
