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

struct CollisionFace
{
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
    std::uint8_t material = 0;
    std::uint8_t light = 0;
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
