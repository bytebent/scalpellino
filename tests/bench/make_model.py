#!/usr/bin/env python3
"""Writes a plain .glb sphere-ish rock with about N triangles (for the prop benchmark).
Usage: make_model.py <out.glb> <triangles>"""
import json, math, struct, sys

out, want = sys.argv[1], int(sys.argv[2])
seg = max(4, int(math.sqrt(want / 2)))
rings = seg
pos, nrm, idx = [], [], []
for r in range(rings + 1):
    lat = math.pi * r / rings
    for s in range(seg + 1):
        lon = 2 * math.pi * s / seg
        x, y, z = math.sin(lat) * math.cos(lon), math.cos(lat), math.sin(lat) * math.sin(lon)
        bump = 1 + 0.08 * math.sin(5 * lon) * math.sin(4 * lat)  # not a perfect sphere
        pos += [0.5 * x * bump, 0.5 + 0.5 * y * bump, 0.5 * z * bump]  # metres, Y up, base at 0
        nrm += [x, y, z]
for r in range(rings):
    for s in range(seg):
        a, b = r * (seg + 1) + s, (r + 1) * (seg + 1) + s
        idx += [a, b, a + 1, a + 1, b, b + 1]
vb = struct.pack('<%df' % len(pos), *pos)
nb = struct.pack('<%df' % len(nrm), *nrm)
ib = struct.pack('<%dI' % len(idx), *idx)
blob = vb + nb + ib
nv = len(pos) // 3
mins = [min(pos[i::3]) for i in range(3)]
maxs = [max(pos[i::3]) for i in range(3)]
gltf = {
    "asset": {"version": "2.0"},
    "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
    "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.5, 0.45, 1]}}],
    "buffers": [{"byteLength": len(blob)}],
    "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(vb)},
                    {"buffer": 0, "byteOffset": len(vb), "byteLength": len(nb)},
                    {"buffer": 0, "byteOffset": len(vb) + len(nb), "byteLength": len(ib)}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": nv, "type": "VEC3", "min": mins, "max": maxs},
                  {"bufferView": 1, "componentType": 5126, "count": nv, "type": "VEC3"},
                  {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}],
}
js = json.dumps(gltf).encode()
js += b' ' * ((4 - len(js) % 4) % 4)
blob += b'\0' * ((4 - len(blob) % 4) % 4)
data = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
data += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(blob), 0x004E4942) + blob
open(out, 'wb').write(data)
print('%s: %d triangles' % (out, len(idx) // 3))
