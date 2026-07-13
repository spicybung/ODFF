#pragma once

#include "MathTypes.h"

#include <cstdint>
#include <string>
#include <vector>

struct Triangle
{
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    std::uint16_t materialIndex = 0;
};

struct MaterialInfo
{
    Color4 color{};
    std::string textureName;
    std::string maskName;
};

struct Geometry
{
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<Color4> colors;
    std::vector<Triangle> triangles;
    std::vector<MaterialInfo> materials;
    Bounds bounds;
};

struct Frame
{
    Mat4 localTransform{};
    Mat4 worldTransform{};
    std::int32_t parentIndex = -1;
    std::string name;
};

struct Atomic
{
    std::int32_t frameIndex = -1;
    std::int32_t geometryIndex = -1;
    std::uint32_t flags = 0;
};

struct ModelData
{
    std::vector<Frame> frames;
    std::vector<Geometry> geometries;
    std::vector<Atomic> atomics;
    Bounds bounds;
    std::uint32_t renderWareVersion = 0;
};
