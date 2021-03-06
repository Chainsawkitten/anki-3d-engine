// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#ifndef ANKI_SHADERS_FORWARD_SHADING_COMMON_FRAG_GLSL
#define ANKI_SHADERS_FORWARD_SHADING_COMMON_FRAG_GLSL

// Common code for all fragment shaders of BS
#include "shaders/Common.glsl"
#include "shaders/Functions.glsl"
#include "shaders/Clusterer.glsl"

// Global resources
layout(ANKI_TEX_BINDING(0, 0)) uniform sampler2D anki_msDepthRt;
#define LIGHT_SET 0
#define LIGHT_UBO_BINDING 0
#define LIGHT_SS_BINDING 0
#define LIGHT_TEX_BINDING 1
#define LIGHT_LIGHTS
#define LIGHT_COMMON_UNIS
#include "shaders/ClusterLightCommon.glsl"

#define anki_u_time u_time
#define RENDERER_SIZE (u_lightingUniforms.rendererSizeTimePad1.xy * 0.5)

// Varyings
layout(location = 0) flat in float in_alpha;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_posViewSpace;

layout(location = 0) out vec4 out_color;

void writeGBuffer(in vec4 color)
{
	out_color = vec4(color.rgb, 1.0 - color.a);
}

vec4 readAnimatedTextureRgba(sampler2DArray tex, float period, vec2 uv, float time)
{
	float layerCount = float(textureSize(tex, 0).z);
	float layer = mod(time * layerCount / period, layerCount);
	return texture(tex, vec3(uv, layer));
}

vec3 computeLightColor(vec3 diffCol)
{
	vec3 outColor = vec3(0.0);

	// Compute frag pos in view space
	vec3 fragPos = in_posViewSpace;

	// Find the cluster and then the light counts
	uint clusterIdx = computeClusterIndex(
		gl_FragCoord.xy / RENDERER_SIZE, u_near, u_clustererMagic, fragPos.z, u_clusterCountX, u_clusterCountY);

	uint idxOffset = u_clusters[clusterIdx];

	// Skip decals
	uint count = u_lightIndices[idxOffset];
	idxOffset += count + 1;

	// Point lights
	count = u_lightIndices[idxOffset++];
	while(count-- != 0)
	{
		PointLight light = u_pointLights[u_lightIndices[idxOffset++]];

		vec3 diffC = computeDiffuseColor(diffCol, light.diffuseColorShadowmapId.rgb);

		vec3 frag2Light = light.posRadius.xyz - fragPos;
		float att = computeAttenuationFactor(light.posRadius.w, frag2Light);

#if LOD > 1
		const float shadow = 1.0;
#else
		float shadow = 1.0;
		if(light.diffuseColorShadowmapId.w >= 0.0)
		{
			shadow = computeShadowFactorOmni(frag2Light,
				light.specularColorRadius.w,
				u_invViewRotation,
				light.atlasTilesPad2.xy,
				light.diffuseColorShadowmapId.w,
				u_shadowTex);
		}
#endif

		outColor += diffC * (att * shadow);
	}

	// Spot lights
	count = u_lightIndices[idxOffset++];
	while(count-- != 0)
	{
		SpotLight light = u_spotLights[u_lightIndices[idxOffset++]];

		vec3 diffC = computeDiffuseColor(diffCol, light.diffuseColorShadowmapId.rgb);

		vec3 frag2Light = light.posRadius.xyz - fragPos;
		float att = computeAttenuationFactor(light.posRadius.w, frag2Light);

		vec3 l = normalize(frag2Light);

		float spot = computeSpotFactor(l, light.outerCosInnerCos.x, light.outerCosInnerCos.y, light.lightDir.xyz);

#if LOD > 1
		const float shadow = 1.0;
#else
		float shadow = 1.0;
		float shadowmapLayerIdx = light.diffuseColorShadowmapId.w;
		if(shadowmapLayerIdx >= 0.0)
		{
			shadow = computeShadowFactorSpot(light.texProjectionMat, fragPos, light.specularColorRadius.w, u_shadowTex);
		}
#endif

		outColor += diffC * (att * spot * shadow);
	}

	return outColor;
}

void particleAlpha(vec4 color, vec4 scaleColor, vec4 biasColor)
{
	writeGBuffer(color * scaleColor + biasColor);
}

void fog(vec3 color, float fogAlphaScale, float fogDistanceOfMaxThikness)
{
	const vec2 screenSize = 1.0 / RENDERER_SIZE;

	vec2 texCoords = gl_FragCoord.xy * screenSize;
	float depth = texture(anki_msDepthRt, texCoords).r;
	float zFeatherFactor;

	vec4 fragPosVspace4 = u_invProjMat * vec4(vec3(UV_TO_NDC(texCoords), depth), 1.0);
	float sceneZVspace = fragPosVspace4.z / fragPosVspace4.w;

	float diff = max(0.0, in_posViewSpace.z - sceneZVspace);

	zFeatherFactor = min(1.0, diff / fogDistanceOfMaxThikness);

	writeGBuffer(vec4(color, zFeatherFactor * fogAlphaScale));
	// writeGBuffer(vec4(vec3(zFeatherFactor), 1.0));
}

#endif
