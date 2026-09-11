#pragma once

#include "CollisionBuilder.h"
#include "ModelTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ModelValidationState
{
    NotScanned,
    Passed,
    Failed
};

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
    std::vector<std::uint8_t> importedCollisionBytes;
    std::string importedCollisionName;
    std::string importedCollisionFormat;
    bool importedCollisionFromLibrary = false;
    bool loaded = false;
    bool loadAttempted = false;
    ModelValidationState validationState = ModelValidationState::NotScanned;
    bool clumpValid = false;
    bool atomicsValid = false;
    bool extensionsValid = false;
    std::size_t scannedAtomicCount = 0;
    std::size_t scannedExtensionCount = 0;
    std::string validationError;
    bool hasCollision = false;
    bool collisionDetached = false;
    CollisionExportMode collisionExportMode =
        CollisionExportMode::PreserveSource;
    bool dynamicLighting = false;
    bool showNightVertexColors = false;
    bool breakableEnabled = false;
    bool breakableStateInitialized = false;
};
