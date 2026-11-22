// scalpellino shader 1
// name: DecalModulate
// kind: material
// blend: modulate
// about: Source's DecalModulate (v0.12.5): a decal that darkens or lightens what is under it
// about: (dirt, stains, cracks, scorch marks). The picture multiplies the surface twice over:
// about: 50% grey changes nothing, darker darkens, lighter lightens; its alpha fades it to grey.
// about: No light of its own: the surface's light shows through.
// setting: $basetexture texture | the picture (50% grey = no change)
// setting: $decalscale float 1 | Hammer units per texel when a decal is placed (size)

vec4 shade() {
    vec4 t = baseColor();
    return vec4(mix(vec3(0.5), t.rgb, t.a), 1.0);
}
