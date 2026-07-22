#include "DffExporter.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace
{
    constexpr std::uint32_t ChunkExtension = 0x00000003;
    constexpr std::uint32_t ChunkStruct = 0x00000001;
    constexpr std::uint32_t ChunkClump = 0x00000010;
    constexpr std::uint32_t ChunkLight = 0x00000012;

    constexpr std::uint32_t NormalCollisionPluginId = 0x0253F2FA;
    constexpr std::uint32_t SampCollisionPluginId = 0x0253F2FF;

    constexpr std::size_t RenderWareChunkHeaderSize = 12;
    constexpr std::uint32_t Col3HeaderOffsetBase = 116;

    bool IsCollisionPlugin(std::uint32_t type)
    {
        return type == NormalCollisionPluginId ||
               type == SampCollisionPluginId;
    }
}

bool DffExporter::ExportDocument(
    const ModelDocument& document,
    const std::filesystem::path& outputPath,
    std::string& error) const
{
    std::vector<std::uint8_t> output;

    if (!BuildExportBytes(document, output, error))
    {
        return false;
    }

    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream)
    {
        error = "Could not create the output DFF.";
        return false;
    }

    if (!output.empty())
    {
        stream.write(
            reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
    }

    if (!stream)
    {
        error = "The output DFF could not be written completely.";
        return false;
    }

    return true;
}


bool DffExporter::DetachCollisionFromDocument(
    ModelDocument& document,
    bool& detached,
    std::string& error) const
{
    detached = false;

    if (document.sourceBytes.empty())
    {
        error = "The DFF data is missing.";
        return false;
    }

    const bool hadGeneratedCollision =
        document.hasCollision ||
        document.collisionExportMode == CollisionExportMode::AttachOrReplace;

    const bool readerReportedCollision =
        document.model.hasNormalCollision ||
        document.model.hasSampCollision;

    std::vector<std::uint8_t> detachedBytes = document.sourceBytes;
    bool removedEmbeddedCollision = false;

    if (!RemoveEmbeddedCollision(
            detachedBytes,
            removedEmbeddedCollision,
            error))
    {
        return false;
    }

    if (readerReportedCollision && !removedEmbeddedCollision)
    {
        error =
            "ODFF found collision but could not remove it.";
        return false;
    }

    detached =
        removedEmbeddedCollision ||
        hadGeneratedCollision ||
        readerReportedCollision;

    if (!detached)
    {
        return true;
    }

    document.sourceBytes = std::move(detachedBytes);
    document.collision = {};
    document.hasCollision = false;
    document.collisionDetached = true;
    document.collisionExportMode = CollisionExportMode::PreserveSource;

    document.model.hasNormalCollision = false;
    document.model.normalCollisionValid = false;
    document.model.hasSampCollision = false;
    document.model.sampCollisionValid = false;

    return true;
}

bool DffExporter::BuildExportBytes(
    const ModelDocument& document,
    std::vector<std::uint8_t>& output,
    std::string& error) const
{
    if (document.sourceBytes.empty())
    {
        error = "The DFF data is missing.";
        return false;
    }

    output = document.sourceBytes;

    if (!EnsureRenderWareLights(output, document.model, error))
    {
        return false;
    }

    if (document.collisionExportMode ==
        CollisionExportMode::PreserveSource)
    {
        return true;
    }

    if (!document.hasCollision)
    {
        error = "No collision was made.";
        return false;
    }

    const std::vector<std::uint8_t> collision =
        BuildSampCol3(document.collision);

    return EmbedSampCollision(output, collision, error);
}

