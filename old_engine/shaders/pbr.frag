#version 460

#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inWorldPos;
layout (location = 2) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;
// ----------------------------------------------------------------------------
// Easy trick to get tangent-normals to world-space to keep PBR code simplified.
// Don't worry if you don't get what's going on; you generally want to do normal 
// mapping the usual way for performance anyways; I do plan make a note of this 
// technique somewhere later in the normal mapping tutorial.
vec3 getNormalFromMap()
{
    vec3 tangentNormal = texture(normalTex, inUV).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(inWorldPos);
    vec3 Q2  = dFdy(inWorldPos);
    vec2 st1 = dFdx(inUV);
    vec2 st2 = dFdy(inUV);

    vec3 N   = normalize(inNormal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}
// ----------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
// ----------------------------------------------------------------------------
void main()
{	
    // vec3 lightPosition = vec3(0.0, 1000.0, 0.0);
    // vec3 lightPosition = vec3(sceneData.sunlightDirection.xyz);
    // vec3 lightPosition[2];
    // lightPosition[0] = vec3(sceneData.cameraPos.xyz + sceneData.sunlightDirection.xyz);
    // lightPosition[1] = vec3(sceneData.cameraPos.xyz - sceneData.sunlightDirection.xyz);

    vec3 lightPosition[2];
    lightPosition[0] = vec3(0.313066, 3.040065, -0.570378);
    lightPosition[1] = vec3(1.460736, 6.127971, -0.455218);
    
    float lightIntensity[2];
    //lightIntensity[0] = sceneData.lightIntensity;
    lightIntensity[0] = 15.0;
    lightIntensity[1] = sceneData.lightIntensity;

    vec3 albedo     = pow(texture(colorTex, inUV).rgb * materialData.colorFactors.rgb, vec3(2.2));
    // float metallic  = texture(metalRoughTex, inUV).b;
    // float roughness = texture(metalRoughTex, inUV).g;
    // float ao        = texture(metalRoughTex, inUV).r;
    float metallic = materialData.metal_rough_factors.x;
    float roughness = materialData.metal_rough_factors.y;

    // vec3 N = getNormalFromMap();
    vec3 N = normalize(inNormal);
    vec3 V = normalize(sceneData.cameraPos.xyz - inWorldPos);

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0 
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)    
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // reflectance equation
    vec3 Lo = vec3(0.0);
        // calculate per-light radiance
    for (int i = 0; i < 2; i++)
    {
        vec3 L = normalize(lightPosition[i] - inWorldPos);
        vec3 H = normalize(V + L);
        float lightDistance = length(lightPosition[i] - inWorldPos);
        float attenuation = 1.0 / (lightDistance * lightDistance);
        vec3 radiance = sceneData.sunlightColor.xyz * lightIntensity[i] * attenuation;
        // vec3 radiance = (sceneData.sunlightColor.xyz * vec3(20.0));
        // vec3 radiance = sceneData.sunlightColor.xyz ;

        // spot light
        // float theta = dot(L, normalize(sceneData.cameraDir.xyz));
        // float epsilon = (sceneData.lightCutoff - sceneData.lightOuterCutoff);
        // float intensity = clamp((theta - sceneData.lightOuterCutoff) / (epsilon + 0.0001), 0.0, 1.0);
        // radiance *= intensity;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator    = NDF * G * F; 
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
        vec3 specular = numerator / denominator;
        
        // kS is equal to Fresnel
        vec3 kS = F;
        // for energy conservation, the diffuse and specular light can't
        // be above 1.0 (unless the surface emits light); to preserve this
        // relationship the diffuse component (kD) should equal 1.0 - kS.
        vec3 kD = vec3(1.0) - kS;
        // multiply kD by the inverse metalness such that only non-metals 
        // have diffuse lighting, or a linear blend if partly metal (pure metals
        // have no diffuse light).
        kD *= 1.0 - metallic;	  

        // scale light by NdotL
        float NdotL = max(dot(N, L), 0.0);        

        vec3 origin = inWorldPos;
        vec3 direction = L;
        float tMin = 0.01;
        float tMax = lightDistance;

        vec3 contribution = (kD * albedo / PI + specular) * radiance * NdotL;

        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, tMin, direction, tMax);

        while(rayQueryProceedEXT(rayQuery))
        {
        }

        if (rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
            contribution *= 0.1;
        }

        // add to outgoing radiance Lo
        Lo += contribution;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
    }
    
    // ambient lighting (note that the next IBL tutorial will replace 
    // this ambient lighting with environment lighting).

    // vec3 ambient = vec3(0.1) * albedo;
    vec3 ambient = vec3(0.03) * albedo;
    // vec3 ambient = vec3(0.06) * albedo * ao;
    
    vec3 color = ambient + Lo;

    // Ray-traced shadows

    // HDR tonemapping
    color = color / (color + vec3(1.0));
    // gamma correct
    color = pow(color, vec3(1.0/2.2)); 

    outFragColor = vec4(color, 1.0);
}
