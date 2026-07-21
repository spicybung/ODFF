#pragma once

#include "Camera.h"
#include "ModelDocument.h"
#include "TxdReader.h"

#include <string>
#include <unordered_map>
#include <vector>

class OpenGLRenderer
{
public:
    void Render(
        const ModelDocument* document,
        const Camera& camera,
        int viewportX,
        int viewportY,
        int viewportWidth,
        int viewportHeight,
        bool wireframe,
        bool showCollision,
        bool showEffects2D,
        bool showGrid,
        const TxdData* textureDictionary) const;

    void InvalidateTextures();
    void ReleaseTextures();

private:
    struct UploadedTexture
    {
        unsigned int id = 0;
        bool hasAlpha = false;
    };

    struct WorldLight
    {
        Vec3 position{};
        Effect2D effect{};
    };

    void DrawModel(
        const ModelData& model,
        bool wireframe,
        const TxdData* textureDictionary) const;

    void DrawGeometry(
        const Geometry& geometry,
        const Mat4& transform,
        const TxdData* textureDictionary) const;

    const TxdTextureInfo* FindTexture(
        const TxdData* textureDictionary,
        const std::string& name) const;

    const UploadedTexture* UploadTexture(
        const TxdTextureInfo& texture) const;

    void DrawMaterialTriangles(
        const Geometry& geometry,
        const MaterialInfo* material,
        std::uint16_t materialIndex,
        const UploadedTexture* texture) const;

    void DrawCollision(
        const CollisionData& collision,
        const ModelData& model) const;

    void DrawMeshCollision(
        const ModelData& model) const;

    void DrawGeometryCollision(
        const Geometry& geometry,
        const Mat4& transform) const;

    void DrawFlatCollision(
        const CollisionData& collision) const;

    void DrawEffects2D(const ModelData& model) const;
    void CollectGeometryLights(
        const Geometry& geometry,
        const Mat4& transform,
        std::vector<WorldLight>& lights) const;
    void DrawPointLightPass(
        const ModelData& model,
        const std::vector<WorldLight>& lights) const;
    void DrawGeometryPointLightPass(
        const Geometry& geometry,
        const Mat4& transform,
        const std::vector<WorldLight>& lights) const;
    void DrawLightGlows(
        const std::vector<WorldLight>& lights) const;

    void DrawGrid(
        float extent,
        float step) const;

    mutable std::unordered_map<std::string, UploadedTexture> uploadedTextures;
};
