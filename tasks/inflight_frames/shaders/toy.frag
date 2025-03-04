#version 430

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"

layout(location = 0) out vec4 fragColor;

layout(binding = 1, set = 0) uniform AppData
{
  UniformParams params;
};
layout(binding = 0) uniform sampler2D iChannel0;
//layout(binding = 1) uniform sampler2D iChannel1;

float iTime;
vec2 iResolution;

float sdBox( vec3 p, vec3 b )
{
  vec3 q = abs(p) - b;
  return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

float sdPlane(vec3 p, vec3 n, float d) {
    return dot(p, n) + d;
}

float sdCylinder( vec3 p)
{
  vec3 c = vec3(2.0, 2.0, 2.0);
  return length(p.xz-c.xy)-c.z;
}

vec3 Color = vec3(1.0, 1.0, 1.0);

float sdf(vec3 p) {
    float step = 6.0;

    float cylDist = sdCylinder(p);
    float planceDist = sdPlane(p, vec3(0.04, 1.0, 0.0), 5.0);
    
    if (true) {
        Color = vec3(165.0 / 255.0, 157.0 / 255.0, 132.0 / 255.0);
        return cylDist;
    } else {
        Color = vec3(34.0 / 255.0 ,139.0 / 255.0,34.0 / 255.0);
        return planceDist;
    }
}

vec3 triplanar(vec3 pos, vec3 normal) {
    vec3 weights;
    
    weights  = smoothstep(0.0, 1.0, abs(normal));
    weights /= dot(weights, vec3(1.0));

    vec2 uvX = pos.yz * 0.5 + 0.5;
    vec2 uvY = pos.xz * 0.5 + 0.5;
    vec2 uvZ = pos.xy * 0.5 + 0.5;

    vec3 colorX = texture(iChannel0, uvX).rgb;
    vec3 colorY = texture(iChannel0, uvY).rgb;
    vec3 colorZ = texture(iChannel0, uvZ).rgb;

    return colorX * weights.x + colorY * weights.y + colorZ * weights.z;
}

float raymarch(vec3 origin, vec3 dir) {
    float totalDist = 0.0;
    const float MAX_DISTANCE = 100.0;
    const float EPS = 0.01;
    vec3 pos;
    float dist;
 
    for (int i = 0; i < 100; i++) {
        pos = origin + totalDist * dir;
        dist = sdf(pos);
  
        if (dist < EPS)
            return totalDist;
     
        totalDist += dist;
        if (totalDist > MAX_DISTANCE) break;
    }
    return MAX_DISTANCE;
}

void main() {
    iTime = params.time;

    ivec2 fragCoord = ivec2(gl_FragCoord.xy);
    vec2 uv = (fragCoord - 0.7 * params.resolution.xy) / params.resolution.y;

    vec3 rayDir = normalize(vec3(uv, 1.0));
    vec3 rayOrigin = vec3(10.0, 20.0, -60.0);

    float dist = raymarch(rayOrigin, rayDir);

    vec3 color = vec3(0.0);
    if (dist < 100.0) {
        vec3 hitPoint = rayOrigin + rayDir * dist;
        
        vec3 normal = normalize(vec3(
            sdf(hitPoint + vec3(0.01, 0, 0)) - sdf(hitPoint - vec3(0.01, 0, 0)),
            sdf(hitPoint + vec3(0, 0.01, 0)) - sdf(hitPoint - vec3(0, 0.01, 0)),
            sdf(hitPoint + vec3(0, 0, 0.01)) - sdf(hitPoint - vec3(0, 0, 0.01))
        ));

        vec3 lightPos = vec3(5.0 * sin(iTime * 3.0), 5.0, 5.0 * cos(iTime * 3.0));
        vec3 lightDir = normalize(lightPos - hitPoint);
        float diffuse = max(dot(normal, lightDir), 0.0);
        float ambient = 0.1;
        
        Color = triplanar(hitPoint, normal);
        color = (ambient + diffuse) * Color;
        fragColor = vec4(color, 1.0);
    } else {
        fragColor = texture(iChannel0, uv);
    }
}
