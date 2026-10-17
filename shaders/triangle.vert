#version 450

/* A triangle from gl_VertexIndex alone: no vertex buffer. It turns with
 * time, and aspect keeps it from stretching on a wide screen. */
layout(push_constant) uniform Push {
   float time;
   float aspect;
} push;

layout(location = 0) out vec3 out_color;

const vec2 positions[3] = vec2[](vec2(0.0, -0.6), vec2(0.52, 0.3), vec2(-0.52, 0.3));
const vec3 colors[3] = vec3[](vec3(1.0, 0.25, 0.2), vec3(0.2, 0.9, 0.3), vec3(0.25, 0.4, 1.0));

void main()
{
   float c = cos(push.time);
   float s = sin(push.time);
   vec2 p = positions[gl_VertexIndex];
   p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
   p.x /= push.aspect;
   gl_Position = vec4(p, 0.0, 1.0);
   out_color = colors[gl_VertexIndex];
}
