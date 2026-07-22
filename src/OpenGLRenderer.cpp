#include "OpenGLRenderer.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace
{
    struct Edge
    {
        std::uint32_t first = 0;
        std::uint32_t second = 0;

        bool operator==(const Edge& other) const
        {
            return
                first == other.first &&
                second == other.second;
        }
    };

    struct EdgeHash
    {
        std::size_t operator()(const Edge& edge) const
        {
            const std::size_t firstHash =
                std::hash<std::uint32_t>{}(edge.first);

            const std::size_t secondHash =
                std::hash<std::uint32_t>{}(edge.second);

            return firstHash ^ (secondHash << 1);
        }
    };

    Edge MakeEdge(
        std::uint32_t first,
        std::uint32_t second)
    {
        if (first > second)
        {
            std::swap(first, second);
        }

        return {first, second};
    }

    bool IsFiniteVector(const Vec3& value)
    {
        return
            std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    std::string NormalizeTextureName(std::string name)
    {
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            name.erase(0, slash + 1);
        }

        const std::size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            name.erase(dot);
        }

        std::transform(
            name.begin(),
            name.end(),
            name.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });

        return name;
    }

    GLint TextureWrapMode(std::uint32_t addressing)
    {
        constexpr GLint GlClampToEdge = 0x812F;
        constexpr GLint GlMirroredRepeat = 0x8370;

        switch (addressing)
        {
            case 2:
                return GlMirroredRepeat;

            case 3:
            case 4:
                return GlClampToEdge;

            default:
                return GL_REPEAT;
        }
    }

    bool IsUsableTriangle(
        const Vec3& a,
        const Vec3& b,
        const Vec3& c)
    {
        if (!IsFiniteVector(a) ||
            !IsFiniteVector(b) ||
            !IsFiniteVector(c))
        {
            return false;
        }

        const Vec3 ab = b - a;
        const Vec3 ac = c - a;
        const Vec3 bc = c - b;

        const float abLengthSquared = Dot(ab, ab);
        const float acLengthSquared = Dot(ac, ac);
        const float bcLengthSquared = Dot(bc, bc);

        const float longestEdgeSquared = std::max({
            abLengthSquared,
            acLengthSquared,
            bcLengthSquared
        });

        if (longestEdgeSquared <= 0.000000000001f)
        {
            return false;
        }

        const Vec3 crossProduct = Cross(ab, ac);
        const float doubledAreaSquared =
            Dot(crossProduct, crossProduct);

        const float relativeArea =
            doubledAreaSquared /
            (longestEdgeSquared * longestEdgeSquared);

        return
            doubledAreaSquared > 0.0000000000000001f &&
            relativeArea > 0.0000000001f;
    }

    void Perspective(
        float fovY,
        float aspect,
        float nearPlane,
        float farPlane)
    {
        const float radians =
            fovY * 3.1415926535f / 180.0f;

        const float top =
            nearPlane * std::tan(radians * 0.5f);

        const float right = top * aspect;

        glFrustum(
            -right,
            right,
            -top,
            top,
            nearPlane,
            farPlane);
    }
}

