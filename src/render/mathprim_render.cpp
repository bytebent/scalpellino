#include "render/mathprim_render.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace scal {

namespace {

// Vertex shader: the box around a shape, drawn from the inside (back faces); each pixel
// then finds the exact surface along its view ray.
const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 fragPos;
void main() {
    fragPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

// Same maths as core/mathprim.cpp, in floats.
const char* kFS = R"(#version 330
in vec3 fragPos;
uniform mat4 mvp;
uniform vec3 camPos;
uniform int orthoOn;   // orthographic camera: parallel rays along viewDir
uniform vec3 viewDir;
uniform int count;
uniform int kind[16];
uniform int op[16];
uniform vec3 ppos[16];
uniform vec3 rx[16];
uniform vec3 ry[16];
uniform vec3 rz[16];
uniform vec3 hs[16];
uniform vec4 param[16];   // x round, y thickness, z capsule cap, w texture repeats
uniform vec3 pcol[16];
uniform int slot[16];     // texture slot 0..3, -1 = flat colour
uniform float pblend[16]; // blend width (units) when joining the combination
uniform int blended;      // 1 = distance functions + ray marching (some piece blends)
uniform vec3 boxMin;      // the drawn box (marching starts where the ray enters it)
uniform vec3 boxMax;
uniform sampler2D texA;
uniform sampler2D texB;
uniform sampler2D texC;
uniform sampler2D texD;
uniform int lightMode;    // 0 = editor shading, 1 = rough light preview
uniform vec3 ambient;
uniform int lightCount;
uniform vec4 lpos[16];    // xyz, w = kind (0 point, 1 spot, 2 sun)
uniform vec4 lcol[16];    // rgb x brightness, w = reach
uniform vec4 ldir[16];    // direction, w = cos(outer)
uniform float lcin[16];   // cos(inner)
uniform int fogOn;
uniform vec3 fogColor;
uniform vec2 fogRange;
out vec4 finalColor;

const float INF = 1e20;
const float PI = 3.14159265;

struct Iv { float t0; float t1; vec3 n0; vec3 n1; int p0; int p1; };

// {t : a t^2 + b t + c <= 0} -> up to 2 stretches; returns how many.
int quadNP(float a, float b, float c, out vec2 s0, out vec2 s1) {
    s0 = vec2(0.0); s1 = vec2(0.0);
    if (abs(a) < 1e-9) {
        if (abs(b) < 1e-9) { if (c <= 0.0) { s0 = vec2(-INF, INF); return 1; } return 0; }
        float r = -c / b;
        s0 = b > 0.0 ? vec2(-INF, r) : vec2(r, INF);
        return 1;
    }
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) { if (a < 0.0) { s0 = vec2(-INF, INF); return 1; } return 0; }
    float s = sqrt(disc);
    float q = -0.5 * (b + (b >= 0.0 ? s : -s));
    float r1 = q / a;
    float r2 = abs(q) > 1e-20 ? c / q : r1;
    if (r1 > r2) { float t = r1; r1 = r2; r2 = t; }
    if (a > 0.0) { s0 = vec2(r1, r2); return 1; }
    s0 = vec2(-INF, r1); s1 = vec2(r2, INF);
    return 2;
}

// lo <= o + t d <= hi on one axis; normals along that axis.
bool slab(float o, float d, float lo, float hi, out vec2 t, out float sEnter) {
    if (abs(d) < 1e-9) { t = vec2(-INF, INF); sEnter = 0.0; return o >= lo && o <= hi; }
    float ta = (lo - o) / d, tb = (hi - o) / d;
    if (d > 0.0) { t = vec2(ta, tb); sEnter = -1.0; } else { t = vec2(tb, ta); sEnter = 1.0; }
    return true;
}

// Local stretches of one primitive (normals in its own space). Returns 0..2.
int sphereL(vec3 o, vec3 d, out Iv a) {
    vec2 s0, s1;
    int n = quadNP(dot(d, d), 2.0 * dot(o, d), dot(o, o) - 1.0, s0, s1);
    if (n == 0) return 0;
    a.t0 = s0.x; a.t1 = s0.y; a.n0 = o + d * s0.x; a.n1 = o + d * s0.y;
    return 1;
}

int cylL(vec3 o, vec3 d, float zh, out Iv a) {
    vec2 s0, s1, sz;
    float se;
    if (!slab(o.z, d.z, -zh, zh, sz, se)) return 0;
    int n = quadNP(d.x * d.x + d.y * d.y, 2.0 * (o.x * d.x + o.y * d.y), o.x * o.x + o.y * o.y - 1.0, s0, s1);
    if (n == 0) return 0;
    float t0 = max(s0.x, sz.x), t1 = min(s0.y, sz.y);
    if (t0 > t1) return 0;
    vec3 p0 = o + d * t0, p1 = o + d * t1;
    a.t0 = t0; a.t1 = t1;
    a.n0 = s0.x >= sz.x ? vec3(p0.xy, 0.0) : vec3(0.0, 0.0, se);
    a.n1 = s0.y <= sz.y ? vec3(p1.xy, 0.0) : vec3(0.0, 0.0, -se);
    return 1;
}

int coneL(vec3 o, vec3 d, out Iv a) {
    vec2 s0, s1, sz;
    float se;
    if (!slab(o.z, d.z, -1.0, 1.0, sz, se)) return 0;
    float w0 = 1.0 - o.z;
    int n = quadNP(d.x * d.x + d.y * d.y - d.z * d.z * 0.25, 2.0 * (o.x * d.x + o.y * d.y) + w0 * d.z * 0.5,
                   o.x * o.x + o.y * o.y - w0 * w0 * 0.25, s0, s1);
    for (int i = 0; i < 2; ++i) {
        if (i >= n) break;
        vec2 s = i == 0 ? s0 : s1;
        float t0 = max(s.x, sz.x), t1 = min(s.y, sz.y);
        if (t0 > t1) continue;
        vec3 p0 = o + d * t0, p1 = o + d * t1;
        a.t0 = t0; a.t1 = t1;
        a.n0 = s.x >= sz.x ? vec3(2.0 * p0.xy, (1.0 - p0.z) * 0.5) : vec3(0.0, 0.0, se);
        a.n1 = s.y <= sz.y ? vec3(2.0 * p1.xy, (1.0 - p1.z) * 0.5) : vec3(0.0, 0.0, -se);
        return 1;
    }
    return 0;
}

int ellL(vec3 o, vec3 d, float zc, float c, out Iv a) {
    float oz = (o.z - zc) / c, dz = d.z / c;
    vec2 s0, s1;
    int n = quadNP(d.x * d.x + d.y * d.y + dz * dz, 2.0 * (o.x * d.x + o.y * d.y + oz * dz),
                   o.x * o.x + o.y * o.y + oz * oz - 1.0, s0, s1);
    if (n == 0) return 0;
    vec3 p0 = o + d * s0.x, p1 = o + d * s0.y;
    a.t0 = s0.x; a.t1 = s0.y;
    a.n0 = vec3(p0.xy, (p0.z - zc) / (c * c));
    a.n1 = vec3(p1.xy, (p1.z - zc) / (c * c));
    return 1;
}

int capsuleL(vec3 o, vec3 d, float c, out Iv a) {
    float zc = 1.0 - c;
    Iv x;
    bool any = false;
    a.t0 = INF; a.t1 = -INF;
    for (int i = 0; i < 3; ++i) {
        int n = i == 0 ? ellL(o, d, zc, c, x) : i == 1 ? ellL(o, d, -zc, c, x) : (zc > 1e-6 ? cylL(o, d, zc, x) : 0);
        if (n == 0) continue;
        any = true;
        if (x.t0 < a.t0) { a.t0 = x.t0; a.n0 = x.n0; }
        if (x.t1 > a.t1) { a.t1 = x.t1; a.n1 = x.n1; }
    }
    return any ? 1 : 0;
}

int discL(vec3 o, vec3 d, out Iv a) {
    if (abs(d.z) < 1e-9) return 0;
    float t = -o.z / d.z;
    vec3 p = o + d * t;
    if (dot(p.xy, p.xy) > 1.0) return 0;
    vec3 n = vec3(0.0, 0.0, d.z > 0.0 ? -1.0 : 1.0);
    a.t0 = t; a.t1 = t; a.n0 = n; a.n1 = n;
    return 1;
}

float torusF(vec3 p, float R, float th) {
    float rho = length(p.xy) - R, qz = p.z * th;
    return rho * rho + qz * qz - th * th;
}
vec3 torusN(vec3 p, float R, float th) {
    float rho = max(1e-6, length(p.xy));
    float k = 1.0 - R / rho;
    return vec3(p.xy * k, p.z * th * th);
}

int torusL(vec3 o, vec3 d, float th, out Iv a, out Iv b) {
    vec2 tx, ty, tz;
    float se;
    if (!slab(o.x, d.x, -1.0, 1.0, tx, se) || !slab(o.y, d.y, -1.0, 1.0, ty, se) || !slab(o.z, d.z, -1.0, 1.0, tz, se))
        return 0;
    float lo = max(tx.x, max(ty.x, tz.x)), hi = min(tx.y, min(ty.y, tz.y));
    if (lo > hi) return 0;
    float R = 1.0 - th;
    float roots[4];
    int nr = 0;
    const int N = 96;
    float prevT = lo, prevF = torusF(o + d * lo, R, th);
    for (int i = 1; i <= N; ++i) {
        float t = lo + (hi - lo) * float(i) / float(N);
        float f = torusF(o + d * t, R, th);
        if ((f < 0.0) != (prevF < 0.0) && nr < 4) {
            float l = prevT, h = t, fl = prevF;
            for (int k = 0; k < 22; ++k) {
                float m = 0.5 * (l + h), fm = torusF(o + d * m, R, th);
                if ((fm < 0.0) == (fl < 0.0)) { l = m; fl = fm; } else h = m;
            }
            roots[nr] = 0.5 * (l + h);
            nr++;
        }
        prevT = t;
        prevF = f;
    }
    int n = 0;
    for (int i = 0; i + 1 < nr; i += 2) {
        Iv x;
        x.t0 = roots[i]; x.t1 = roots[i + 1];
        x.n0 = torusN(o + d * x.t0, R, th);
        x.n1 = torusN(o + d * x.t1, R, th);
        if (n == 0) a = x; else b = x;
        n++;
    }
    return n;
}

float rboxSdf(vec3 p, vec3 h, float r) {
    vec3 q = abs(p) - (h - r);
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}
vec3 rboxN(vec3 p, vec3 h, float r) {
    float e = 1e-3 * max(max(h.x, h.y), max(h.z, 1.0));
    return normalize(vec3(rboxSdf(p + vec3(e, 0, 0), h, r) - rboxSdf(p - vec3(e, 0, 0), h, r),
                          rboxSdf(p + vec3(0, e, 0), h, r) - rboxSdf(p - vec3(0, e, 0), h, r),
                          rboxSdf(p + vec3(0, 0, e), h, r) - rboxSdf(p - vec3(0, 0, e), h, r)));
}

// Cube in its rotated, unscaled frame (d unit length).
int cubeR(vec3 o, vec3 d, vec3 h, float r, out Iv a) {
    vec2 tx, ty, tz;
    float sx, sy, sz;
    if (!slab(o.x, d.x, -h.x, h.x, tx, sx) || !slab(o.y, d.y, -h.y, h.y, ty, sy) || !slab(o.z, d.z, -h.z, h.z, tz, sz))
        return 0;
    float t0 = max(tx.x, max(ty.x, tz.x)), t1 = min(tx.y, min(ty.y, tz.y));
    if (t0 > t1) return 0;
    if (r <= 1e-6) {
        a.t0 = t0; a.t1 = t1;
        a.n0 = t0 == tx.x ? vec3(sx, 0, 0) : t0 == ty.x ? vec3(0, sy, 0) : vec3(0, 0, sz);
        a.n1 = t1 == tx.y ? vec3(-sx, 0, 0) : t1 == ty.y ? vec3(0, -sy, 0) : vec3(0, 0, -sz);
        return 1;
    }
    float tol = 1e-4 * max(max(h.x, h.y), max(h.z, 1.0));
    float t = t0;
    bool hit = false;
    for (int i = 0; i < 96; ++i) {
        float s = rboxSdf(o + d * t, h, r);
        if (s < tol) { hit = true; break; }
        t += s;
        if (t > t1) break;
    }
    if (!hit) return 0;
    float u = t1;
    for (int i = 0; i < 96; ++i) {
        float s = rboxSdf(o + d * u, h, r);
        if (s < tol) break;
        u -= s;
    }
    a.t0 = t; a.t1 = u;
    a.n0 = rboxN(o + d * t, h, r);
    a.n1 = rboxN(o + d * u, h, r);
    return 1;
}

mat3 rotOf(int i) { return mat3(rx[i], ry[i], rz[i]); }

// ---- distance functions (same as core primDistance)
float dist(int i, vec3 wp) {
    mat3 R = rotOf(i);
    vec3 h = hs[i];
    vec3 p = transpose(R) * (wp - ppos[i]);
    float hmin = min(h.x, h.y);
    int k = kind[i];
    if (k == 0) {
        float k0 = length(p / h), k1 = length(p / (h * h));
        return k1 < 1e-9 ? -min(hmin, h.z) : k0 * (k0 - 1.0) / k1;
    }
    if (k == 1) return rboxSdf(p, h, param[i].x);
    if (k == 2) {
        float dr = (length(p.xy / h.xy) - 1.0) * hmin, dz = abs(p.z) - h.z;
        return min(max(dr, dz), 0.0) + length(max(vec2(dr, dz), 0.0));
    }
    if (k == 3) {
        vec3 u = p / h;
        float qx = length(u.xy), qy = u.z;
        float cax = qx - min(qx, qy < 0.0 ? 1.0 : 0.0), cay = abs(qy) - 1.0;
        float k2x = -1.0, k2y = 2.0;
        float t = clamp(((0.0 - qx) * k2x + (1.0 - qy) * k2y) / (k2x * k2x + k2y * k2y), 0.0, 1.0);
        float cbx = qx + k2x * t, cby = qy - 1.0 + k2y * t;
        float s = (cbx < 0.0 && cay < 0.0) ? -1.0 : 1.0;
        return s * sqrt(min(cax * cax + cay * cay, cbx * cbx + cby * cby)) * min(hmin, h.z);
    }
    if (k == 4) {
        float th = param[i].y;
        vec3 u = vec3(p.xy / h.xy, p.z / h.z * th);
        float ring = length(u.xy) - (1.0 - th);
        return (length(vec2(ring, u.z)) - th) * min(hmin, h.z / max(th, 1e-6));
    }
    if (k == 5) {
        float r = hmin;
        vec3 q = vec3(p.xy * r / h.xy, p.z);
        float zc = max(0.0, h.z - r);
        return length(vec3(q.xy, q.z - clamp(q.z, -zc, zc))) - r;
    }
    float out_ = max(length(p.xy / h.xy) - 1.0, 0.0) * hmin;
    return length(vec2(out_, p.z)) - 0.25;
}
float smin(float a, float b, float k) {
    if (k <= 0.0) return min(a, b);
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
float smax(float a, float b, float k) { return -smin(-a, -b, k); }
float sceneDist(vec3 p) {
    float d = 1e20;
    bool first = true;
    for (int i = 0; i < count; ++i)
        if (op[i] == 0) { float di = dist(i, p); d = first ? di : smin(d, di, pblend[i]); first = false; }
    for (int i = 0; i < count; ++i)
        if (op[i] == 1) d = smax(d, -dist(i, p), pblend[i]);
    for (int i = 0; i < count; ++i)
        if (op[i] == 2) d = smax(d, dist(i, p), pblend[i]);
    return d;
}

// ---- a piece's own colour at a point (texture mapped by its kind)
vec3 pieceBase(int who, vec3 p, vec3 n) {
    mat3 R = rotOf(who);
    vec3 lp = transpose(R) * (p - ppos[who]) / hs[who];
    vec3 ln = transpose(R) * n;
    vec2 uv;
    int k = kind[who];
    if (k == 0) uv = vec2(atan(lp.y, lp.x) / (2.0 * PI) + 0.5, acos(clamp(lp.z, -1.0, 1.0)) / PI);
    else if (k == 1) {
        vec3 an = abs(ln);
        uv = an.z >= an.x && an.z >= an.y ? lp.xy : an.x >= an.y ? lp.yz : lp.xz;
        uv = uv * 0.5 + 0.5;
    } else if (k == 4) {
        float th = param[who].y;
        uv = vec2(atan(lp.y, lp.x) / (2.0 * PI) + 0.5, atan(lp.z * th, length(lp.xy) - (1.0 - th)) / (2.0 * PI) + 0.5);
    } else if (k == 6 || ((k == 2 || k == 3) && abs(ln.z) > 0.999)) uv = lp.xy * 0.5 + 0.5;
    else uv = vec2(atan(lp.y, lp.x) / (2.0 * PI) + 0.5, 0.5 - lp.z * 0.5);
    uv *= param[who].w;
    vec3 base = pcol[who] / 255.0;
    int s = slot[who];
    if (s == 0) base *= texture(texA, uv).rgb;
    else if (s == 1) base *= texture(texB, uv).rgb;
    else if (s == 2) base *= texture(texC, uv).rgb;
    else if (s == 3) base *= texture(texD, uv).rgb;
    return base;
}

// World stretches of member i (up to 2).
int memberIv(int i, vec3 o, vec3 d, out Iv a, out Iv b) {
    mat3 R = rotOf(i);
    vec3 h = hs[i];
    vec3 ro = transpose(R) * (o - ppos[i]), rd = transpose(R) * d;
    int n = 0;
    bool rotated = kind[i] == 1;
    if (rotated) n = cubeR(ro, rd, h, param[i].x, a);
    else {
        vec3 ol = ro / h, dl = rd / h;
        if (kind[i] == 0) n = sphereL(ol, dl, a);
        else if (kind[i] == 2) n = cylL(ol, dl, 1.0, a);
        else if (kind[i] == 3) n = coneL(ol, dl, a);
        else if (kind[i] == 4) n = torusL(ol, dl, param[i].y, a, b);
        else if (kind[i] == 5) n = capsuleL(ol, dl, param[i].z, a);
        else if (kind[i] == 6) n = discL(ol, dl, a);
    }
    a.p0 = i; a.p1 = i; b.p0 = i; b.p1 = i;
    if (rotated) {
        a.n0 = normalize(R * a.n0); a.n1 = normalize(R * a.n1);
    } else {
        a.n0 = normalize(R * (a.n0 / h)); a.n1 = normalize(R * (a.n1 / h));
        if (n > 1) { b.n0 = normalize(R * (b.n0 / h)); b.n1 = normalize(R * (b.n1 / h)); }
    }
    return n;
}

const int MAXI = 8;

uniform vec2 fogHeight; // the fog's base and thickness (thickness 0: everywhere)
// How fogged a point is: the stretch of the line of sight inside the fog layer, through the
// fog's start and end distances (as Sky Lab's fog in the sky).
float fogAmount(vec3 p) {
    float d = length(p - camPos);
    float path = d;
    if (fogHeight.y > 0.0) {
        float lo = fogHeight.x, hi = fogHeight.x + fogHeight.y, z0 = camPos.z, z1 = p.z;
        if (abs(z1 - z0) < 0.001) path = (z0 >= lo && z0 <= hi) ? d : 0.0;
        else {
            float a = clamp((lo - z0) / (z1 - z0), 0.0, 1.0), b = clamp((hi - z0) / (z1 - z0), 0.0, 1.0);
            path = abs(b - a) * d;
        }
    }
    return clamp((path - fogRange.x) / max(fogRange.y - fogRange.x, 1.0), 0.0, 1.0);
}
void main() {
    vec3 o = camPos;
    vec3 d = normalize(fragPos - camPos);
    // Orthographic: parallel rays starting on the camera's plane (not far away: the
    // intersections lose precision in floats).
    if (orthoOn == 1) { d = viewDir; o = fragPos - viewDir * dot(fragPos - camPos, viewDir); }
    float best = INF;
    vec3 n = vec3(0.0);
    int who = -1;
    vec3 base = vec3(1.0);
    if (blended == 1) {
        // Ray marching through the distance field, from where the ray enters the box.
        vec3 inv = 1.0 / d;
        vec3 ta = (boxMin - o) * inv, tb = (boxMax - o) * inv;
        vec3 tlo = min(ta, tb), thi = max(ta, tb);
        float t0 = max(max(tlo.x, tlo.y), max(tlo.z, 2.0)), t1 = min(min(thi.x, thi.y), thi.z);
        if (t0 > t1) discard;
        float t = t0;
        float eps = 1e-4 * length(boxMax - boxMin);
        if (sceneDist(o + d * t) < 0.0) discard; // inside: nothing, like brushes
        bool hit = false;
        for (int i = 0; i < 256; ++i) {
            float dd = sceneDist(o + d * t);
            if (dd < eps * (1.0 + t * 0.001)) { hit = true; break; }
            t += max(dd * 0.9, eps * 0.5);
            if (t > t1) break;
        }
        if (!hit) discard;
        best = t;
        vec3 p = o + d * t;
        float e = max(eps * 4.0, 0.01);
        n = normalize(vec3(sceneDist(p + vec3(e, 0, 0)) - sceneDist(p - vec3(e, 0, 0)),
                           sceneDist(p + vec3(0, e, 0)) - sceneDist(p - vec3(0, e, 0)),
                           sceneDist(p + vec3(0, 0, e)) - sceneDist(p - vec3(0, 0, e))));
        // Colour: the pieces near this point, weighted by how close their own surface is
        // (within their blend width), so materials melt across the joint too.
        vec3 sum = vec3(0.0);
        float wsum = 0.0, nearest = 1e20, kmax = 1e-3;
        for (int i = 0; i < count; ++i) kmax = max(kmax, pblend[i]);
        for (int i = 0; i < count; ++i) {
            float di = abs(dist(i, p));
            if (di < nearest) { nearest = di; who = i; }
            float k = kmax;
            float w = max(0.0, 1.0 - di / k);
            if (w > 0.0) { w = w * w; sum += pieceBase(i, p, n) * w; wsum += w; }
        }
        base = wsum > 0.0 ? sum / wsum : pieceBase(who, p, n);
    } else {
    Iv cur[MAXI];
    int nc = 0;
    Iv a, b;
    // Union of the Add pieces (a lone primitive always counts as Add).
    for (int i = 0; i < count; ++i) {
        if (op[i] != 0 && count > 1) continue;
        int n = memberIv(i, o, d, a, b);
        for (int k = 0; k < n; ++k) {
            Iv x = k == 0 ? a : b;
            // insert, merging overlaps
            bool merged = false;
            for (int j = 0; j < nc; ++j) {
                if (x.t0 <= cur[j].t1 && x.t1 >= cur[j].t0) {
                    if (x.t0 < cur[j].t0) { cur[j].t0 = x.t0; cur[j].n0 = x.n0; cur[j].p0 = x.p0; }
                    if (x.t1 > cur[j].t1) { cur[j].t1 = x.t1; cur[j].n1 = x.n1; cur[j].p1 = x.p1; }
                    merged = true;
                    break;
                }
            }
            if (!merged && nc < MAXI) { cur[nc] = x; nc++; }
        }
    }
    // Subtract pieces cut stretches out.
    for (int i = 0; i < count && count > 1; ++i) {
        if (op[i] != 1) continue;
        int n = memberIv(i, o, d, a, b);
        for (int k = 0; k < n; ++k) {
            Iv c = k == 0 ? a : b;
            if (c.t1 <= c.t0) continue;
            Iv nxt[MAXI];
            int nn = 0;
            for (int j = 0; j < nc; ++j) {
                Iv x = cur[j];
                if (c.t1 <= x.t0 || c.t0 >= x.t1) { if (nn < MAXI) { nxt[nn] = x; nn++; } continue; }
                if (c.t0 > x.t0 && nn < MAXI) { Iv l = x; l.t1 = c.t0; l.n1 = -c.n0; l.p1 = c.p0; nxt[nn] = l; nn++; }
                if (c.t1 < x.t1 && nn < MAXI) { Iv r = x; r.t0 = c.t1; r.n0 = -c.n1; r.p0 = c.p1; nxt[nn] = r; nn++; }
            }
            for (int j = 0; j < nn; ++j) cur[j] = nxt[j];
            nc = nn;
        }
    }
    // Intersect pieces keep only what is inside them.
    for (int i = 0; i < count && count > 1; ++i) {
        if (op[i] != 2) continue;
        int n = memberIv(i, o, d, a, b);
        Iv nxt[MAXI];
        int nn = 0;
        for (int j = 0; j < nc; ++j)
            for (int k = 0; k < n; ++k) {
                Iv y = k == 0 ? a : b;
                Iv r = cur[j];
                if (y.t0 > r.t0) { r.t0 = y.t0; r.n0 = y.n0; r.p0 = y.p0; }
                if (y.t1 < r.t1) { r.t1 = y.t1; r.n1 = y.n1; r.p1 = y.p1; }
                if (r.t0 <= r.t1 && nn < MAXI) { nxt[nn] = r; nn++; }
            }
        for (int j = 0; j < nn; ++j) cur[j] = nxt[j];
        nc = nn;
    }
    // Nearest entry in front of the camera (from inside a solid: nothing, like brushes).
    for (int j = 0; j < nc; ++j)
        if (cur[j].t0 >= 2.0 && cur[j].t0 < best) { best = cur[j].t0; n = cur[j].n0; who = cur[j].p0; }
    if (who < 0) discard;
    if (dot(n, d) > 0.0) n = -n; // discs and cut faces: face the viewer
    base = pieceBase(who, o + d * best, n);
    }
    vec3 p = o + d * best;
    vec4 clip = mvp * vec4(p, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);

    vec3 c;
    if (lightMode == 0) {
        float kk = 0.5 + 0.5 * max(0.0, dot(n, normalize(vec3(0.35, 0.55, 0.85))));
        if (n.z < -0.5) kk = 0.42;
        c = base * kk;
    } else {
        vec3 L = ambient;
        vec3 q = p + n * 0.5;
        for (int i = 0; i < lightCount; ++i) {
            if (lpos[i].w > 1.5) { L += lcol[i].rgb * max(0.0, dot(n, -ldir[i].xyz)); continue; }
            vec3 dl = lpos[i].xyz - q;
            float dist = length(dl);
            float range = lcol[i].w;
            if (dist >= range || range <= 0.0) continue;
            vec3 to = dist > 1e-6 ? dl / dist : n;
            float f = 1.0 - dist / range;
            float kk = max(0.0, dot(n, to)) * f * f;
            if (lpos[i].w > 0.5) {
                float ca = dot(-to, ldir[i].xyz), co = ldir[i].w, ci = lcin[i];
                float t = ci - co > 1e-6 ? clamp((ca - co) / (ci - co), 0.0, 1.0) : (ca >= co ? 1.0 : 0.0);
                kk *= t * t * (3.0 - 2.0 * t);
            }
            L += lcol[i].rgb * kk;
        }
        c = min(base * L, vec3(1.0));
    }
    if (fogOn == 1) {
        float f = fogAmount(p);
        c = mix(c, fogColor, f);
    }
    finalColor = vec4(c, 1.0);
})";

struct PrimShader {
    Shader sh{};
    bool tried = false, ok = false;
    std::map<std::string, int> loc;
    int at(const char* n) {
        auto it = loc.find(n);
        if (it != loc.end()) return it->second;
        const int l = GetShaderLocation(sh, n);
        loc[n] = l;
        return l;
    }
};

PrimShader& primShader() {
    static PrimShader s;
    if (!s.tried) {
        s.tried = true;
        s.sh = LoadShaderFromMemory(kVS, kFS);
        s.ok = s.sh.id != 0 && s.sh.id != rlGetShaderIdDefault();
    }
    return s;
}

void drawBox(const AABB& b) {
    const Vec3 lo = b.min, hi = b.max;
    const Vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                       {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (const auto& q : f)
        for (int k = 0; k < 4; ++k) rlVertex3f((float)c[q[k]].x, (float)c[q[k]].y, (float)c[q[k]].z);
    rlEnd();
}
} // namespace

bool mathPrimShaderOk() { return primShader().ok; }

void drawMathPrims3D(const std::vector<MathPrim>& prims, const PrimDrawContext& ctx) {
    if (prims.empty()) return;
    PrimShader& S = primShader();
    if (!S.ok) return;
    // Group: combinations together, lone primitives alone.
    std::vector<std::vector<const MathPrim*>> groups;
    std::map<std::uint32_t, size_t> comboAt;
    for (const MathPrim& p : prims) {
        if (p.combo == 0) { groups.push_back({&p}); continue; }
        auto it = comboAt.find(p.combo);
        if (it == comboAt.end()) { comboAt[p.combo] = groups.size(); groups.push_back({&p}); }
        else groups[it->second].push_back(&p);
    }
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    BeginShaderMode(S.sh);
    auto setI = [&](const char* n, int v) { SetShaderValue(S.sh, S.at(n), &v, SHADER_UNIFORM_INT); };
    auto set3 = [&](const char* n, const Vec3& v) {
        const float f[3] = {(float)v.x, (float)v.y, (float)v.z};
        SetShaderValue(S.sh, S.at(n), f, SHADER_UNIFORM_VEC3);
    };
    set3("camPos", ctx.camPos);
    setI("orthoOn", ctx.orthoDir ? 1 : 0);
    set3("viewDir", ctx.orthoDir ? *ctx.orthoDir : Vec3{1, 0, 0});
    setI("lightMode", ctx.lightPreview ? 1 : 0);
    set3("ambient", ctx.ambient / 255.0);
    {
        float lp[64] = {}, lc[64] = {}, ld[64] = {}, li[16] = {};
        int n = 0;
        for (const Light& l : ctx.lights) {
            if (n >= 16) break;
            const Vec3 dir = lightDirection(l);
            const double k = l.brightness / 200.0 / 255.0;
            lp[4 * n] = (float)l.pos.x; lp[4 * n + 1] = (float)l.pos.y; lp[4 * n + 2] = (float)l.pos.z;
            lp[4 * n + 3] = (float)(int)l.kind;
            lc[4 * n] = (float)(l.color.x * k); lc[4 * n + 1] = (float)(l.color.y * k); lc[4 * n + 2] = (float)(l.color.z * k);
            lc[4 * n + 3] = (float)l.range;
            ld[4 * n] = (float)dir.x; ld[4 * n + 1] = (float)dir.y; ld[4 * n + 2] = (float)dir.z;
            ld[4 * n + 3] = (float)std::cos(l.outer * 3.14159265358979 / 180);
            li[n] = (float)std::cos(std::min(l.inner, l.outer) * 3.14159265358979 / 180);
            ++n;
        }
        setI("lightCount", n);
        SetShaderValueV(S.sh, S.at("lpos"), lp, SHADER_UNIFORM_VEC4, 16);
        SetShaderValueV(S.sh, S.at("lcol"), lc, SHADER_UNIFORM_VEC4, 16);
        SetShaderValueV(S.sh, S.at("ldir"), ld, SHADER_UNIFORM_VEC4, 16);
        SetShaderValueV(S.sh, S.at("lcin"), li, SHADER_UNIFORM_FLOAT, 16);
    }
    setI("fogOn", ctx.fog ? 1 : 0);
    set3("fogColor", ctx.fogColor / 255.0);
    {
        const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd};
        SetShaderValue(S.sh, S.at("fogRange"), fr, SHADER_UNIFORM_VEC2);
        const float fh[2] = {(float)ctx.fogBase, (float)ctx.fogThickness};
        SetShaderValue(S.sh, S.at("fogHeight"), fh, SHADER_UNIFORM_VEC2);
    }
    static const char* texNames[4] = {"texA", "texB", "texC", "texD"};
    for (const auto& g : groups) {
        const int n = (int)std::min<size_t>(g.size(), 16);
        int kind[16] = {}, op[16] = {}, slot[16] = {};
        float pos[48] = {}, rx[48] = {}, ry[48] = {}, rz[48] = {}, hs[48] = {}, prm[64] = {}, col[48] = {}, bl[16] = {};
        int blended = 0;
        double pad = 0;
        std::vector<std::string> slots;
        AABB box;
        bool anyAdd = false;
        for (int i = 0; i < n; ++i) {
            const MathPrim& p = *g[i];
            kind[i] = (int)p.kind;
            op[i] = (int)p.op;
            const Mat3 R = rotationMatrix(p.rot);
            for (int a = 0; a < 3; ++a) {
                pos[3 * i + a] = (float)p.pos[a];
                rx[3 * i + a] = (float)R.col[0][a];
                ry[3 * i + a] = (float)R.col[1][a];
                rz[3 * i + a] = (float)R.col[2][a];
                hs[3 * i + a] = (float)std::max(std::fabs(p.size[a] * 0.5), 1e-3);
                col[3 * i + a] = (float)p.color[a];
            }
            const double hmin = std::min({std::fabs(p.size.x), std::fabs(p.size.y)}) * 0.5;
            prm[4 * i] = (float)std::clamp(p.round, 0.0, std::min({hs[3 * i], hs[3 * i + 1], hs[3 * i + 2]}) * 1.0);
            prm[4 * i + 1] = (float)std::clamp(p.thickness, 0.02, 0.5);
            prm[4 * i + 2] = (float)std::clamp(hmin / std::max(std::fabs(p.size.z) * 0.5, 1e-3), 1e-3, 1.0);
            prm[4 * i + 3] = (float)p.texScale;
            bl[i] = n > 1 ? (float)std::max(0.0, p.blend) : 0.0f;
            if (bl[i] > 0) blended = 1;
            pad = std::max(pad, (double)bl[i]);
            slot[i] = -1;
            if (ctx.textures && !p.texture.empty()) {
                auto it = std::find(slots.begin(), slots.end(), p.texture);
                if (it != slots.end()) slot[i] = (int)(it - slots.begin());
                else if (slots.size() < 4) { slot[i] = (int)slots.size(); slots.push_back(p.texture); }
            }
            if (p.op == PrimOp::Add || n == 1) {
                const AABB pb = primBounds(p);
                box.add(pb.min);
                box.add(pb.max);
                anyAdd = true;
            }
        }
        if (!anyAdd) continue;
        // A little bigger, so the box's faces never cut the surface.
        // (blends can grow the shape by up to their width)
        const Vec3 grow = box.size() * 0.01 + Vec3{1, 1, 1} * (1 + pad);
        box.min -= grow;
        box.max += grow;
        setI("blended", blended);
        set3("boxMin", box.min);
        set3("boxMax", box.max);
        SetShaderValueV(S.sh, S.at("pblend"), bl, SHADER_UNIFORM_FLOAT, 16);
        setI("count", n);
        SetShaderValueV(S.sh, S.at("kind"), kind, SHADER_UNIFORM_INT, 16);
        SetShaderValueV(S.sh, S.at("op"), op, SHADER_UNIFORM_INT, 16);
        SetShaderValueV(S.sh, S.at("slot"), slot, SHADER_UNIFORM_INT, 16);
        SetShaderValueV(S.sh, S.at("ppos"), pos, SHADER_UNIFORM_VEC3, 16);
        SetShaderValueV(S.sh, S.at("rx"), rx, SHADER_UNIFORM_VEC3, 16);
        SetShaderValueV(S.sh, S.at("ry"), ry, SHADER_UNIFORM_VEC3, 16);
        SetShaderValueV(S.sh, S.at("rz"), rz, SHADER_UNIFORM_VEC3, 16);
        SetShaderValueV(S.sh, S.at("hs"), hs, SHADER_UNIFORM_VEC3, 16);
        SetShaderValueV(S.sh, S.at("param"), prm, SHADER_UNIFORM_VEC4, 16);
        SetShaderValueV(S.sh, S.at("pcol"), col, SHADER_UNIFORM_VEC3, 16);
        for (size_t t = 0; t < 4; ++t) {
            const Texture2D& tex = t < slots.size() ? ctx.textures->get(slots[t]) : Texture2D{rlGetTextureIdDefault(), 1, 1, 1, 7};
            SetShaderValueTexture(S.sh, S.at(texNames[t]), tex);
        }
        // Inside the box: the camera still sees its back faces, so the shape stays visible.
        drawBox(box);
        rlDrawRenderBatchActive();
    }
    EndShaderMode();
    rlSetCullFace(RL_CULL_FACE_BACK);
}

} // namespace scal
