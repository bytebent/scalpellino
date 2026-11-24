// scalpellino shader 1
// name: LightmappedReflective
// kind: material
// about: Source's mirror and reflective-glass shader (SDK 2013's lightmappedreflective): on a
// about: flat brush face, the scene as seen in the mirror, and, if it sees through, what is
// about: behind the glass, mixed by Fresnel; a normal map ripples both. Renders draw these
// about: pictures for up to 8 mirror planes in view; elsewhere (terrain, props, more planes, a
// about: mirror seen in a mirror) it reflects the reflection probes or the sky. Optional: a
// about: base texture lit by the baked light, drawn under it. Lets light through in the bake.
// about: Without $basetexture the editor shows the material's %tooltexture (as Hammer).
// setting: $basetexture texture | optional: drawn lit under the reflection
// setting: $reflecttexture text | Source's _rt_WaterReflection (the default: on); anything else (e.g. none): no reflection
// setting: $reflecttint color 1 1 1 | reflection colour (as Source: gamma)
// setting: $reflectamount float 0.8 | how far the normal map shifts the reflection (picture widths)
// setting: $refracttexture text | Source's _rt_WaterRefraction (the default: sees through); anything else (e.g. none): no see-through
// setting: $refracttint color 1 1 1 | colour of what is seen through (as Source: gamma)
// setting: $refractamount float 0 | how far the normal map shifts what is seen through
// setting: $normalmap texture | ripples (normal map; its alpha scales them)
// setting: $envmapmask texture | mask for the reflection and the see-through (rgb)
// setting: $fresnelpower float 5 | Fresnel: how quickly the reflection grows at grazing angles
// setting: $minreflectivity float 0 | Fresnel: the reflection looking straight at it (1: a mirror)
// setting: $maxreflectivity float 1 | Fresnel: the reflection at grazing angles
// setting: $reflectivity color | light bake: how much light it bounces

vec4 shade() {
    vec3 n = worldNormal();
    vec2 offR = vec2(0.0), offT = vec2(0.0);
    if (u_normalmap_set != 0) {
        vec4 ns = texture(u_normalmap, baseUV());
        vec3 nt = normalize(ns.rgb * 2.0 - 1.0);
        offR = nt.xy * ns.a * u_reflectamount;
        offT = nt.xy * ns.a * u_refractamount;
        n = tangentToWorld(nt);
    }
    bool reflects = (mirrorWants & 1) != 0, through = (mirrorWants & 2) != 0;
    vec3 refl = reflects ? mirrorReflection(offR) * toLinear(u_reflecttint) : vec3(0.0);
    vec3 refr = through ? mirrorRefraction(offT) * toLinear(u_refracttint) : vec3(0.0);
    vec3 mask = u_envmapmask_set != 0 ? texture(u_envmapmask, baseUV()).rgb : vec3(1.0);
    float ndv = clamp(dot(eyeVector(), n), 0.0, 1.0);
    float fresnel = (u_maxreflectivity - u_minreflectivity) * pow(1.0 - ndv, u_fresnelpower) + u_minreflectivity;
    vec3 result = vec3(0.0);
    if (reflects && through) result = mix(refr, refl, clamp(fresnel, 0.0, 1.0)) * mask;
    else if (reflects) result = refl * mask;
    else if (through) result = refr * mask;
    if ((mirrorWants & 4) != 0) result += toLinear(baseColor().rgb) * bakedLight();
    return vec4(result, 1.0); // drawn with the solid faces (the see-through is its own picture)
}