void OpenGLRenderer::Render(
    const ModelDocument* document,
    const Camera& camera,
    int viewportX,
    int viewportY,
    int viewportWidth,
    int viewportHeight,
    bool wireframe,
    bool showCollision,
    bool showEffects2D,
    bool previewBuiltInTrafficModel,
    bool showGrid,
    const TxdData* textureDictionary) const
{
    (void)showCollision;

    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
        return;
    }

    glViewport(
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDepthRange(0.0, 1.0);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    float sceneRadius = 10.0f;

    if (document != nullptr &&
        document->model.bounds.valid)
    {
        sceneRadius = std::max(
            document->model.bounds.Radius(),
            0.01f);
    }

    const float nearPlane = std::max({
        0.001f,
        camera.distance * 0.001f,
        sceneRadius * 0.0005f
    });

    const float farPlane = std::max({
        nearPlane + 1.0f,
        camera.distance + sceneRadius * 8.0f,
        nearPlane * 1000.0f
    });

    Perspective(
        55.0f,
        static_cast<float>(viewportWidth) /
            static_cast<float>(viewportHeight),
        nearPlane,
        farPlane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    camera.ApplyView();

    if (showGrid)
    {
        const float extent =
            document != nullptr &&
            document->model.bounds.valid
                ? std::max(
                    document->model.bounds.Radius() * 4.0f,
                    20.0f)
                : 20.0f;

        DrawGrid(
            extent,
            std::max(extent / 20.0f, 1.0f));
    }

    if (document != nullptr)
    {
        DrawModel(document->model, wireframe, textureDictionary);

        if (showEffects2D)
        {
            DrawEffects2D(
                document->model,
                previewBuiltInTrafficModel);
        }

    }

    glDepthRange(0.0, 1.0);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
    glDisable(GL_DEPTH_TEST);
}

void OpenGLRenderer::DrawEffects2D(
    const ModelData& model,
    bool previewBuiltInTrafficModel) const
{
    if (model.trafficLightSignature && !previewBuiltInTrafficModel)
    {
        return;
    }
    std::vector<WorldLight> lights;
    lights.reserve(model.omniLightCount);

    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(atomic.geometryIndex) >= model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};
            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(atomic.frameIndex) < model.frames.size())
            {
                transform = model.frames[static_cast<std::size_t>(atomic.frameIndex)].worldTransform;
            }

            CollectGeometryLights(
                model.geometries[static_cast<std::size_t>(atomic.geometryIndex)],
                transform,
                lights);
        }
    }
    else
    {
        for (const Geometry& geometry : model.geometries)
        {
            CollectGeometryLights(geometry, Mat4{}, lights);
        }
    }

    if (lights.empty())
    {
        return;
    }

    DrawPointLightPass(model, lights);
}

void OpenGLRenderer::CollectGeometryLights(
    const Geometry& geometry,
    const Mat4& transform,
    std::vector<WorldLight>& lights) const
{
    for (const Effect2D& effect : geometry.effects2d)
    {
        if (effect.type != 0)
        {
            continue;
        }

        lights.push_back({TransformPoint(transform, effect.position), effect});
    }
}

void OpenGLRenderer::DrawPointLightPass(
    const ModelData& model,
    const std::vector<WorldLight>& lights) const
{
    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(atomic.geometryIndex) >= model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};
            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(atomic.frameIndex) < model.frames.size())
            {
                transform = model.frames[static_cast<std::size_t>(atomic.frameIndex)].worldTransform;
            }

            DrawGeometryPointLightPass(
                model.geometries[static_cast<std::size_t>(atomic.geometryIndex)],
                transform,
                lights);
        }
        return;
    }

    for (const Geometry& geometry : model.geometries)
    {
        DrawGeometryPointLightPass(geometry, Mat4{}, lights);
    }
}

