#include "io/glb_loader.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf_rename.h"
#include "../../third_party/cgltf/cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace scal {

namespace fs = std::filesystem;

namespace {

constexpr double kMetersToUnits = 100.0;

struct Mat4 { float m[16]; }; // column-major, as glTF

// Y-up metres -> Z-up centimetres.
Vec3 convertPoint(double x, double y, double z) { return {x * kMetersToUnits, -z * kMetersToUnits, y * kMetersToUnits}; }
Vec3 convertDir(double x, double y, double z) { return {x, -z, y}; }

Vec3 transformPoint(const Mat4& M, const float* p) {
    const float* m = M.m;
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

// Normals turn with the cofactor matrix (inverse transpose, up to a positive factor when
// the transform doesn't mirror).
Vec3 transformNormal(const Mat4& M, const float* n) {
    const float* m = M.m;
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const double C[3][3] = {{e * i - f * h, f * g - d * i, d * h - e * g},
                            {c * h - b * i, a * i - c * g, b * g - a * h},
                            {b * f - c * e, c * d - a * f, a * e - b * d}};
    return {C[0][0] * n[0] + C[0][1] * n[1] + C[0][2] * n[2], C[1][0] * n[0] + C[1][1] * n[1] + C[1][2] * n[2],
            C[2][0] * n[0] + C[2][1] * n[1] + C[2][2] * n[2]};
}

double determinant3(const Mat4& M) {
    const float* m = M.m;
    return (double)m[0] * (m[5] * m[10] - m[9] * m[6]) - (double)m[4] * (m[1] * m[10] - m[9] * m[2]) +
           (double)m[8] * (m[1] * m[6] - m[5] * m[2]);
}

struct Ctx {
    cgltf_options opt{};
    cgltf_data* gl = nullptr;
    Model* out = nullptr;
    std::string dir;
    int defaultMaterial = -1;
    int skipped = 0;
    bool warnedStrips = false;
};

std::string decodedUri(const char* uri) {
    std::string s = uri ? uri : "";
    std::string buf = s;
    const cgltf_size n = cgltf_decode_uri(buf.data());
    buf.resize(n);
    return buf;
}

// Image `img` of the file: embedded bytes, a data: URI or a file next to the model.
void readImage(Ctx& c, const cgltf_image* img, std::vector<unsigned char>& bytes, std::string& file) {
    if (!img) return;
    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
        const unsigned char* p = (const unsigned char*)img->buffer_view->buffer->data + img->buffer_view->offset;
        bytes.assign(p, p + img->buffer_view->size);
        return;
    }
    if (!img->uri) return;
    if (std::strncmp(img->uri, "data:", 5) == 0) {
        const char* comma = std::strchr(img->uri, ',');
        if (!comma) return;
        const char* b64 = comma + 1;
        const size_t len = std::strlen(b64);
        size_t pad = 0;
        while (pad < len && pad < 2 && b64[len - 1 - pad] == '=') ++pad;
        const size_t size = len / 4 * 3 - pad;
        void* data = nullptr;
        if (cgltf_load_buffer_base64(&c.opt, size, b64, &data) == cgltf_result_success && data) {
            bytes.assign((unsigned char*)data, (unsigned char*)data + size);
            std::free(data);
        }
        return;
    }
    const fs::path f = fs::path(c.dir) / decodedUri(img->uri);
    std::error_code ec;
    if (fs::is_regular_file(f, ec)) file = f.string();
}

int addMaterial(Ctx& c, const cgltf_material* m) {
    ModelMaterial mat;
    if (m) {
        if (m->name) mat.name = m->name;
        if (m->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& p = m->pbr_metallic_roughness;
            mat.color = {p.base_color_factor[0] * 255.0, p.base_color_factor[1] * 255.0, p.base_color_factor[2] * 255.0};
            if (p.base_color_texture.texture) readImage(c, p.base_color_texture.texture->image, mat.image, mat.imageFile);
        } else if (m->has_pbr_specular_glossiness) {
            const cgltf_pbr_specular_glossiness& p = m->pbr_specular_glossiness;
            mat.color = {p.diffuse_factor[0] * 255.0, p.diffuse_factor[1] * 255.0, p.diffuse_factor[2] * 255.0};
            if (p.diffuse_texture.texture) readImage(c, p.diffuse_texture.texture->image, mat.image, mat.imageFile);
        }
        if (m->normal_texture.texture) readImage(c, m->normal_texture.texture->image, mat.normalImage, mat.normalImageFile);
        if (m->alpha_mode == cgltf_alpha_mode_mask) {
            mat.cutout = true;
            mat.cutoff = m->alpha_cutoff;
        }
    } else {
        mat.color = {200, 200, 200}; // no material: plain grey
    }
    c.out->materials.push_back(std::move(mat));
    return (int)c.out->materials.size() - 1;
}

void addPrimitive(Ctx& c, const cgltf_primitive& prim, const Mat4& M) {
    if (prim.type != cgltf_primitive_type_triangles) {
        ++c.skipped;
        return;
    }
    const cgltf_accessor *apos = nullptr, *anor = nullptr, *auv = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position && a.index == 0) apos = a.data;
        else if (a.type == cgltf_attribute_type_normal && a.index == 0) anor = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) auv = a.data;
    }
    if (!apos || apos->count == 0) return;

    ModelMesh mesh;
    if (prim.material) {
        const int idx = (int)(prim.material - c.gl->materials);
        mesh.material = idx; // materials were added in file order first
    } else {
        if (c.defaultMaterial < 0) c.defaultMaterial = addMaterial(c, nullptr);
        mesh.material = c.defaultMaterial;
    }
    const bool mirrored = determinant3(M) < 0; // a mirrored node flips the winding
    const size_t n = apos->count;
    mesh.pos.resize(3 * n);
    mesh.normal.assign(3 * n, 0.0f);
    mesh.uv.assign(2 * n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(apos, i, p, 3);
        const Vec3 w = transformPoint(M, p);
        const Vec3 v = convertPoint(w.x, w.y, w.z);
        mesh.pos[3 * i] = (float)v.x;
        mesh.pos[3 * i + 1] = (float)v.y;
        mesh.pos[3 * i + 2] = (float)v.z;
        if (anor) {
            float nn[3] = {0, 0, 1};
            cgltf_accessor_read_float(anor, i, nn, 3);
            Vec3 w2 = transformNormal(M, nn);
            if (mirrored) w2 = -w2;
            const Vec3 cn = normalize(convertDir(w2.x, w2.y, w2.z));
            mesh.normal[3 * i] = (float)cn.x;
            mesh.normal[3 * i + 1] = (float)cn.y;
            mesh.normal[3 * i + 2] = (float)cn.z;
        }
        if (auv) {
            float t[2] = {0, 0};
            cgltf_accessor_read_float(auv, i, t, 2);
            mesh.uv[2 * i] = t[0];
            mesh.uv[2 * i + 1] = t[1];
        }
    }
    const size_t ni = prim.indices ? prim.indices->count : n;
    mesh.index.reserve(ni - ni % 3);
    for (size_t i = 0; i + 2 < ni; i += 3) {
        std::uint32_t t[3];
        for (int k = 0; k < 3; ++k) {
            const size_t ix = prim.indices ? cgltf_accessor_read_index(prim.indices, i + k) : i + k;
            t[k] = (std::uint32_t)(ix < n ? ix : 0);
        }
        if (mirrored) std::swap(t[1], t[2]);
        mesh.index.insert(mesh.index.end(), t, t + 3);
    }
    if (mesh.index.empty()) return;
    if (!anor) { // smooth normals from the faces
        for (size_t i = 0; i + 2 < mesh.index.size(); i += 3) {
            const float *a = &mesh.pos[3 * mesh.index[i]], *b = &mesh.pos[3 * mesh.index[i + 1]], *d = &mesh.pos[3 * mesh.index[i + 2]];
            const Vec3 fn = cross(Vec3{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, Vec3{d[0] - a[0], d[1] - a[1], d[2] - a[2]});
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j) mesh.normal[3 * mesh.index[i + k] + j] += (float)fn[j];
        }
        for (size_t i = 0; i < n; ++i) {
            const Vec3 nn = normalize({mesh.normal[3 * i], mesh.normal[3 * i + 1], mesh.normal[3 * i + 2]});
            mesh.normal[3 * i] = (float)nn.x;
            mesh.normal[3 * i + 1] = (float)nn.y;
            mesh.normal[3 * i + 2] = (float)nn.z;
        }
    }
    c.out->meshes.push_back(std::move(mesh));
}

