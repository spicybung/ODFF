#include "OpenGLRenderer.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    bool showGrid) const
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
        DrawModel(document->model, wireframe);

    }

    glDepthRange(0.0, 1.0);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLineWidth(1.0f);
    glDisable(GL_DEPTH_TEST);
}

void OpenGLRenderer::DrawModel(
    const ModelData& model,
    bool wireframe) const
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
                transform);
        }

        return;
    }

    Mat4 identity{};

    for (const Geometry& geometry : model.geometries)
    {
        DrawGeometry(geometry, identity);
    }
}

void OpenGLRenderer::DrawGeometry(
    const Geometry& geometry,
    const Mat4& transform) const
{
    glPushMatrix();
    glMultMatrixf(transform.m);

    glBegin(GL_TRIANGLES);

    for (const Triangle& triangle : geometry.triangles)
    {
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
            Color4 color{};

            if (index < geometry.colors.size())
            {
                color = geometry.colors[index];
            }
            else if (
                triangle.materialIndex <
                geometry.materials.size())
            {
                color =
                    geometry.materials[
                        triangle.materialIndex]
                        .color;
            }

            if (color.a == 0)
            {
                color.a = 255;
            }

            glColor4ub(
                color.r,
                color.g,
                color.b,
                color.a);

            if (index < geometry.normals.size())
            {
                const Vec3& normal =
                    geometry.normals[index];

                glNormal3f(
                    normal.x,
                    normal.y,
                    normal.z);
            }

            const Vec3& vertex =
                geometry.vertices[index];

            glVertex3f(
                vertex.x,
                vertex.y,
                vertex.z);
        }
    }

    glEnd();
    glPopMatrix();
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