void OpenGLRenderer::DrawGeometryPointLightPass(
    const Geometry& geometry,
    const Mat4& transform,
    const std::vector<WorldLight>& lights) const
{
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_EQUAL);

    glPushMatrix();
    glMultMatrixf(transform.m);
    glBegin(GL_TRIANGLES);

    for (const Triangle& triangle : geometry.triangles)
    {
        const std::uint32_t indices[3] = {triangle.a, triangle.b, triangle.c};
        if (indices[0] >= geometry.vertices.size() ||
            indices[1] >= geometry.vertices.size() ||
            indices[2] >= geometry.vertices.size())
        {
            continue;
        }

        for (const std::uint32_t index : indices)
        {
            const Vec3 worldPosition = TransformPoint(transform, geometry.vertices[index]);
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;

            for (const WorldLight& light : lights)
            {
                const float range = light.effect.pointLightRange;
                if (range <= 0.0f)
                {
                    continue;
                }

                const float distance = Length(light.position - worldPosition);
                if (distance >= range)
                {
                    continue;
                }

                const float strength = 1.0f - distance / range;
                const float falloff = strength * strength;
                red += falloff * static_cast<float>(light.effect.color.r) / 255.0f;
                green += falloff * static_cast<float>(light.effect.color.g) / 255.0f;
                blue += falloff * static_cast<float>(light.effect.color.b) / 255.0f;
            }

            glColor3f(
                std::min(red, 1.0f),
                std::min(green, 1.0f),
                std::min(blue, 1.0f));
            const Vec3& vertex = geometry.vertices[index];
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
    }

    glEnd();
    glPopMatrix();
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void OpenGLRenderer::DrawLightGlows(
    const std::vector<WorldLight>& lights) const
{
    float modelView[16]{};
    glGetFloatv(GL_MODELVIEW_MATRIX, modelView);

    const Vec3 right{modelView[0], modelView[4], modelView[8]};
    const Vec3 up{modelView[1], modelView[5], modelView[9]};

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    constexpr int SegmentCount = 32;
    constexpr float TwoPi = 6.28318530718f;

    for (const WorldLight& light : lights)
    {
        if (light.effect.coronaSize <= 0.0f || (light.effect.flags1 & 8) != 0)
        {
            continue;
        }

        const float size = light.effect.coronaSize;
        const float red = static_cast<float>(light.effect.color.r) / 255.0f;
        const float green = static_cast<float>(light.effect.color.g) / 255.0f;
        const float blue = static_cast<float>(light.effect.color.b) / 255.0f;
        const float alpha = static_cast<float>(light.effect.color.a) / 255.0f;

        if ((light.effect.flags1 & 1) != 0)
        {
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }

        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(red, green, blue, alpha);
        glVertex3f(light.position.x, light.position.y, light.position.z);

        glColor4f(red, green, blue, 0.0f);
        for (int segment = 0; segment <= SegmentCount; ++segment)
        {
            const float angle = TwoPi * static_cast<float>(segment) /
                static_cast<float>(SegmentCount);
            const Vec3 offset =
                right * (std::cos(angle) * size) +
                up * (std::sin(angle) * size);
            const Vec3 vertex = light.position + offset;
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
        glEnd();

        const float coreSize = size * 0.16f;
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 1.0f, 1.0f, std::max(alpha, 0.85f));
        glVertex3f(light.position.x, light.position.y, light.position.z);

        glColor4f(red, green, blue, 0.0f);
        for (int segment = 0; segment <= SegmentCount; ++segment)
        {
            const float angle = TwoPi * static_cast<float>(segment) /
                static_cast<float>(SegmentCount);
            const Vec3 offset =
                right * (std::cos(angle) * coreSize) +
                up * (std::sin(angle) * coreSize);
            const Vec3 vertex = light.position + offset;
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
        glEnd();
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void OpenGLRenderer::DrawModel(
    const ModelData& model,
    bool wireframe,
    const TxdData* textureDictionary) const
{
    glPolygonMode(
        GL_FRONT_AND_BACK,
        wireframe ? GL_LINE : GL_FILL);

    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(
                    atomic.geometryIndex) >=
                    model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};

            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(
                    atomic.frameIndex) <
                    model.frames.size())
            {
                transform =
                    model.frames[
                        static_cast<std::size_t>(
                            atomic.frameIndex)]
                        .worldTransform;
            }

            DrawGeometry(
                model.geometries[
                    static_cast<std::size_t>(
                        atomic.geometryIndex)],
                transform,
                textureDictionary);
        }

        return;
    }

    Mat4 identity{};

    for (const Geometry& geometry : model.geometries)
    {
        DrawGeometry(geometry, identity, textureDictionary);
    }
}