void addNode(Ctx& c, const cgltf_node* node, int depth) {
    if (!node || depth > 64) return;
    if (node->mesh) {
        Mat4 M;
        cgltf_node_transform_world(node, M.m);
        for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i) addPrimitive(c, node->mesh->primitives[i], M);
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) addNode(c, node->children[i], depth + 1);
}

} // namespace

bool loadGlbFile(const std::string& path, Model& out, std::string& error) {
    error.clear();
    out.materials.clear();
    out.meshes.clear();
    out.bounds = AABB{};
    Ctx c;
    c.out = &out;
    c.dir = fs::path(path).parent_path().string();
    cgltf_result r = cgltf_parse_file(&c.opt, path.c_str(), &c.gl);
    if (r != cgltf_result_success) {
        error = r == cgltf_result_file_not_found ? "the file isn't there" : "not a readable glTF / glb file";
        return false;
    }
    struct Free { cgltf_data* d; ~Free() { if (d) cgltf_free(d); } } guard{c.gl};
    for (cgltf_size i = 0; i < c.gl->extensions_required_count; ++i) {
        const char* e = c.gl->extensions_required[i];
        if (std::strstr(e, "draco") || std::strstr(e, "meshopt")) {
            error = std::string("compressed models (") + e + ") are not supported yet";
            return false;
        }
    }
    r = cgltf_load_buffers(&c.opt, c.gl, path.c_str());
    if (r != cgltf_result_success) {
        error = "the model's data file can't be read";
        return false;
    }
    if (cgltf_validate(c.gl) != cgltf_result_success) {
        error = "the file is damaged";
        return false;
    }
    for (cgltf_size i = 0; i < c.gl->materials_count; ++i) addMaterial(c, &c.gl->materials[i]);
    const cgltf_scene* scene = c.gl->scene ? c.gl->scene : (c.gl->scenes_count ? &c.gl->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) addNode(c, scene->nodes[i], 0);
    } else {
        for (cgltf_size i = 0; i < c.gl->nodes_count; ++i)
            if (!c.gl->nodes[i].parent) addNode(c, &c.gl->nodes[i], 0);
    }
    for (const ModelMesh& m : out.meshes)
        for (size_t i = 0; i < m.vertices(); ++i) out.bounds.add({m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2]});
    if (out.meshes.empty() || out.triangles() == 0) {
        error = "the file holds no triangle meshes";
        return false;
    }
    out.fingerprint = modelFingerprint(out);
    return true;
}

} // namespace scal
