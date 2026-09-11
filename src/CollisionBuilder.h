#pragma once

#include "ModelTypes.h"

#include <filesystem>
#include <string>
#include <vector>

enum class CollisionMode
{
    Empty,
    Box,
    MeshFaces
};

constexpr std::uint8_t DefaultCollisionDayLight = 15;
constexpr std::uint8_t DefaultCollisionNightLight = 15;

constexpr std::uint8_t PackCollisionLight(
    std::uint8_t dayLight,
    std::uint8_t nightLight)
{
    return static_cast<std::uint8_t>(
        ((dayLight & 0x0F) << 4) |
        (nightLight & 0x0F));
}

constexpr std::uint8_t DefaultPackedCollisionLight =
    PackCollisionLight(
        DefaultCollisionDayLight,
        DefaultCollisionNightLight);

struct CollisionFace
{
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
    std::uint8_t material = 0;
    std::uint8_t light = DefaultPackedCollisionLight;
};

struct CollisionData
{
    CollisionMode mode = CollisionMode::Empty;
    Bounds bounds;
    std::vector<Vec3> vertices;
    std::vector<CollisionFace> faces;
};

class CollisionBuilder
{
public:
    CollisionData Build(const ModelData& model, CollisionMode mode) const;

private:
    CollisionData BuildBox(const ModelData& model) const;
    CollisionData BuildMesh(const ModelData& model) const;
};