void OpenGLRenderer::DrawGeometry(
    const Geometry& geometry,
    const Mat4& transform,
    const TxdData* textureDictionary) const
{
    glPushMatrix();
    glMultMatrixf(transform.m);

    if (geometry.materials.empty())
    {
        DrawMaterialTriangles(
            geometry,
            nullptr,
            std::numeric_limits<std::uint16_t>::max(),
            nullptr);
    }
    else
    {
        for (std::size_t materialIndex = 0;
             materialIndex < geometry.materials.size();
             ++materialIndex)
        {
            const MaterialInfo& material = geometry.materials[materialIndex];
            const TxdTextureInfo* sourceTexture = FindTexture(
                textureDictionary,
                material.textureName);

            const UploadedTexture* uploadedTexture =
                sourceTexture != nullptr
                    ? UploadTexture(*sourceTexture)
                    : nullptr;

            DrawMaterialTriangles(
                geometry,
                &material,
                static_cast<std::uint16_t>(materialIndex),
                uploadedTexture);
        }

        DrawMaterialTriangles(
            geometry,
            nullptr,
            std::numeric_limits<std::uint16_t>::max(),
            nullptr);
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glPopMatrix();
}

const TxdTextureInfo* OpenGLRenderer::FindTexture(
    const TxdData* textureDictionary,
    const std::string& name) const
{
    if (textureDictionary == nullptr || name.empty())
    {
        return nullptr;
    }

    const std::string wantedName = NormalizeTextureName(name);
    for (const TxdTextureInfo& texture : textureDictionary->textures)
    {
        if (NormalizeTextureName(texture.name) == wantedName)
        {
            return &texture;
        }
    }

    return nullptr;
}

const OpenGLRenderer::UploadedTexture* OpenGLRenderer::UploadTexture(
    const TxdTextureInfo& texture) const
{
    if (texture.mipLevels.empty() || !texture.decodeError.empty())
    {
        return nullptr;
    }

    const std::string key = NormalizeTextureName(texture.name);
    const auto existing = uploadedTextures.find(key);
    if (existing != uploadedTextures.end())
    {
        return &existing->second;
    }

    UploadedTexture uploaded{};
    uploaded.hasAlpha = texture.sampPreviewUsesAlpha;
    glGenTextures(1, &uploaded.id);
    if (uploaded.id == 0)
    {
        return nullptr;
    }

    glBindTexture(GL_TEXTURE_2D, uploaded.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (std::size_t levelIndex = 0;
         levelIndex < texture.mipLevels.size();
         ++levelIndex)
    {
        const TxdMipLevel& mip = texture.mipLevels[levelIndex];
        const std::uint8_t* pixelData = mip.rgbaPixels.data();
        std::vector<std::uint8_t> forcedOpaquePixels;

        if (texture.hasAlpha && !texture.sampPreviewUsesAlpha)
        {
            forcedOpaquePixels = mip.rgbaPixels;
            for (std::size_t alphaOffset = 3;
                 alphaOffset < forcedOpaquePixels.size();
                 alphaOffset += 4)
            {
                forcedOpaquePixels[alphaOffset] = 255;
            }
            pixelData = forcedOpaquePixels.data();
        }

        glTexImage2D(
            GL_TEXTURE_2D,
            static_cast<GLint>(levelIndex),
            GL_RGBA,
            mip.width,
            mip.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixelData);
    }

    const std::uint32_t filterMode = texture.filterAddressing & 0xFF;
    const bool hasMipmaps = texture.mipLevels.size() > 1;
    GLint minimumFilter = GL_LINEAR;
    GLint magnificationFilter = GL_LINEAR;

    if (filterMode == 1)
    {
        minimumFilter = hasMipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
        magnificationFilter = GL_NEAREST;
    }
    else if (hasMipmaps)
    {
        minimumFilter =
            filterMode == 3 || filterMode == 5
                ? GL_LINEAR_MIPMAP_NEAREST
                : GL_LINEAR_MIPMAP_LINEAR;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minimumFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magnificationFilter);
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        TextureWrapMode((texture.filterAddressing >> 8) & 0x0F));
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        TextureWrapMode((texture.filterAddressing >> 12) & 0x0F));

    const auto inserted = uploadedTextures.emplace(key, uploaded);
    return &inserted.first->second;
}

