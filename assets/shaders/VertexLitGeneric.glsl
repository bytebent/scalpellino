// scalpellino shader 1
// name: VertexLitGeneric
// kind: material
// about: Source's model shader (props): the base texture times the prop's baked light (per
// about: vertex, or like a Source model: its ambient cube and 2 strongest lights with
// about: Half-Lambert), with glow, cutouts, see-through and chrome (probes or the sky); bump maps (the
// about: model's own normal map by itself), Phong and rim light from the prop's 2 strongest
// about: lights and its ambient cube (v0.12.4b), as Source SDK 2013's vertexlit / phong.
// setting: $basetexture texture | the picture
// setting: $bumpmap texture | bump map (a glTF model's own normal map is used by itself)
// setting: %flipgreen bool 0 | the bump map's green points the other way (glTF: set by itself)
// setting: $phong bool 0 | shine from the prop's 2 strongest lights (not shadowed, as Source)
// setting: $phongexponent float 5 | how tight the shine is (higher: smaller, sharper)
// setting: $phongexponenttexture texture | per-pixel exponent (red x 149 + 1), rim mask in alpha
// setting: $phongboost float 1 | shine strength
// setting: $phongtint color 1 1 1 | shine colour
// setting: $phongfresnelranges color 0 0.5 1 | shine facing you, at 45 degrees, at grazing angles
// setting: $basemapalphaphongmask bool 0 | the base texture's alpha masks the shine (else the bump map's)
// setting: $rimlight bool 0 | rim light: the ambient cube and lights behind, at grazing angles
// setting: $rimlightexponent float 4 | how tight the rim is
// setting: $rimlightboost float 1 | rim strength
// setting: $rimmask bool 0 | the exponent texture's alpha masks the rim
// setting: $color color 1 1 1 | tint (linear)
// setting: $selfillum bool 0 | glow: the base texture's alpha marks what glows
// setting: $selfillumtint color 1 1 1 | glow colour and strength
// setting: $selfillummask texture | glow mask instead of the base alpha (red channel)
// setting: $alphatest bool 0 | cutout
// setting: $alphatestreference float 0.5 | cutout threshold
// setting: $translucent bool 0 | see-through by the base alpha
// setting: $alpha float 1 | opacity
// setting: $nocull bool 0 | draw both sides
// setting: $envmap text | reflection: env_cubemap = the nearest reflection probes (no probes: the sky); any other value = the sky (cheap chrome)
// setting: $envmaptint color 1 1 1 | reflection colour and strength
// setting: $envmapmask texture | reflection mask (rgb)
// setting: $basealphaenvmapmask bool 0 | the base alpha masks the reflection (inverted, as Source's model shader)
// setting: $envmapcontrast float 0 | reflection contrast
// setting: $envmapsaturation float 1 | reflection saturation

// Source's Fresnel for Phong: 1 - N.V squared, through the 3 ranges.
float fresnelRanges(vec3 n, vec3 v, vec3 r) {
    float f = clamp(1.0 - dot(n, v), 0.0, 1.0);
    f *= f;
    return f > 0.5 ? mix(r.y, r.z, 2.0 * f - 1.0) : mix(r.x, r.y, 2.0 * f);
}

vec4 shade() {
    vec4 base = baseColor();
    if (u_alphatest != 0 && base.a < u_alphatestreference) discard;
    vec3 albedo = toLinear(base.rgb) * u_color;
    vec3 n = worldNormal();
    vec3 light;
    vec4 bump = vec4(0.5, 0.5, 1.0, 1.0);
    if (u_bumpmap_set != 0) {
        bump = texture(u_bumpmap, baseUV());
        vec3 nt = bumpNormal(bump, u_flipgreen != 0);
        light = bakedLightBumped(nt);
        n = tangentToWorld(nt);
    } else {
        light = bakedLight();
    }
    vec3 diffuse = albedo * light;
    if (u_selfillum != 0) {
        float glow = u_selfillummask_set != 0 ? texture(u_selfillummask, baseUV()).r : base.a;
        diffuse = mix(diffuse, albedo * u_selfillumtint, glow);
    }
    vec3 v = eyeVector();
    vec3 spec = vec3(0.0);
    if (u_envmap_set != 0) {
        vec3 e = envmap(reflect(-v, n));
        if (u_envmapmask_set != 0) e *= texture(u_envmapmask, baseUV()).rgb;
        if (u_basealphaenvmapmask != 0) e *= 1.0 - base.a; // Source's model shader reads it inverted
        e *= u_envmaptint;
        e = mix(e, e * e, u_envmapcontrast);
        spec = mix(vec3(luminance(e)), e, u_envmapsaturation);
    }
    // Phong and rim light (Source's SpecularAndRimTerms) from the prop's 2 lights
    if (u_phong != 0 || u_rimlight != 0) {
        vec4 ex = u_phongexponenttexture_set != 0 ? texture(u_phongexponenttexture, baseUV()) : vec4(0.0, 0.0, 0.0, 1.0);
        float expo = u_phongexponenttexture_set != 0 ? 1.0 + 149.0 * ex.r : u_phongexponent;
        float mask = u_basemapalphaphongmask != 0 ? base.a : (u_bumpmap_set != 0 ? bump.a : 1.0);
        vec3 r = reflect(-v, n);
        vec3 ph = vec3(0.0), rim = vec3(0.0);
        for (int i = 0; i < 2; ++i) {
            if (i >= modelLightCount()) break;
            vec3 L, col;
            modelLightAt(i, L, col);
            float ndl = clamp(dot(n, L), 0.0, 1.0);
            float ldr = clamp(dot(r, L), 0.0, 1.0);
            ph += col * pow(ldr, max(expo, 1.0)) * ndl;
            rim += col * pow(ldr, max(u_rimlightexponent, 1.0)) * ndl;
        }
        if (u_phong != 0) spec += ph * mask * fresnelRanges(n, v, u_phongfresnelranges) * u_phongboost * u_phongtint;
        if (u_rimlight != 0) {
            float rf = clamp(1.0 - dot(n, v), 0.0, 1.0);
            rf = rf * rf * rf * rf;   // Source's Fresnel4
            float rm = (u_rimmask != 0 ? ex.a : 1.0) * rf;
            spec += max(rim, ambientCube(v)) * rm * u_rimlightboost;
        }
    }
    float alpha = (u_translucent != 0 ? base.a : 1.0) * u_alpha;
    return vec4(diffuse + spec, alpha);
}
