$input v_color0, v_normal, v_worldPos

#include <bgfx_shader.sh>

uniform vec4 u_alphaParams;
uniform vec4 u_lightParams;
uniform vec4 u_lightDirection[4];
uniform vec4 u_lightColor[4];
uniform vec4 u_pointPosition[4];
uniform vec4 u_pointColor[4];
uniform vec4 u_pointRange[4];
uniform vec4 u_baseColorFactor;

vec3 srgbToLinear(vec3 color)
{
	vec3 low = color / 12.92;
	vec3 high = pow((color + 0.055) / 1.055, vec3(2.4, 2.4, 2.4));
	return mix(high, low, step(color, vec3(0.04045, 0.04045, 0.04045)));
}

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
	if (u_lightParams.x > 0.5 || u_lightParams.y > 0.5)
		color.a *= u_baseColorFactor.a;
	if (u_alphaParams.x < 0.5)
		color.a = 1.0;
	else if (u_alphaParams.x < 1.5 && color.a < u_alphaParams.y)
		discard;
	if (u_lightParams.x > 0.5 || u_lightParams.y > 0.5)
	{
		vec3 normal = normalize(v_normal);
		vec3 diffuse = vec3(0.0, 0.0, 0.0);
		if (u_lightParams.x > 0.5) diffuse += u_lightColor[0].rgb * max(dot(normal, -normalize(u_lightDirection[0].xyz)), 0.0);
		if (u_lightParams.x > 1.5) diffuse += u_lightColor[1].rgb * max(dot(normal, -normalize(u_lightDirection[1].xyz)), 0.0);
		if (u_lightParams.x > 2.5) diffuse += u_lightColor[2].rgb * max(dot(normal, -normalize(u_lightDirection[2].xyz)), 0.0);
		if (u_lightParams.x > 3.5) diffuse += u_lightColor[3].rgb * max(dot(normal, -normalize(u_lightDirection[3].xyz)), 0.0);
		vec3 toPoint0 = u_pointPosition[0].xyz - v_worldPos;
		float dist0 = length(toPoint0);
		if (u_lightParams.y > 0.5 && dist0 < u_pointRange[0].x) diffuse += u_pointColor[0].rgb * max(dot(normal, normalize(toPoint0)), 0.0) * pow(max(1.0 - dist0 / u_pointRange[0].x, 0.0), 2.0) / max(dist0 * dist0, 0.01);
		vec3 toPoint1 = u_pointPosition[1].xyz - v_worldPos;
		float dist1 = length(toPoint1);
		if (u_lightParams.y > 1.5 && dist1 < u_pointRange[1].x) diffuse += u_pointColor[1].rgb * max(dot(normal, normalize(toPoint1)), 0.0) * pow(max(1.0 - dist1 / u_pointRange[1].x, 0.0), 2.0) / max(dist1 * dist1, 0.01);
		vec3 toPoint2 = u_pointPosition[2].xyz - v_worldPos;
		float dist2 = length(toPoint2);
		if (u_lightParams.y > 2.5 && dist2 < u_pointRange[2].x) diffuse += u_pointColor[2].rgb * max(dot(normal, normalize(toPoint2)), 0.0) * pow(max(1.0 - dist2 / u_pointRange[2].x, 0.0), 2.0) / max(dist2 * dist2, 0.01);
		vec3 toPoint3 = u_pointPosition[3].xyz - v_worldPos;
		float dist3 = length(toPoint3);
		if (u_lightParams.y > 3.5 && dist3 < u_pointRange[3].x) diffuse += u_pointColor[3].rgb * max(dot(normal, normalize(toPoint3)), 0.0) * pow(max(1.0 - dist3 / u_pointRange[3].x, 0.0), 2.0) / max(dist3 * dist3, 0.01);
		color.rgb *= u_baseColorFactor.rgb * clamp(diffuse, 0.0, 1.0);
	}
	if (u_alphaParams.z < 0.5) color.rgb = linearToSrgb(color.rgb);
	gl_FragColor = color;
}

