#include "DffExporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace
{
    constexpr std::uint32_t ChunkExtension = 0x00000003;
    constexpr std::uint32_t ChunkClump = 0x00000010;

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

bool DffExporter::BuildExportBytes(
    const ModelDocument& document,
    std::vector<std::uint8_t>& output,
    std::string& error) const
{
    if (document.sourceBytes.empty())
    {
        error = "The original DFF bytes were not retained when the file was loaded.";
        return false;
    }

    output = document.sourceBytes;

    if (!document.hasCollision)
    {
        return true;
    }

    const std::vector<std::uint8_t> collision =
        BuildSampCol3(document.collision);

    return EmbedSampCollision(output, collision, error);
}

bool DffExporter::EmbedSampCollision(
    std::vector<std::uint8_t>& dffBytes,
    const std::vector<std::uint8_t>& col3Bytes,
    std::string& error) const
{
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
