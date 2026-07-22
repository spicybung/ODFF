#include "CollisionBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool IsFiniteVector(const Vec3& value)
    {
        return
            std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    bool IsUsableCollisionTriangle(
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
}

CollisionData CollisionBuilder::Build(
    const ModelData& model,
    CollisionMode mode) const
{
    switch (mode)
    {
        case CollisionMode::Empty:
            return {};

        case CollisionMode::Box:
            return BuildBox(model);

        case CollisionMode::MeshFaces:
            return BuildMesh(model);
    }

    return {};
}

CollisionData CollisionBuilder::BuildBox(const ModelData& model) const
{
    CollisionData collision{};
    collision.mode = CollisionMode::Box;
    collision.bounds = model.bounds;

    if (!model.bounds.valid)
    {
        return collision;
    }

    const Vec3& minimum = model.bounds.minimum;
    const Vec3& maximum = model.bounds.maximum;

    collision.vertices = {
        {minimum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z},
        {minimum.x, maximum.y, minimum.z},
        {minimum.x, minimum.y, maximum.z},
        {maximum.x, minimum.y, maximum.z},
        {maximum.x, maximum.y, maximum.z},
        {minimum.x, maximum.y, maximum.z}
    };

    const std::uint16_t indices[][3] = {
        {0, 2, 1}, {0, 3, 2},
        {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4},
        {1, 2, 6}, {1, 6, 5},
        {2, 3, 7}, {2, 7, 6},
        {3, 0, 4}, {3, 4, 7}
    };

    for (const auto& face : indices)
    {
        collision.faces.push_back({face[0], face[1], face[2], 0, 0});
    }

    return collision;
}

CollisionData CollisionBuilder::BuildMesh(
    const ModelData& model) const
{
    CollisionData collision{};
    collision.mode = CollisionMode::MeshFaces;

    auto appendGeometry = [&](const Geometry& geometry, const Mat4& transform)
    {
        constexpr std::uint32_t missingIndex =
            std::numeric_limits<std::uint32_t>::max();

        constexpr std::size_t maximumCollisionVertexCount =
            static_cast<std::size_t>(
                std::numeric_limits<std::uint16_t>::max()) + 1;

        std::vector<std::uint32_t> vertexMap(
            geometry.vertices.size(),
            missingIndex);

        auto appendVertex = [&](std::uint32_t sourceIndex) -> std::uint32_t
        {
            if (sourceIndex >= geometry.vertices.size())
            {
                return missingIndex;
            }

            std::uint32_t& mappedIndex = vertexMap[sourceIndex];
            if (mappedIndex != missingIndex)
            {
                return mappedIndex;
            }

            if (collision.vertices.size() >= maximumCollisionVertexCount)
            {
                return missingIndex;
            }

            const Vec3 transformed = TransformPoint(
                transform,
                geometry.vertices[sourceIndex]);

            if (!IsFiniteVector(transformed))
            {
                return missingIndex;
            }

            mappedIndex = static_cast<std::uint32_t>(
                collision.vertices.size());

            collision.vertices.push_back(transformed);
            collision.bounds.Expand(transformed);
            return mappedIndex;
        };

        for (const Triangle& triangle : geometry.triangles)
        {
            if (triangle.a >= geometry.vertices.size() ||
                triangle.b >= geometry.vertices.size() ||
                triangle.c >= geometry.vertices.size())
            {
                continue;
            }

            const Vec3 a = TransformPoint(
                transform,
                geometry.vertices[triangle.a]);

            const Vec3 b = TransformPoint(
                transform,
                geometry.vertices[triangle.b]);

            const Vec3 c = TransformPoint(
                transform,
                geometry.vertices[triangle.c]);

            if (!IsUsableCollisionTriangle(a, b, c))
            {
                continue;
            }

            const std::uint32_t ia = appendVertex(triangle.a);
            const std::uint32_t ib = appendVertex(triangle.b);
            const std::uint32_t ic = appendVertex(triangle.c);

            if (ia == missingIndex ||
                ib == missingIndex ||
                ic == missingIndex)
            {
                continue;
            }

            if (ia == ib || ib == ic || ic == ia)
            {
                continue;
            }

            collision.faces.push_back({
                static_cast<std::uint16_t>(ia),
                static_cast<std::uint16_t>(ib),
                static_cast<std::uint16_t>(ic),
                static_cast<std::uint8_t>(
                    triangle.materialIndex & 0xFF),
                0
            });
        }
    };

    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(atomic.geometryIndex) >=
                    model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};
            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(atomic.frameIndex) <
                    model.frames.size())
            {
                transform = model.frames[
                    static_cast<std::size_t>(atomic.frameIndex)]
                    .worldTransform;
            }

            appendGeometry(
                model.geometries[
                    static_cast<std::size_t>(atomic.geometryIndex)],
                transform);
        }
    }
    else
    {
        Mat4 identity{};
        for (const Geometry& geometry : model.geometries)
        {
            appendGeometry(geometry, identity);
        }
    }

    return collision;
}
