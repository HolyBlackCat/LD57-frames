#version 460

layout(set = 1, binding = 0) uniform Uni
{
    vec2 u_scr_size;
};

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_factors;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_texcoord;
layout(location = 2) out vec3 v_factors;

void main()
{
    gl_Position = vec4(a_pos * 2 / u_scr_size, 0, 1);
    v_color = a_color;
    v_texcoord = a_texcoord;
    v_factors = a_factors;
}
