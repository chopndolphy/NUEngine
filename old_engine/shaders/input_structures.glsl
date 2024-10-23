#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform SceneData{

	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
	vec4 cameraPos;
	vec4 cameraDir;
	float lightCutoff;
	float lightOuterCutoff;
	float lightIntensity;
} sceneData;

layout(set = 1, binding = 0) uniform GLTFMaterialData{
	
	vec4 colorFactors;
	vec4 metal_rough_factors;

} materialData;

layout(set = 1, binding = 1) uniform sampler2D colorTex;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;

// if ray-tracing is enabled
layout(set = 2, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 2, binding = 1, rgba32f) uniform image2D image;