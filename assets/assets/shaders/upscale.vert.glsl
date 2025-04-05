#version 460

layout(location = 0) in vec2 a_pos;
layout(location = 0) out vec2 v_texcoord;

void main()
{
    v_texcoord = a_pos * 0.5 - 0.5;
    gl_Position = vec4(a_pos, 0, 1);
}