void OpenGLRenderer::DrawMaterialTriangles(
    const Geometry& geometry,
    const MaterialInfo* material,
    std::uint16_t materialIndex,
    const UploadedTexture* texture) const
{
    const bool hasTexture = texture != nullptr && texture->id != 0;

    auto triangleUsesRequestedMaterial = [&](const Triangle& triangle)
    {
        const bool unassignedMaterial =
            triangle.materialIndex >= geometry.materials.size();

        if (materialIndex == std::numeric_limits<std::uint16_t>::max())
        {
            return unassignedMaterial;
        }

        return triangle.materialIndex == materialIndex;
    };

    bool hasVertexAlpha = false;
    for (const Triangle& triangle : geometry.triangles)
    {
        if (!triangleUsesRequestedMaterial(triangle))
        {
            continue;
        }

        const std::uint32_t indices[3] = {
            triangle.a,
            triangle.b,
            triangle.c
        };

        for (const std::uint32_t index : indices)
        {
            if (index < geometry.colors.size() &&
                geometry.colors[index].a < 255)
            {
                hasVertexAlpha = true;
                break;
            }
        }

        if (hasVertexAlpha)
        {
            break;
        }
    }

    const bool transparent =
        (hasTexture && texture->hasAlpha) ||
        (material != nullptr && material->color.a < 255) ||
        hasVertexAlpha;

    if (hasTexture)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture->id);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    else
    {
        glDisable(GL_TEXTURE_2D);
    }

    if (hasTexture && texture->hasAlpha)
    {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 1.0f / 255.0f);
    }
    else
    {
        glDisable(GL_ALPHA_TEST);
    }

    if (transparent)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    auto multiplyChannel = [](std::uint8_t left, std::uint8_t right)
    {
        return static_cast<std::uint8_t>(
            (static_cast<unsigned int>(left) * right + 127) / 255);
    };

    glBegin(GL_TRIANGLES);

    for (const Triangle& triangle : geometry.triangles)
    {
        if (!triangleUsesRequestedMaterial(triangle))
        {
            continue;
        }

        if (triangle.a >= geometry.vertices.size() ||
            triangle.b >= geometry.vertices.size() ||
            triangle.c >= geometry.vertices.size())
        {
            continue;
        }

        const std::uint32_t indices[3] = {
            triangle.a,
            triangle.b,
            triangle.c
        };

        for (std::uint32_t index : indices)
        {
            Color4 color{255, 255, 255, 255};

            if (index < geometry.colors.size())
            {
                color = geometry.colors[index];
            }

            if (material != nullptr)
            {
                color.r = multiplyChannel(color.r, material->color.r);
                color.g = multiplyChannel(color.g, material->color.g);
                color.b = multiplyChannel(color.b, material->color.b);
                color.a = multiplyChannel(color.a, material->color.a);
            }

            glColor4ub(
                color.r,
                color.g,
                color.b,
                color.a);

            if (index < geometry.normals.size())
            {
                const Vec3& normal = geometry.normals[index];
                glNormal3f(normal.x, normal.y, normal.z);
            }

            if (hasTexture && index < geometry.texCoords.size())
            {
                const Vec2& texCoord = geometry.texCoords[index];
                glTexCoord2f(texCoord.x, texCoord.y);
            }

            const Vec3& vertex = geometry.vertices[index];
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
    }

    glEnd();
    glDisable(GL_ALPHA_TEST);
}

void OpenGLRenderer::InvalidateTextures()
{
    ReleaseTextures();
}

void OpenGLRenderer::ReleaseTextures()
{
    for (const auto& entry : uploadedTextures)
    {
        if (entry.second.id != 0)
        {
            glDeleteTextures(1, &entry.second.id);
        }
    }

    uploadedTextures.clear();
}

void OpenGLRenderer::DrawCollision(
    const CollisionData& collision,
    const ModelData& model) const
{
    if (collision.mode == CollisionMode::Empty)
    {
        return;
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glDepthRange(0.0, 0.9975);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.35f);
    glColor4ub(222, 30, 48, 255);

    if (collision.mode == CollisionMode::MeshFaces)
    {
        DrawMeshCollision(model);
    }
    else
    {
        DrawFlatCollision(collision);
    }

    glDepthRange(0.0, 1.0);
    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
}

