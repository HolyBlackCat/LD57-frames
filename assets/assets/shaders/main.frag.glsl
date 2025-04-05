#version 460

layout(set = 2, binding = 0) uniform sampler2D u_texture;

layout(set = 3, binding = 0) uniform Uni
{
    vec2 u_tex_size;
};


layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_texcoord;
layout(location = 2) in vec3 v_factors;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 tex_color = texture(u_texture, v_texcoord / u_tex_size);
    out_color = vec4(mix(v_color.rgb, tex_color.rgb, v_factors.x),
                     mix(v_color.a  , tex_color.a  , v_factors.y));

    out_color.rgb *= out_color.a;
    out_color.a *= v_factors.z;
}
