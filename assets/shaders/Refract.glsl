// scalpellino shader 1
// name: Refract
// kind: material
// about: Source's bending glass (SDK 2013's refract): what is behind the face, taken from
// about: the picture drawn so far (a copy made just before the face is drawn, as Source's
// about: frame buffer copy), shifted by the normal map's ripples and tinted, with an optional
// about: 4-tap blur and a reflection ($envmap: the probes or the sky) on top. Like Source it
// about: can only bend what is already in the picture: near the picture's edges it smears.
// about: Drawn with the see-through faces (far to near); lets light through in the bake.
// about: On terrain and props (no copy there) it shows the probes straight through.
// setting: $normalmap texture | the ripples (normal map; its alpha scales them and the reflection)
// setting: $normalmap2 texture | a second normal map, added to the first
// setting: $refractamount float 2 | how far the ripples shift what is behind (picture widths)
// setting: $refracttint color 1 1 1 | tint of what is behind (as Source: gamma)
// setting: $refracttinttexture texture | tint picture (times 2 x $refracttint)
// setting: $bluramount float 0 | 1: blur what is behind a little (Source's 0 or 1)
// setting: $fadeoutonsilhouette bool 0 | seen edge-on, show what is behind unbent and untinted
// setting: $envmap text | reflection: env_cubemap = the nearest reflection probes (no probes: the sky); any other value = the sky (cheap chrome)
// setting: $envmaptint color 1 1 1 | reflection colour and strength
// setting: $envmapcontrast float 0 | reflection contrast
// setting: $envmapsaturation float 1 | reflection saturation
// setting: $reflectivity color | light bake: how much light it bounces

vec4 shade() {
    vec4 nm = vec4(0.5, 0.5, 1.0, 1.0);
    if (u_normalmap_set != 0) nm = texture(u_normalmap, baseUV());
    vec3 n = normalize(nm.rgb * 2.0 - 1.0);
    if (u_normalmap2_set != 0) n = normalize(n + normalize(texture(u_normalmap2, baseUV()).rgb * 2.0 - 1.0));
    vec3 tint = toLinear(u_refracttint);
    if (u_refracttinttexture_set != 0) tint = 2.0 * tint * toLinear(texture(u_refracttinttexture, baseUV()).rgb);
    vec2 off = n.xy * nm.a * u_refractamount;
    float blend = 1.0;
    if (u_fadeoutonsilhouette != 0) {
        blend = clamp(dot(eyeVector(), worldNormal()), 0.0, 1.0);
        blend = blend * blend * blend;
    }
    vec3 warp;
    if (u_bluramount >= 0.5) { // Source's 9 taps in 4 (1/512 of the picture apart)
        float b = 1.0 / 512.0, hb = 0.5 * b;
        warp = screenBehind(off + vec2(-hb, -hb)) * 0.4444444 + screenBehind(off + vec2(b, -hb)) * 0.2222222 +
               screenBehind(off + vec2(-hb, b)) * 0.2222222 + screenBehind(off + vec2(b, b)) * 0.1111111;
    } else {
        warp = screenBehind(off);
    }
    vec3 result = mix(screenBehind(vec2(0.0)), warp * tint, blend);
    if (u_envmap_set != 0) {
        vec3 e = envmap(reflect(-eyeVector(), tangentToWorld(n))) * nm.a * u_envmaptint;
        e = mix(e, e * e, u_envmapcontrast);
        result += mix(vec3(luminance(e)), e, u_envmapsaturation);
    }
    return vec4(result, 1.0); // what is behind is in the picture already: it replaces it
}
