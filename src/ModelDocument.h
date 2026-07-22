#pragma once

#include "CollisionBuilder.h"
#include "ModelTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class CollisionExportMode
{
    PreserveSource,
    AttachOrReplace
};

struct ModelDocument
{
    std::filesystem::path sourcePath;
    std::string displayName;
    std::vector<std::uint8_t> sourceBytes;
    ModelData model;
    CollisionData collision;
    bool hasCollision = false;
    bool collisionDetached = false;
    CollisionExportMode collisionExportMode =
        CollisionExportMode::PreserveSource;
};
