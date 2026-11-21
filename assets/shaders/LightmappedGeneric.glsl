// scalpellino shader 1
// name: LightmappedGeneric
// kind: material
// about: Source's world shader (brushes and terrain): the base texture times the baked
// about: light, with Source's detail texture (mod2x), glow ($selfillum), cutouts
// about: ($alphatest), see-through ($translucent, $alpha) and cheap chrome ($envmap: the
// about: sky picture until the reflection probes, with mask, tint, contrast, saturation
// about: and Fresnel), as in Source SDK 2013's lightmappedgeneric. Bump maps (v0.12.4b):
// about: radiosity normal mapping over the face's 3 bump lightmaps, and SSBump.
// setting: $basetexture texture | the picture
// setting: $bumpmap texture | bump map (normal map, Source's convention: green down; or an SSBump)
// setting: $ssbump bool 0 | the bump map is an SSBump (self-shadowed: it holds the 3 weights)
// setting: %flipgreen bool 0 | the bump map's green points the other way (OpenGL / most tools)
// setting: $normalmapalphaenvmapmask bool 0 | the bump map's alpha masks the reflection
// setting: $color color 1 1 1 | tint, multiplies the texture (linear)
// setting: $detail texture | detail texture, laid over the base (mod2x: mid grey = no change)
// setting: $detailscale float 4 | detail repeats per repeat of the base texture
// setting: $detailblendfactor float 1 | how much detail (0 none, 1 full)
// setting: $detailtint color 1 1 1 | detail tint
// setting: $selfillum bool 0 | glow: the base texture's alpha marks what glows (no light needed)
// setting: $selfillumtint color 1 1 1 | glow colour and strength
// setting: $selfillummask texture | glow mask instead of the base alpha (red channel)
// setting: $alphatest bool 0 | cutout: pixels under the reference are holes (also in the light bake)
// setting: $alphatestreference float 0.5 | cutout threshold (alpha 0..1)
// setting: $translucent bool 0 | see-through by the base alpha (doesn't block light in the bake)
// setting: $alpha float 1 | opacity of the whole surface (under 1 = see-through)
// setting: $nocull bool 0 | draw both sides
// setting: $envmap text | reflection: env_cubemap = the sky (cheap chrome; the nearest probe later)
// setting: $envmaptint color 1 1 1 | reflection colour and strength
// setting: $envmapmask texture | reflection mask (rgb)
// setting: $basealphaenvmapmask bool 0 | the base texture's alpha masks the reflection
// setting: $envmapcontrast float 0 | 0 = as is, 1 = reflection squared (darker, punchier)
// setting: $envmapsaturation float 1 | 0 = grey reflection, 1 = full colour
// setting: $fresnelreflection float 1 | 1 = the same at every angle, 0 = only at grazing angles
// setting: $reflectivity color | light bake: how much light it bounces (default the texture's average)
// setting: %texlight light | light bake: the surface gives off light ("r g b brightness", as lights.rad)

vec4 shade() {
    vec4 base = baseColor();
    if (u_alphatest != 0 && base.a < u_alphatestreference) discard;
    vec3 albedo = toLinear(base.rgb) * u_color;
    if (u_detail_set != 0) {
        // Source's $detailblendmode 0 (mod2x), read without the sRGB curve as Source does
        vec3 d = texture(u_detail, baseUV() * u_detailscale).rgb * u_detailtint;
        albedo *= mix(vec3(1.0), 2.0 * d, u_detailblendfactor);
    }
    // the light: flat, or through the bump map (radiosity normal mapping / SSBump)
    vec3 light;
    vec3 n = worldNormal();
    vec4 bump = vec4(0.5, 0.5, 1.0, 1.0);
    if (u_bumpmap_set != 0) {
        bump = texture(u_bumpmap, baseUV());
        if (u_ssbump != 0) {
            light = bakedLightSSBump(bump.rgb);
        } else {
            vec3 nt = bumpNormal(bump, u_flipgreen != 0);
            light = bakedLightBumped(nt);
            n = tangentToWorld(nt);
        }
    } else {
        light = bakedLight();
    }
    vec3 diffuse = albedo * light;
    if (u_selfillum != 0) {
        float glow = u_selfillummask_set != 0 ? texture(u_selfillummask, baseUV()).r : base.a;
        diffuse = mix(diffuse, albedo * u_selfillumtint, glow);
    }
    vec3 spec = vec3(0.0);
    if (u_envmap_set != 0) {
        vec3 v = eyeVector();
        vec3 e = envmap(reflect(-v, n));
        if (u_envmapmask_set != 0) e *= texture(u_envmapmask, baseUV()).rgb;
        if (u_basealphaenvmapmask != 0) e *= base.a;
        if (u_normalmapalphaenvmapmask != 0 && u_bumpmap_set != 0) e *= bump.a;
        e *= u_envmaptint;
        e = mix(e, e * e, u_envmapcontrast);
        e = mix(vec3(luminance(e)), e, u_envmapsaturation);
        float f = 1.0 - max(dot(n, v), 0.0);
        f = f * f * f * f * f;
        spec = e * (u_fresnelreflection + (1.0 - u_fresnelreflection) * f);
    }
    float alpha = (u_translucent != 0 ? base.a : 1.0) * u_alpha;
    return vec4(diffuse + spec, alpha);
}