void OpenGLRenderer::DrawMeshCollision(
    const ModelData& model) const
{
    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(
                    atomic.geometryIndex) >=
                    model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};

            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(
                    atomic.frameIndex) <
                    model.frames.size())
            {
                transform =
                    model.frames[
                        static_cast<std::size_t>(
                            atomic.frameIndex)]
                        .worldTransform;
            }

            DrawGeometryCollision(
                model.geometries[
                    static_cast<std::size_t>(
                        atomic.geometryIndex)],
                transform);
        }

        return;
    }

    Mat4 identity{};

    for (const Geometry& geometry : model.geometries)
    {
        DrawGeometryCollision(
            geometry,
            identity);
    }
}

void OpenGLRenderer::DrawGeometryCollision(
    const Geometry& geometry,
    const Mat4& transform) const
{
    std::unordered_set<Edge, EdgeHash> edges;
    edges.reserve(geometry.triangles.size() * 3);

    for (const Triangle& triangle : geometry.triangles)
    {
        if (triangle.a >= geometry.vertices.size() ||
            triangle.b >= geometry.vertices.size() ||
            triangle.c >= geometry.vertices.size())
        {
            continue;
        }

        const Vec3& a =
            geometry.vertices[triangle.a];

        const Vec3& b =
            geometry.vertices[triangle.b];

        const Vec3& c =
            geometry.vertices[triangle.c];

        if (!IsUsableTriangle(a, b, c))
        {
            continue;
        }

        edges.insert(MakeEdge(
            triangle.a,
            triangle.b));

        edges.insert(MakeEdge(
            triangle.b,
            triangle.c));

        edges.insert(MakeEdge(
            triangle.c,
            triangle.a));
    }

    if (edges.empty())
    {
        return;
    }

    glPushMatrix();
    glMultMatrixf(transform.m);

    glBegin(GL_LINES);

    for (const Edge& edge : edges)
    {
        const Vec3& a =
            geometry.vertices[edge.first];

        const Vec3& b =
            geometry.vertices[edge.second];

        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
    }

    glEnd();
    glPopMatrix();
}

void OpenGLRenderer::DrawFlatCollision(
    const CollisionData& collision) const
{
    if (collision.vertices.empty() ||
        collision.faces.empty())
    {
        return;
    }

    std::unordered_set<Edge, EdgeHash> edges;
    edges.reserve(collision.faces.size() * 3);

    for (const CollisionFace& face : collision.faces)
    {
        if (face.a >= collision.vertices.size() ||
            face.b >= collision.vertices.size() ||
            face.c >= collision.vertices.size())
        {
            continue;
        }

        const Vec3& a =
            collision.vertices[face.a];

        const Vec3& b =
            collision.vertices[face.b];

        const Vec3& c =
            collision.vertices[face.c];

        if (!IsUsableTriangle(a, b, c))
        {
            continue;
        }

        edges.insert(MakeEdge(face.a, face.b));
        edges.insert(MakeEdge(face.b, face.c));
        edges.insert(MakeEdge(face.c, face.a));
    }

    glBegin(GL_LINES);

    for (const Edge& edge : edges)
    {
        const Vec3& a =
            collision.vertices[edge.first];

        const Vec3& b =
            collision.vertices[edge.second];

        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
    }

    glEnd();
}

void OpenGLRenderer::DrawGrid(
    float extent,
    float step) const
{
    const int lineCount =
        static_cast<int>(extent / step);

    glLineWidth(1.0f);
    glColor4ub(120, 116, 145, 255);

    glBegin(GL_LINES);

    for (int index = -lineCount;
         index <= lineCount;
         ++index)
    {
        const float coordinate =
            static_cast<float>(index) * step;

        glVertex3f(-extent, coordinate, 0.0f);
        glVertex3f(extent, coordinate, 0.0f);

        glVertex3f(coordinate, -extent, 0.0f);
        glVertex3f(coordinate, extent, 0.0f);
    }

    glEnd();

    glLineWidth(2.0f);
    glBegin(GL_LINES);

    glColor4ub(190, 125, 45, 255);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(extent, 0.0f, 0.0f);

    glColor4ub(65, 170, 75, 255);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, extent, 0.0f);

    glColor4ub(65, 90, 205, 255);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, extent);

    glEnd();
}