bool DffExporter::EnsureRenderWareLights(
    std::vector<std::uint8_t>& dffBytes,
    const ModelData& model,
    std::string& error) const
{
    if (model.omniLightCount == 0)
    {
        return true;
    }

    ChunkLocation clump{};
    if (!ReadChunk(dffBytes, 0, dffBytes.size(), clump) ||
        clump.type != ChunkClump)
    {
        error = "The source file does not contain a valid root clump.";
        return false;
    }

    ChunkLocation clumpStruct{};
    ChunkLocation firstClumpExtension{};
    bool foundStruct = false;
    bool foundExtension = false;

    struct ExistingLight
    {
        ChunkLocation frameSection{};
        ChunkLocation lightSection{};
    };

    std::vector<ExistingLight> existingLights;
    std::size_t actualLightChunkCount = 0;
    std::size_t unpairedLightChunkCount = 0;
    ChunkLocation pendingFrameSection{};
    bool hasPendingFrameSection = false;
    std::size_t cursor = clump.dataOffset;

    while (cursor + RenderWareChunkHeaderSize <= clump.endOffset)
    {
        ChunkLocation child{};
        if (!ReadChunk(dffBytes, cursor, clump.endOffset, child))
        {
            error = "The source DFF contains a damaged clump section.";
            return false;
        }

        if (!foundStruct && child.type == ChunkStruct)
        {
            clumpStruct = child;
            foundStruct = true;
        }

        if (!foundExtension && child.type == ChunkExtension)
        {
            firstClumpExtension = child;
            foundExtension = true;
        }

        if (child.type == ChunkStruct && child.length == 4)
        {
            pendingFrameSection = child;
            hasPendingFrameSection = true;
        }
        else if (child.type == ChunkLight)
        {
            ++actualLightChunkCount;
            if (hasPendingFrameSection)
            {
                existingLights.push_back({pendingFrameSection, child});
            }
            else
            {
                ++unpairedLightChunkCount;
            }
            hasPendingFrameSection = false;
        }
        else
        {
            hasPendingFrameSection = false;
        }

        cursor = child.endOffset;
    }

    if (!foundStruct || clumpStruct.length < 12)
    {
        error = "The clump does not have room for its Light section count.";
        return false;
    }

    struct PendingLight
    {
        std::uint32_t frameIndex = 0;
        const Effect2D* effect = nullptr;
    };

    std::vector<PendingLight> pendingLights;
    std::vector<bool> usedFrames(model.frames.size(), false);

    auto addGeometryLights = [&](const Geometry& geometry, const Mat4& transform)
    {
        for (const Effect2D& effect : geometry.effects2d)
        {
            if (effect.type != 0)
            {
                continue;
            }

            const Vec3 position = TransformPoint(transform, effect.position);
            std::size_t bestFrame = model.frames.size();
            float bestDistanceSquared = std::numeric_limits<float>::max();

            for (std::size_t frameIndex = 0;
                 frameIndex < model.frames.size();
                 ++frameIndex)
            {
                if (usedFrames[frameIndex])
                {
                    continue;
                }

                const Mat4& world = model.frames[frameIndex].worldTransform;
                const Vec3 framePosition{world.m[12], world.m[13], world.m[14]};
                const Vec3 difference = framePosition - position;
                const float distanceSquared = Dot(difference, difference);

                if (distanceSquared < bestDistanceSquared)
                {
                    bestDistanceSquared = distanceSquared;
                    bestFrame = frameIndex;
                }
            }

            if (bestFrame < model.frames.size() &&
                bestDistanceSquared <= 0.01f)
            {
                usedFrames[bestFrame] = true;
                pendingLights.push_back({
                    static_cast<std::uint32_t>(bestFrame),
                    &effect});
            }
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
                static_cast<std::size_t>(atomic.frameIndex) < model.frames.size())
            {
                transform = model.frames[
                    static_cast<std::size_t>(atomic.frameIndex)].worldTransform;
            }

            addGeometryLights(
                model.geometries[
                    static_cast<std::size_t>(atomic.geometryIndex)],
                transform);
        }
    }
    else
    {
        for (const Geometry& geometry : model.geometries)
        {
            addGeometryLights(geometry, Mat4{});
        }
    }

    if (pendingLights.size() != model.omniLightCount)
    {
        error = "A 2DFX light does not have a matching Light frame.";
        return false;
    }

    std::sort(
        pendingLights.begin(),
        pendingLights.end(),
        [](const PendingLight& left, const PendingLight& right)
        {
            return left.frameIndex < right.frameIndex;
        });

    if (actualLightChunkCount != 0)
    {
        if (unpairedLightChunkCount != 0 ||
            existingLights.size() != actualLightChunkCount)
        {
            error = "The source DFF contains an unpaired or damaged Light section.";
            return false;
        }

        if (existingLights.size() != pendingLights.size())
        {
            error = "The actual RenderWare Light chunks do not match the 2DFX omni lights.";
            return false;
        }

        for (std::size_t index = 0; index < pendingLights.size(); ++index)
        {
            const PendingLight& expected = pendingLights[index];
            const ExistingLight& existing = existingLights[index];

            ChunkLocation lightStruct{};
            if (!ReadChunk(
                    dffBytes,
                    existing.lightSection.dataOffset,
                    existing.lightSection.endOffset,
                    lightStruct) ||
                lightStruct.type != ChunkStruct ||
                lightStruct.length < 24)
            {
                error = "The source DFF contains a damaged Light value section.";
                return false;
            }

            WriteU32(
                dffBytes,
                existing.frameSection.dataOffset,
                expected.frameIndex);

            const std::size_t valueOffset = lightStruct.dataOffset;

            // The RenderWare Light radius is not the 2DFX point-light range.
            // Preserve the source radius: stock traffic lights use 1.0 while
            // normal SA building lights commonly use 200.0.
            WriteF32(
                dffBytes,
                valueOffset + 4,
                static_cast<float>(expected.effect->color.r) / 255.0f);
            WriteF32(
                dffBytes,
                valueOffset + 8,
                static_cast<float>(expected.effect->color.g) / 255.0f);
            WriteF32(
                dffBytes,
                valueOffset + 12,
                static_cast<float>(expected.effect->color.b) / 255.0f);
            WriteF32(dffBytes, valueOffset + 16, 0.0f);
            WriteU16(dffBytes, valueOffset + 20, 3);
            WriteU16(dffBytes, valueOffset + 22, 0x80);
        }

        // Preserve the original declared count. GTA SA files are inconsistent:
        // some valid files declare zero but still contain real Light chunks.
        return true;
    }

    std::vector<std::uint8_t> lightSections;
    const float newLightRadius = model.trafficLightSignature ? 1.0f : 200.0f;

    for (const PendingLight& light : pendingLights)
    {
        std::vector<std::uint8_t> framePayload;
        AppendU32(framePayload, light.frameIndex);
        const std::vector<std::uint8_t> frameSection =
            BuildChunk(ChunkStruct, clump.version, framePayload);

        std::vector<std::uint8_t> lightStructPayload;
        AppendF32(lightStructPayload, newLightRadius);
        AppendF32(lightStructPayload,
            static_cast<float>(light.effect->color.r) / 255.0f);
        AppendF32(lightStructPayload,
            static_cast<float>(light.effect->color.g) / 255.0f);
        AppendF32(lightStructPayload,
            static_cast<float>(light.effect->color.b) / 255.0f);
        AppendF32(lightStructPayload, 0.0f);
        AppendU16(lightStructPayload, 3);
        AppendU16(lightStructPayload, 0x80);

        const std::vector<std::uint8_t> lightStruct =
            BuildChunk(ChunkStruct, clump.version, lightStructPayload);
        const std::vector<std::uint8_t> lightExtension =
            BuildChunk(ChunkExtension, clump.version, {});

        std::vector<std::uint8_t> lightPayload;
        lightPayload.insert(
            lightPayload.end(), lightStruct.begin(), lightStruct.end());
        lightPayload.insert(
            lightPayload.end(), lightExtension.begin(), lightExtension.end());
        const std::vector<std::uint8_t> lightSection =
            BuildChunk(ChunkLight, clump.version, lightPayload);

        lightSections.insert(
            lightSections.end(), frameSection.begin(), frameSection.end());
        lightSections.insert(
            lightSections.end(), lightSection.begin(), lightSection.end());
    }

    const std::size_t insertOffset = foundExtension
        ? firstClumpExtension.headerOffset
        : clump.endOffset;

    dffBytes.insert(
        dffBytes.begin() + static_cast<std::ptrdiff_t>(insertOffset),
        lightSections.begin(),
        lightSections.end());

    const std::uint32_t declaredCount = model.trafficLightSignature
        ? static_cast<std::uint32_t>(pendingLights.size())
        : 0;
    WriteU32(dffBytes, clumpStruct.dataOffset + 4, declaredCount);

    const std::uint64_t newClumpLength =
        static_cast<std::uint64_t>(clump.length) + lightSections.size();
    if (newClumpLength > std::numeric_limits<std::uint32_t>::max())
    {
        error = "The rebuilt DFF is too large.";
        return false;
    }

    WriteU32(dffBytes, 4, static_cast<std::uint32_t>(newClumpLength));
    return true;
}
bool DffExporter::RemoveEmbeddedCollision(
    std::vector<std::uint8_t>& dffBytes,
    bool& removedCollision,
    std::string& error) const
{
    removedCollision = false;
    ChunkLocation clump{};

    if (!ReadChunk(dffBytes, 0, dffBytes.size(), clump) ||
        clump.type != ChunkClump)
    {
        error = "The source file does not contain a valid root RenderWare clump.";
        return false;
    }

    std::vector<std::uint8_t> clumpPayload;
    clumpPayload.reserve(clump.length);

    bool removedAnyCollision = false;
    std::size_t cursor = clump.dataOffset;

    while (cursor + RenderWareChunkHeaderSize <= clump.endOffset)
    {
        ChunkLocation child{};

        if (!ReadChunk(dffBytes, cursor, clump.endOffset, child))
        {
            error = "The source DFF contains a malformed top-level clump child.";
            return false;
        }

        if (child.type != ChunkExtension)
        {
            clumpPayload.insert(
                clumpPayload.end(),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(
                    child.headerOffset),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(
                    child.endOffset));

            cursor = child.endOffset;
            continue;
        }

        std::vector<std::uint8_t> extensionPayload;
        extensionPayload.reserve(child.length);
        bool removedCollisionFromExtension = false;
        std::size_t pluginCursor = child.dataOffset;

        while (pluginCursor + RenderWareChunkHeaderSize <= child.endOffset)
        {
            ChunkLocation plugin{};

            if (!ReadChunk(
                    dffBytes,
                    pluginCursor,
                    child.endOffset,
                    plugin))
            {
                extensionPayload.insert(
                    extensionPayload.end(),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        pluginCursor),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        child.endOffset));

                pluginCursor = child.endOffset;
                break;
            }

            if (IsCollisionPlugin(plugin.type))
            {
                removedAnyCollision = true;
                removedCollisionFromExtension = true;
            }
            else
            {
                extensionPayload.insert(
                    extensionPayload.end(),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        plugin.headerOffset),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        plugin.endOffset));
            }

            pluginCursor = plugin.endOffset;
        }

        if (pluginCursor < child.endOffset)
        {
            extensionPayload.insert(
                extensionPayload.end(),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(pluginCursor),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(child.endOffset));
        }

        if (!extensionPayload.empty() ||
            !removedCollisionFromExtension)
        {
            const std::vector<std::uint8_t> rebuiltExtension =
                BuildChunk(ChunkExtension, child.version, extensionPayload);

            clumpPayload.insert(
                clumpPayload.end(),
                rebuiltExtension.begin(),
                rebuiltExtension.end());
        }

        cursor = child.endOffset;
    }

    if (cursor < clump.endOffset)
    {
        clumpPayload.insert(
            clumpPayload.end(),
            dffBytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            dffBytes.begin() + static_cast<std::ptrdiff_t>(clump.endOffset));
    }

    if (!removedAnyCollision)
    {
        return true;
    }

    removedCollision = true;

    const std::vector<std::uint8_t> rebuiltClump =
        BuildChunk(ChunkClump, clump.version, clumpPayload);

    std::vector<std::uint8_t> rebuilt;
    rebuilt.reserve(
        dffBytes.size() -
        (clump.endOffset - clump.headerOffset) +
        rebuiltClump.size());

    rebuilt.insert(
        rebuilt.end(),
        dffBytes.begin(),
        dffBytes.begin() + static_cast<std::ptrdiff_t>(clump.headerOffset));

    rebuilt.insert(
        rebuilt.end(),
        rebuiltClump.begin(),
        rebuiltClump.end());

    rebuilt.insert(
        rebuilt.end(),
        dffBytes.begin() + static_cast<std::ptrdiff_t>(clump.endOffset),
        dffBytes.end());

    dffBytes = std::move(rebuilt);
    return true;
}

