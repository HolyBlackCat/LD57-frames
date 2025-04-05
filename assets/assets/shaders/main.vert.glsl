#version 460

layout(set = 1, binding = 0) uniform Uni
{
    vec2 u_scr_size;
};

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_texcoord;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    v_texcoord = a_texcoord;
    gl_Position = vec4(a_pos / u_scr_size, 0, 1);
}
