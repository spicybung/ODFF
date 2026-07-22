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

struct Effect2D
{
    Vec3 position{};
    std::uint32_t type = 0;
    Color4 color{};
    float coronaFarClip = 0.0f;
    float pointLightRange = 0.0f;
    float coronaSize = 0.0f;
    float shadowSize = 0.0f;
    std::uint8_t flags1 = 0;
    std::uint8_t flags2 = 0;
};

struct Geometry
{
    std::string name;
    std::uint16_t flags = 0;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<Color4> colors;
    std::vector<Triangle> triangles;
    std::vector<MaterialInfo> materials;
    std::vector<Effect2D> effects2d;
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
    bool hasNormalCollision = false;
    bool normalCollisionValid = false;
    bool hasSampCollision = false;
    bool sampCollisionValid = false;
    std::size_t effect2dCount = 0;
    std::size_t omniLightCount = 0;
    std::size_t declaredRenderWareLightCount = 0;
    std::size_t renderWareLightCount = 0;
    bool trafficLightSignature = false;
};