bool DffExporter::EmbedSampCollision(
    std::vector<std::uint8_t>& dffBytes,
    const std::vector<std::uint8_t>& col3Bytes,
    std::string& error) const
{
    bool removedExistingCollision = false;
    if (!RemoveEmbeddedCollision(
            dffBytes,
            removedExistingCollision,
            error))
    {
        return false;
    }

    ChunkLocation clump{};

    if (!ReadChunk(dffBytes, 0, dffBytes.size(), clump) ||
        clump.type != ChunkClump)
    {
        error = "The source file does not contain a valid root RenderWare clump.";
        return false;
    }

    ChunkLocation clumpExtension{};
    bool foundClumpExtension = false;

    std::size_t cursor = clump.dataOffset;

    while (cursor + RenderWareChunkHeaderSize <= clump.endOffset)
    {
        ChunkLocation child{};

        if (!ReadChunk(dffBytes, cursor, clump.endOffset, child))
        {
            error = "The source DFF contains a malformed top-level clump child.";
            return false;
        }

        if (child.type == ChunkExtension)
        {
            clumpExtension = child;
            foundClumpExtension = true;
        }

        cursor = child.endOffset;
    }

    const std::vector<std::uint8_t> collisionPlugin =
        BuildChunk(SampCollisionPluginId, clump.version, col3Bytes);

    std::vector<std::uint8_t> replacementExtension;

    if (foundClumpExtension)
    {
        std::vector<std::uint8_t> extensionPayload;
        std::size_t pluginCursor = clumpExtension.dataOffset;

        while (pluginCursor + RenderWareChunkHeaderSize <= clumpExtension.endOffset)
        {
            ChunkLocation plugin{};

            if (!ReadChunk(
                    dffBytes,
                    pluginCursor,
                    clumpExtension.endOffset,
                    plugin))
            {
                extensionPayload.insert(
                    extensionPayload.end(),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(pluginCursor),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        clumpExtension.endOffset));

                pluginCursor = clumpExtension.endOffset;
                break;
            }

            if (!IsCollisionPlugin(plugin.type))
            {
                extensionPayload.insert(
                    extensionPayload.end(),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        plugin.headerOffset),
                    dffBytes.begin() + static_cast<std::ptrdiff_t>(
                        plugin.endOffset));
            }

            pluginCursor = plugin.endOffset;
        }

        if (pluginCursor < clumpExtension.endOffset)
        {
            extensionPayload.insert(
                extensionPayload.end(),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(pluginCursor),
                dffBytes.begin() + static_cast<std::ptrdiff_t>(
                    clumpExtension.endOffset));
        }

        extensionPayload.insert(
            extensionPayload.end(),
            collisionPlugin.begin(),
            collisionPlugin.end());

        replacementExtension = BuildChunk(
            ChunkExtension,
            clumpExtension.version,
            extensionPayload);
    }
    else
    {
        replacementExtension = BuildChunk(
            ChunkExtension,
            clump.version,
            collisionPlugin);
    }

    std::vector<std::uint8_t> rebuilt;
    rebuilt.reserve(
        dffBytes.size() +
        replacementExtension.size() +
        collisionPlugin.size());

    std::size_t replacedStart = clump.endOffset;
    std::size_t replacedEnd = clump.endOffset;

    if (foundClumpExtension)
    {
        replacedStart = clumpExtension.headerOffset;
        replacedEnd = clumpExtension.endOffset;
    }

    rebuilt.insert(
        rebuilt.end(),
        dffBytes.begin(),
        dffBytes.begin() + static_cast<std::ptrdiff_t>(replacedStart));

    rebuilt.insert(
        rebuilt.end(),
        replacementExtension.begin(),
        replacementExtension.end());

    rebuilt.insert(
        rebuilt.end(),
        dffBytes.begin() + static_cast<std::ptrdiff_t>(replacedEnd),
        dffBytes.end());

    const std::ptrdiff_t delta =
        static_cast<std::ptrdiff_t>(replacementExtension.size()) -
        static_cast<std::ptrdiff_t>(replacedEnd - replacedStart);

    const std::int64_t newClumpLength =
        static_cast<std::int64_t>(clump.length) +
        static_cast<std::int64_t>(delta);

    if (newClumpLength < 0 ||
        newClumpLength > std::numeric_limits<std::uint32_t>::max())
    {
        error = "The rebuilt clump length is outside the RenderWare limit.";
        return false;
    }

    WriteU32(
        rebuilt,
        4,
        static_cast<std::uint32_t>(newClumpLength));

    dffBytes = std::move(rebuilt);
    return true;
}

