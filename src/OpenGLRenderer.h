#pragma once

#include "Camera.h"
#include "ModelDocument.h"

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
        bool showGrid) const;

private:
    void DrawModel(
        const ModelData& model,
        bool wireframe) const;

    void DrawGeometry(
        const Geometry& geometry,
        const Mat4& transform) const;

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

    void DrawGrid(
        float extent,
        float step) const;
};
