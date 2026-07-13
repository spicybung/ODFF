#pragma once

#include "ModelDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class DffExporter
{
public:
    bool ExportDocument(
        const ModelDocument& document,
        const std::filesystem::path& outputPath,
        std::string& error) const;

private:
    struct ChunkLocation
    {
        std::uint32_t type = 0;
        std::uint32_t length = 0;
        std::uint32_t version = 0;
        std::size_t headerOffset = 0;
        std::size_t dataOffset = 0;
        std::size_t endOffset = 0;
    };

    bool BuildExportBytes(
        const ModelDocument& document,
        std::vector<std::uint8_t>& output,
        std::string& error) const;

    bool EmbedSampCollision(
        std::vector<std::uint8_t>& dffBytes,
        const std::vector<std::uint8_t>& col3Bytes,
        std::string& error) const;

    std::vector<std::uint8_t> BuildSampCol3(
        const CollisionData& collision) const;

    bool ReadChunk(
        const std::vector<std::uint8_t>& bytes,
        std::size_t headerOffset,
        std::size_t containerEnd,
        ChunkLocation& chunk) const;

    std::vector<std::uint8_t> BuildChunk(
        std::uint32_t type,
        std::uint32_t version,
        const std::vector<std::uint8_t>& payload) const;

    static std::uint32_t ReadU32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset);

    static void WriteU32(
        std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::uint32_t value);

    static void AppendU8(
        std::vector<std::uint8_t>& bytes,
        std::uint8_t value);

    static void AppendU16(
        std::vector<std::uint8_t>& bytes,
        std::uint16_t value);

    static void AppendI16(
        std::vector<std::uint8_t>& bytes,
        std::int16_t value);

    static void AppendU32(
        std::vector<std::uint8_t>& bytes,
        std::uint32_t value);

    static void AppendF32(
        std::vector<std::uint8_t>& bytes,
        float value);

    static std::int16_t CompressCoordinate(float value);
};
