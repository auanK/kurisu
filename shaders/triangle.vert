#version 450

const vec2 kPositions[3] = vec2[3](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
}
