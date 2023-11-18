#version 450

// vertex attributes
// they're properties that are specified per-vertex in the vertex buffer
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

// ********************************************************************************
// an example:
// ********************************************************************************
// "layout(location = x)" annotations assign indices to the inputs that we can
// later use to reference them. some types, like dvec3 64 bit vectors, use multiple slots
//layout(locationn = 0) in dvec3 in_position;
//layout(location = 2) in vec3 in_color;
// ********************************************************************************

layout(location = 0) out vec3 out_frag_color;

// the main function is invoked for every vertex
// gl_VertexIndex is the index of the current vertex
// z == 0.0, w = 1.0 (dummy coordinates)
// built-in variable gl_Position functions as the output
void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);

    // this is matched by
    // layout(location = 0) in vec3 ...
    // in the fragment shader
    out_frag_color = in_color;
}
