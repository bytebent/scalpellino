// scalpellino shader 1
// name: Clouds
// kind: material
// about: A cloud layer for an endless plane (Scalpellino's own, from Bryce's cloud planes):
// about: see-through cloud shapes made from noise across the plane, lit by the sun from above
// about: and darker underneath where they are thick. Separate from Sky Lab's clouds (which
// about: are painted into the sky picture); only the settings are alike. In the bake a cloud
// about: plane dims the sun by how thick it is above each point (its shadows on the map).
// about: Drawn with the see-through faces. The editor shows the material's %tooltexture.
// setting: $cover float 50 | how much of the sky they cover (0..100)
// setting: $cloudsize float 4000 | the size of a formation (cm)
// setting: $softness float 40 | soft (100) or hard (0) edges
// setting: $complexity int 5 | detail: layers of noise (1..8)
// setting: $opacity float 0.9 | how solid the thickest part is (0..1); also how much sun it stops
// setting: $seed int 1 | another number, another pattern
// setting: $color color 1 1 1 | the colour lit by the sun (gamma)
// setting: $shadowcolor color 0.72 0.74 0.8 | the colour of their thick undersides (gamma)

vec4 shade() {
    vec4 p = vec4(u_cloudsize, u_cover, u_softness, u_opacity);
    ivec2 q = ivec2(clamp(u_complexity, 1, 8), u_seed);
    float th = cloudThickness(worldPos().xy, p, q);
    // far away a pixel covers many formations: their average instead of a flicker
    float across = length(fwidth(worldPos().xy)) / max(u_cloudsize, 10.0);
    th = mix(th, 0.75 * clamp(u_cover / 100.0, 0.0, 1.0), smoothstep(0.08, 0.6, across));
    float a = clamp(th * u_opacity, 0.0, 1.0);
    if (a < 0.003) discard;
    vec3 light = bakedLight(); // the sun (dimmed by cloud planes above) and the sky
    vec3 lit = toLinear(u_color) * light;
    bool fromBelow = eyeVector().z < 0.0;
    vec3 c;
    if (fromBelow) {
        // seen from below: the sun comes through what is thin; the thick parts are dark
        vec3 under = toLinear(u_shadowcolor) * light;
        c = mix(lit, under, smoothstep(0.3, 1.0, th));
    } else {
        // from above: lit tops, a little shading where they thin out
        c = lit * (0.82 + 0.18 * th);
    }
    return vec4(c, a);
}