std::vector<std::uint8_t> DffExporter::BuildSampCol3(
    const CollisionData& collision) const
{
    std::vector<std::uint8_t> sectionData;

    const bool writeBox =
        collision.mode == CollisionMode::Box &&
        collision.bounds.valid;

    const bool writeMesh =
        collision.mode == CollisionMode::MeshFaces &&
        !collision.vertices.empty() &&
        !collision.faces.empty();

    const std::uint16_t sphereCount = 0;
    const std::uint16_t boxCount = writeBox ? 1 : 0;

    const std::size_t limitedFaceCount = std::min<std::size_t>(
        collision.faces.size(),
        std::numeric_limits<std::uint16_t>::max());

    const std::uint16_t faceCount = writeMesh
        ? static_cast<std::uint16_t>(limitedFaceCount)
        : 0;

    std::uint32_t flags = 0;

    if (writeBox || writeMesh)
    {
        flags |= 2;
    }

    const std::uint32_t spheresOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    const std::uint32_t boxesOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    if (writeBox)
    {
        AppendF32(sectionData, collision.bounds.minimum.x);
        AppendF32(sectionData, collision.bounds.minimum.y);
        AppendF32(sectionData, collision.bounds.minimum.z);

        AppendF32(sectionData, collision.bounds.maximum.x);
        AppendF32(sectionData, collision.bounds.maximum.y);
        AppendF32(sectionData, collision.bounds.maximum.z);

        AppendU8(sectionData, 0);
        AppendU8(sectionData, 0);
        AppendU8(sectionData, 255);
        AppendU8(sectionData, 255);
    }

    const std::uint32_t linesOffset = 0;

    const std::uint32_t verticesOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    if (writeMesh)
    {
        for (const Vec3& vertex : collision.vertices)
        {
            AppendI16(sectionData, CompressCoordinate(vertex.x));
            AppendI16(sectionData, CompressCoordinate(vertex.y));
            AppendI16(sectionData, CompressCoordinate(vertex.z));
        }
    }

    const std::uint32_t facesOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    if (writeMesh)
    {
        for (std::size_t index = 0; index < limitedFaceCount; ++index)
        {
            const CollisionFace& face = collision.faces[index];

            AppendU16(sectionData, face.a);
            AppendU16(sectionData, face.b);
            AppendU16(sectionData, face.c);
            AppendU8(sectionData, face.material);
            AppendU8(sectionData, face.light);
        }
    }

    const std::uint32_t trianglePlanesOffset = 0;

    const std::uint32_t shadowFaceCount = 0;
    const std::uint32_t shadowVerticesOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    const std::uint32_t shadowFacesOffset =
        Col3HeaderOffsetBase +
        static_cast<std::uint32_t>(sectionData.size());

    Bounds bounds = collision.bounds;

    if (!bounds.valid)
    {
        bounds.minimum = {};
        bounds.maximum = {};
        bounds.valid = true;
    }

    const Vec3 center = bounds.Center();
    const float radius = bounds.Radius();

    std::vector<std::uint8_t> body;
    body.reserve(112 + sectionData.size());

    AppendF32(body, bounds.minimum.x);
    AppendF32(body, bounds.minimum.y);
    AppendF32(body, bounds.minimum.z);

    AppendF32(body, bounds.maximum.x);
    AppendF32(body, bounds.maximum.y);
    AppendF32(body, bounds.maximum.z);

    AppendF32(body, center.x);
    AppendF32(body, center.y);
    AppendF32(body, center.z);
    AppendF32(body, radius);

    AppendU16(body, sphereCount);
    AppendU16(body, boxCount);
    AppendU16(body, faceCount);
    AppendU8(body, 0);
    AppendU8(body, 0);

    AppendU32(body, flags);
    AppendU32(body, spheresOffset);
    AppendU32(body, boxesOffset);
    AppendU32(body, linesOffset);
    AppendU32(body, verticesOffset);
    AppendU32(body, facesOffset);
    AppendU32(body, trianglePlanesOffset);

    AppendU32(body, shadowFaceCount);
    AppendU32(body, shadowVerticesOffset);
    AppendU32(body, shadowFacesOffset);

    body.insert(body.end(), sectionData.begin(), sectionData.end());

    std::vector<std::uint8_t> file;
    file.reserve(32 + body.size());

    AppendU8(file, static_cast<std::uint8_t>('C'));
    AppendU8(file, static_cast<std::uint8_t>('O'));
    AppendU8(file, static_cast<std::uint8_t>('L'));
    AppendU8(file, static_cast<std::uint8_t>('3'));

    AppendU32(
        file,
        static_cast<std::uint32_t>(24 + body.size()));

    const char sampName[] = "samp";

    for (std::size_t index = 0; index < 22; ++index)
    {
        const std::uint8_t value = index < sizeof(sampName) - 1
            ? static_cast<std::uint8_t>(sampName[index])
            : 0;

        AppendU8(file, value);
    }

    AppendU16(file, 0);
    file.insert(file.end(), body.begin(), body.end());

    return file;
}

