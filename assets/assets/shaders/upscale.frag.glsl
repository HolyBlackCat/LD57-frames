#version 460

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 out_fragcolor;

void main()
{
    out_fragcolor = texture(u_tex, v_texcoord);
}
