// scalpellino shader 1
// name: VertexLitGeneric
// kind: material
// about: Source's model shader (props): the base texture times the prop's baked light (per
// about: vertex, or like a Source model: its ambient cube and 2 strongest lights with
// about: Half-Lambert), with glow, cutouts, see-through and cheap chrome. Phong, rim light
// about: and bump maps: v0.12.4b.
// setting: $basetexture texture | the picture
// setting: $color color 1 1 1 | tint (linear)
// setting: $selfillum bool 0 | glow: the base texture's alpha marks what glows
// setting: $selfillumtint color 1 1 1 | glow colour and strength
// setting: $selfillummask texture | glow mask instead of the base alpha (red channel)
// setting: $alphatest bool 0 | cutout
// setting: $alphatestreference float 0.5 | cutout threshold
// setting: $translucent bool 0 | see-through by the base alpha
// setting: $alpha float 1 | opacity
// setting: $nocull bool 0 | draw both sides
// setting: $envmap text | reflection: env_cubemap = the sky (cheap chrome)
// setting: $envmaptint color 1 1 1 | reflection colour and strength
// setting: $envmapmask texture | reflection mask (rgb)
// setting: $basealphaenvmapmask bool 0 | the base alpha masks the reflection (inverted, as Source's model shader)
// setting: $envmapcontrast float 0 | reflection contrast
// setting: $envmapsaturation float 1 | reflection saturation

vec4 shade() {
    vec4 base = baseColor();
    if (u_alphatest != 0 && base.a < u_alphatestreference) discard;
    vec3 albedo = toLinear(base.rgb) * u_color;
    vec3 diffuse = albedo * bakedLight();
    if (u_selfillum != 0) {
        float glow = u_selfillummask_set != 0 ? texture(u_selfillummask, baseUV()).r : base.a;
        diffuse = mix(diffuse, albedo * u_selfillumtint, glow);
    }
    vec3 spec = vec3(0.0);
    if (u_envmap_set != 0) {
        vec3 e = envmap(reflect(-eyeVector(), worldNormal()));
        if (u_envmapmask_set != 0) e *= texture(u_envmapmask, baseUV()).rgb;
        if (u_basealphaenvmapmask != 0) e *= 1.0 - base.a; // Source's model shader reads it inverted
        e *= u_envmaptint;
        e = mix(e, e * e, u_envmapcontrast);
        spec = mix(vec3(luminance(e)), e, u_envmapsaturation);
    }
    float alpha = (u_translucent != 0 ? base.a : 1.0) * u_alpha;
    return vec4(diffuse + spec, alpha);
}
