$input v_color0

#include <bgfx_shader.sh>
#include "shaderlib.sh"

uniform vec4 u_alphaParams; // x: mode (0 opaque, 1 mask, 2 blend), y: cutoff

vec3 linearToSrgb(vec3 color)
{
	color = clamp(color, 0.0, 1.0);
	vec3 low = color * 12.92;
	vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
	return mix(high, low, step(color, vec3(0.0031308, 0.0031308, 0.0031308)));
}

void main()
{
	vec4 color = v_color0;
	if (u_alphaParams.x < 0.5)
		color.a = 1.0;
	else if (u_alphaParams.x < 1.5 && color.a < u_alphaParams.y)
		discard;
	if (u_alphaParams.z < 0.5)
		color.rgb = linearToSrgb(color.rgb);
	gl_FragColor = color;
}