bool DffExporter::ReadChunk(
    const std::vector<std::uint8_t>& bytes,
    std::size_t headerOffset,
    std::size_t containerEnd,
    ChunkLocation& chunk) const
{
    if (headerOffset > containerEnd ||
        containerEnd > bytes.size() ||
        containerEnd - headerOffset < RenderWareChunkHeaderSize)
    {
        return false;
    }

    chunk.type = ReadU32(bytes, headerOffset);
    chunk.length = ReadU32(bytes, headerOffset + 4);
    chunk.version = ReadU32(bytes, headerOffset + 8);
    chunk.headerOffset = headerOffset;
    chunk.dataOffset = headerOffset + RenderWareChunkHeaderSize;

    if (chunk.length > containerEnd - chunk.dataOffset)
    {
        return false;
    }

    chunk.endOffset = chunk.dataOffset + chunk.length;
    return true;
}

std::vector<std::uint8_t> DffExporter::BuildChunk(
    std::uint32_t type,
    std::uint32_t version,
    const std::vector<std::uint8_t>& payload) const
{
    std::vector<std::uint8_t> chunk;
    chunk.reserve(RenderWareChunkHeaderSize + payload.size());

    AppendU32(chunk, type);
    AppendU32(chunk, static_cast<std::uint32_t>(payload.size()));
    AppendU32(chunk, version);
    chunk.insert(chunk.end(), payload.begin(), payload.end());

    return chunk;
}

std::uint32_t DffExporter::ReadU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void DffExporter::WriteU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void DffExporter::WriteU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void DffExporter::WriteF32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(bytes, offset, bits);
}

void DffExporter::AppendU8(
    std::vector<std::uint8_t>& bytes,
    std::uint8_t value)
{
    bytes.push_back(value);
}

void DffExporter::AppendU16(
    std::vector<std::uint8_t>& bytes,
    std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void DffExporter::AppendI16(
    std::vector<std::uint8_t>& bytes,
    std::int16_t value)
{
    AppendU16(bytes, static_cast<std::uint16_t>(value));
}

void DffExporter::AppendU32(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void DffExporter::AppendF32(
    std::vector<std::uint8_t>& bytes,
    float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(bytes, bits);
}

std::int16_t DffExporter::CompressCoordinate(float value)
{
    if (!std::isfinite(value))
    {
        return 0;
    }

    const float scaled = std::round(value * 128.0f);
    const float clamped = std::clamp(
        scaled,
        static_cast<float>(std::numeric_limits<std::int16_t>::min()),
        static_cast<float>(std::numeric_limits<std::int16_t>::max()));

    return static_cast<std::int16_t>(clamped);
}
