// scalpellino shader 1
// name: UnlitGeneric
// kind: material
// about: Source's unlit shader: the texture as it is, no light (screens, signs, skies on
// about: brushes), with Source's detail texture, cutouts, see-through and cheap chrome.
// setting: $basetexture texture | the picture
// setting: $color color 1 1 1 | tint and brightness (linear)
// setting: $detail texture | detail texture (mod2x)
// setting: $detailscale float 4 | detail repeats per repeat of the base texture
// setting: $detailblendfactor float 1 | how much detail
// setting: $detailtint color 1 1 1 | detail tint
// setting: $alphatest bool 0 | cutout (also in the light bake)
// setting: $alphatestreference float 0.5 | cutout threshold
// setting: $translucent bool 0 | see-through by the base alpha
// setting: $alpha float 1 | opacity of the whole surface
// setting: $nocull bool 0 | draw both sides
// setting: $envmap text | reflection: env_cubemap = the sky (cheap chrome)
// setting: $envmaptint color 1 1 1 | reflection colour and strength
// setting: $envmapmask texture | reflection mask (rgb)
// setting: $envmapcontrast float 0 | reflection contrast
// setting: $envmapsaturation float 1 | reflection saturation
// setting: $reflectivity color | light bake: how much light it bounces
// setting: %texlight light | light bake: the surface gives off light ("r g b brightness")

vec4 shade() {
    vec4 base = baseColor();
    if (u_alphatest != 0 && base.a < u_alphatestreference) discard;
    vec3 albedo = toLinear(base.rgb) * u_color;
    if (u_detail_set != 0) {
        vec3 d = texture(u_detail, baseUV() * u_detailscale).rgb * u_detailtint;
        albedo *= mix(vec3(1.0), 2.0 * d, u_detailblendfactor);
    }
    vec3 spec = vec3(0.0);
    if (u_envmap_set != 0) {
        vec3 e = envmap(reflect(-eyeVector(), worldNormal()));
        if (u_envmapmask_set != 0) e *= texture(u_envmapmask, baseUV()).rgb;
        e *= u_envmaptint;
        e = mix(e, e * e, u_envmapcontrast);
        spec = mix(vec3(luminance(e)), e, u_envmapsaturation);
    }
    float alpha = (u_translucent != 0 ? base.a : 1.0) * u_alpha;
    return vec4(albedo + spec, alpha);
}
