#include "CollisionBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace
{
    struct QuantizedVertex
    {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const QuantizedVertex& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct QuantizedVertexHash
    {
        std::size_t operator()(const QuantizedVertex& value) const
        {
            const std::size_t h1 = std::hash<int>{}(value.x);
            const std::size_t h2 = std::hash<int>{}(value.y);
            const std::size_t h3 = std::hash<int>{}(value.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    QuantizedVertex Quantize(const Vec3& value)
    {
        constexpr float scale = 10000.0f;

        return {
            static_cast<int>(value.x * scale),
            static_cast<int>(value.y * scale),
            static_cast<int>(value.z * scale)
        };
    }

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
    CollisionMode mode,
    bool optimize) const
{
    switch (mode)
    {
        case CollisionMode::Empty:
            return {};

        case CollisionMode::Box:
            return BuildBox(model);

        case CollisionMode::MeshFaces:
            return BuildMesh(model, optimize);
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
    const ModelData& model,
    bool optimize) const
{
    CollisionData collision{};
    collision.mode = CollisionMode::MeshFaces;

    std::unordered_map<QuantizedVertex, std::uint16_t, QuantizedVertexHash> optimizedVertices;

    auto appendVertex = [&](const Vec3& vertex) -> std::uint16_t
    {
        if (!optimize)
        {
            if (collision.vertices.size() >= std::numeric_limits<std::uint16_t>::max())
            {
                return std::numeric_limits<std::uint16_t>::max();
            }

            collision.vertices.push_back(vertex);
            return static_cast<std::uint16_t>(collision.vertices.size() - 1);
        }

        const QuantizedVertex key = Quantize(vertex);
        const auto found = optimizedVertices.find(key);
        if (found != optimizedVertices.end())
        {
            return found->second;
        }

        if (collision.vertices.size() >= std::numeric_limits<std::uint16_t>::max())
        {
            return std::numeric_limits<std::uint16_t>::max();
        }

        const std::uint16_t index = static_cast<std::uint16_t>(collision.vertices.size());
        collision.vertices.push_back(vertex);
        optimizedVertices.emplace(key, index);
        return index;
    };

    auto appendGeometry = [&](const Geometry& geometry, const Mat4& transform)
    {
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

            collision.bounds.Expand(a);
            collision.bounds.Expand(b);
            collision.bounds.Expand(c);

            const std::uint16_t ia = appendVertex(a);
            const std::uint16_t ib = appendVertex(b);
            const std::uint16_t ic = appendVertex(c);

            if (ia == std::numeric_limits<std::uint16_t>::max() ||
                ib == std::numeric_limits<std::uint16_t>::max() ||
                ic == std::numeric_limits<std::uint16_t>::max())
            {
                continue;
            }

            if (ia == ib || ib == ic || ic == ia)
            {
                continue;
            }

            collision.faces.push_back({
                ia,
                ib,
                ic,
                static_cast<std::uint8_t>(triangle.materialIndex & 0xFF),
                0
            });
        }
    };

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

            appendGeometry(
                model.geometries[static_cast<std::size_t>(atomic.geometryIndex)],
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
