// scalpellino shader 1
// name: Water
// kind: material
// about: Source's water (SDK 2013's water shader): on the top face of a brush (paint the other
// about: faces nodraw, as in Hammer) or on an endless water plane. What the surface reflects
// about: (drawn from the mirrored camera) and what is under it (seen through the murk:
// about: $fogcolor from $fogstart to $fogend, by how far the view travels under the water) are
// about: mixed by Fresnel and rippled by a normal map. The reflection shows the map and the
// about: sky; props and primitives only with $reflectentities. Far away it fades into cheap
// about: water ($cheapwaterstartdistance to $cheapwaterenddistance, Hammer units): the
// about: reflection probes mixed with the murk's colour by Fresnel; $forcecheap is always
// about: cheap. Up to 8 water and mirror planes per picture get pictures; the rest are cheap.
// about: The surface must be level. Lets light through in the bake.
// about: Without $basetexture the editor shows the material's %tooltexture (as Hammer).
// setting: $normalmap texture water_ripple_normal | the ripples (normal map; its alpha scales them); Source's default is dev/water_normal
// setting: $bumptransform text | where the ripples are read: "center .5 .5 scale 1 1 rotate 0 translate 0 0"
// setting: $reflecttexture text | Source's _rt_WaterReflection (the default: reflects); anything else (e.g. none): no reflection picture
// setting: $reflecttint color 1 1 1 | reflection colour (as Source: gamma)
// setting: $reflectamount float 0.8 | how far the ripples shift the reflection (picture widths)
// setting: $reflectentities bool 0 | props and primitives show in the reflection too (Source: only the map and the sky)
// setting: $refracttexture text | Source's _rt_WaterRefraction (the default: sees under the surface); anything else: no
// setting: $refracttint color 1 1 1 | colour of what is seen under the surface (as Source: gamma)
// setting: $refractamount float 0 | how far the ripples shift what is under the surface
// setting: $fogcolor color 0.2 0.25 0.25 | the murk's colour (as Source: gamma); also cheap water's colour
// setting: $fogstart float 0 | where the murk starts (Hammer units under the water, along the view)
// setting: $fogend float 0 | where it is complete (Hammer units); not more than $fogstart: clear water
// setting: $forcecheap bool 0 | always cheap water: the reflection probes mixed with $fogcolor by Fresnel, no pictures
// setting: $cheapwaterstartdistance float 500 | from here (Hammer units from the camera) it fades into cheap water
// setting: $cheapwaterenddistance float 1000 | from here it is cheap water
// setting: $envmap text | cheap water's reflection: env_cubemap (the default) = the reflection probes; any other value = the sky
// setting: $basetexture texture | optional: drawn lit under the reflection (Source: only when it doesn't see under the surface)
// setting: $reflectivity color | light bake: how much light it bounces

vec4 shade() {
    vec3 n = worldNormal();
    vec3 nt = vec3(0.0, 0.0, 1.0);
    float na = 0.0;
    if (u_normalmap_set != 0) {
        vec4 ns = texture(u_normalmap, bumpUV(baseUV()));
        nt = normalize(ns.rgb * 2.0 - 1.0);
        na = ns.a;
        n = tangentToWorld(nt);
    }
    vec2 offR = nt.xy * na * u_reflectamount;
    vec2 offT = nt.xy * na * u_refractamount;
    bool reflects = (mirrorWants & 1) != 0, through = (mirrorWants & 2) != 0;
    bool hasR = mirrorReflect == 1, hasT = mirrorRefract == 1;
    vec3 tintR = toLinear(u_reflecttint), tintT = toLinear(u_refracttint);
    vec3 murk = toLinear(u_fogcolor);
    float ndv = clamp(dot(eyeVector(), n), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 5.0); // Source's water: fixed power 5
    vec3 r = reflect(-eyeVector(), n);
    vec3 env = ((u_envmap_set != 0 && envProbes == 0) ? skyEnvmap(r) : probesOrSky(r)) * tintR;
    vec3 cheap = mix(murk, env, fresnel); // Source's cheap water (opaque)
    vec3 result;
    if (!hasR && !hasT) {
        result = cheap;
    } else {
        vec3 R = hasR ? mirrorReflection(offR) * tintR : env;
        vec3 T = hasT ? mirrorRefraction(offT) * tintT : murk;
        vec3 full = (reflects && through) ? mix(T, R, fresnel) : reflects ? R : T;
        // the fade into cheap water with distance (Source's 2-pass blend: the probes over
        // what is under the surface, more of them the farther)
        float d = length(worldPos() - camPos) / 2.54;
        float t = clamp((d - u_cheapwaterstartdistance) / max(u_cheapwaterenddistance - u_cheapwaterstartdistance, 1.0), 0.0, 1.0);
        vec3 farOff = mix(T, env, clamp(fresnel + t, 0.0, 1.0));
        result = mix(full, farOff, t);
        if ((mirrorWants & 4) != 0 && !through) {
            vec4 b = baseColor();
            result = toLinear(b.rgb) * bakedLight() + R * fresnel * b.a;
        }
    }
    return vec4(result, 1.0);
}
