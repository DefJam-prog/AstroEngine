$input a_position, a_texcoord0, a_normal
$output v_texcoord0, v_normal, v_worldPos

#include <bgfx_shader.sh>

uniform mat3 u_normalMatrix;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
	v_normal = normalize(mul(u_normalMatrix, a_normal));
	v_worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
}
