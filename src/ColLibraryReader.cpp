#include "ColLibraryReader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
    constexpr std::size_t ColFileHeaderSize = 32;
    constexpr float CompressedVertexScale = 1.0f / 128.0f;

    bool IsKnownSignature(const std::string& signature)
    {
        return
            signature == "COLL" ||
            signature == "COL2" ||
            signature == "COL3" ||
            signature == "COL4";
    }

    bool IsFiniteBounds(const Bounds& bounds)
    {
        return
            bounds.valid &&
            std::isfinite(bounds.minimum.x) &&
            std::isfinite(bounds.minimum.y) &&
            std::isfinite(bounds.minimum.z) &&
            std::isfinite(bounds.maximum.x) &&
            std::isfinite(bounds.maximum.y) &&
            std::isfinite(bounds.maximum.z);
    }
}

bool ColLibraryReader::Load(
    const std::filesystem::path& path,
    std::unordered_map<std::string, ColLibraryEntry>& entries,
    std::string& error) const
{
    entries.clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open the COL library.";
        return false;
    }

    const std::istreambuf_iterator<char> begin(stream);
    const std::istreambuf_iterator<char> end;
    const std::vector<char> source(begin, end);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(source.size());
    for (char value : source)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
    }

    if (bytes.size() < 8)
    {
        error = "The COL library is too small.";
        return false;
    }

    std::size_t offset = 0;
    std::size_t parsedCount = 0;

    while (offset + 8 <= bytes.size())
    {
        while (offset < bytes.size() && bytes[offset] == 0)
        {
            ++offset;
        }

        if (offset + 8 > bytes.size())
        {
            break;
        }

        const std::string signature(
            reinterpret_cast<const char*>(bytes.data() + offset),
            4);

        if (!IsKnownSignature(signature))
        {
            error =
                "Unexpected data in COL library at offset " +
                std::to_string(offset) + ".";
            return false;
        }

        const std::uint32_t storedSize = ReadU32(bytes, offset + 4);
        const std::size_t entrySize = static_cast<std::size_t>(storedSize) + 8;

        if (entrySize < ColFileHeaderSize ||
            entrySize > bytes.size() - offset)
        {
            error =
                "A COL entry has an invalid size at offset " +
                std::to_string(offset) + ".";
            return false;
        }

        ColLibraryEntry entry;
        if (!ParseEntry(bytes, offset, entrySize, entry, error))
        {
            return false;
        }

        const std::string key = NormalizeModelName(entry.name);
        if (!key.empty())
        {
            entries[key] = std::move(entry);
            ++parsedCount;
        }

        offset += entrySize;
    }

    if (parsedCount == 0)
    {
        error = "No named collision models were found in the COL library.";
        return false;
    }

    return true;
}

std::string ColLibraryReader::NormalizeModelName(const std::string& name)
{
    std::filesystem::path path(name);
    std::string normalized = path.stem().string();

    if (normalized.empty())
    {
        normalized = name;
    }

    while (!normalized.empty() &&
           (normalized.back() == '\0' || normalized.back() == ' '))
    {
        normalized.pop_back();
    }

    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });

    return normalized;
}

bool ColLibraryReader::ParseEntry(
    const std::vector<std::uint8_t>& bytes,
    std::size_t entryOffset,
    std::size_t entrySize,
    ColLibraryEntry& entry,
    std::string& error) const
{
    if (entrySize < ColFileHeaderSize)
    {
        error = "COL entry header is truncated.";
        return false;
    }

    entry.format.assign(
        reinterpret_cast<const char*>(bytes.data() + entryOffset),
        4);

    std::size_t nameLength = 0;
    while (nameLength < 22 &&
           bytes[entryOffset + 8 + nameLength] != 0)
    {
        ++nameLength;
    }

    entry.name.assign(
        reinterpret_cast<const char*>(bytes.data() + entryOffset + 8),
        nameLength);

    entry.rawBytes.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(entryOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(entryOffset + entrySize));

    if (entry.format == "COL2" ||
        entry.format == "COL3" ||
        entry.format == "COL4")
    {
        entry.previewAvailable = ParseModernCollision(
            entry.rawBytes,
            entry.format,
            entry.collision);
    }

    return true;
}

bool ColLibraryReader::ParseModernCollision(
    const std::vector<std::uint8_t>& bytes,
    const std::string& format,
    CollisionData& collision) const
{
    if (bytes.size() < 108)
    {
        return false;
    }

    collision = {};
    collision.mode = CollisionMode::MeshFaces;

    collision.bounds.minimum = {
        ReadF32(bytes, 32),
        ReadF32(bytes, 36),
        ReadF32(bytes, 40)};

    collision.bounds.maximum = {
        ReadF32(bytes, 44),
        ReadF32(bytes, 48),
        ReadF32(bytes, 52)};

    collision.bounds.valid = true;

    if (!IsFiniteBounds(collision.bounds))
    {
        collision.bounds = {};
    }

    const std::uint16_t sphereCount = ReadU16(bytes, 72);
    const std::uint16_t boxCount = ReadU16(bytes, 74);
    const std::uint16_t faceCount = ReadU16(bytes, 76);

    const std::uint32_t encodedBoxesOffset = ReadU32(bytes, 88);
    const std::uint32_t encodedVerticesOffset = ReadU32(bytes, 96);
    const std::uint32_t encodedFacesOffset = ReadU32(bytes, 100);

    auto decodeOffset = [&](std::uint32_t encoded) -> std::size_t
    {
        if (encoded == 0)
        {
            return 0;
        }

        // GTA COL2/COL3/COL4 offsets are based four bytes after the
        // beginning of the collision model. ODFF's COL3 writer follows the
        // same convention.
        return static_cast<std::size_t>(encoded) + 4;
    };

    const std::size_t boxesOffset = decodeOffset(encodedBoxesOffset);
    const std::size_t verticesOffset = decodeOffset(encodedVerticesOffset);
    const std::size_t facesOffset = decodeOffset(encodedFacesOffset);

    if (boxCount != 0 &&
        boxesOffset != 0 &&
        boxesOffset + static_cast<std::size_t>(boxCount) * 28 <= bytes.size())
    {
        for (std::size_t boxIndex = 0; boxIndex < boxCount; ++boxIndex)
        {
            const std::size_t base = boxesOffset + boxIndex * 28;

            const Vec3 minimum{
                ReadF32(bytes, base + 0),
                ReadF32(bytes, base + 4),
                ReadF32(bytes, base + 8)};

            const Vec3 maximum{
                ReadF32(bytes, base + 12),
                ReadF32(bytes, base + 16),
                ReadF32(bytes, base + 20)};

            const std::uint8_t material = bytes[base + 24];
            const std::uint8_t light = bytes[base + 25];

            if (!std::isfinite(minimum.x) ||
                !std::isfinite(minimum.y) ||
                !std::isfinite(minimum.z) ||
                !std::isfinite(maximum.x) ||
                !std::isfinite(maximum.y) ||
                !std::isfinite(maximum.z))
            {
                continue;
            }

            const std::size_t vertexBase = collision.vertices.size();
            if (vertexBase + 8 > std::numeric_limits<std::uint16_t>::max())
            {
                break;
            }

            collision.vertices.insert(
                collision.vertices.end(),
                {
                    {minimum.x, minimum.y, minimum.z},
                    {maximum.x, minimum.y, minimum.z},
                    {maximum.x, maximum.y, minimum.z},
                    {minimum.x, maximum.y, minimum.z},
                    {minimum.x, minimum.y, maximum.z},
                    {maximum.x, minimum.y, maximum.z},
                    {maximum.x, maximum.y, maximum.z},
                    {minimum.x, maximum.y, maximum.z}
                });

            const std::uint16_t v = static_cast<std::uint16_t>(vertexBase);
            const std::uint16_t triangles[12][3] = {
                {0, 2, 1}, {0, 3, 2},
                {4, 5, 6}, {4, 6, 7},
                {0, 1, 5}, {0, 5, 4},
                {1, 2, 6}, {1, 6, 5},
                {2, 3, 7}, {2, 7, 6},
                {3, 0, 4}, {3, 4, 7}
            };

            for (const auto& triangle : triangles)
            {
                collision.faces.push_back({
                    static_cast<std::uint16_t>(v + triangle[0]),
                    static_cast<std::uint16_t>(v + triangle[1]),
                    static_cast<std::uint16_t>(v + triangle[2]),
                    material,
                    light});
            }
        }
    }

    if (faceCount != 0 &&
        verticesOffset != 0 &&
        facesOffset != 0 &&
        verticesOffset < facesOffset &&
        facesOffset + static_cast<std::size_t>(faceCount) * 8 <= bytes.size())
    {
        const std::size_t vertexBytes = facesOffset - verticesOffset;
        const std::size_t vertexCount = vertexBytes / 6;

        if (vertexCount <= std::numeric_limits<std::uint16_t>::max() &&
            verticesOffset + vertexCount * 6 <= bytes.size())
        {
            const std::size_t meshVertexBase = collision.vertices.size();
            if (meshVertexBase + vertexCount <=
                std::numeric_limits<std::uint16_t>::max())
            {
                for (std::size_t vertexIndex = 0;
                     vertexIndex < vertexCount;
                     ++vertexIndex)
                {
                    const std::size_t base = verticesOffset + vertexIndex * 6;
                    collision.vertices.push_back({
                        static_cast<float>(ReadI16(bytes, base + 0)) * CompressedVertexScale,
                        static_cast<float>(ReadI16(bytes, base + 2)) * CompressedVertexScale,
                        static_cast<float>(ReadI16(bytes, base + 4)) * CompressedVertexScale});
                }

                for (std::size_t faceIndex = 0;
                     faceIndex < faceCount;
                     ++faceIndex)
                {
                    const std::size_t base = facesOffset + faceIndex * 8;
                    const std::uint16_t a = ReadU16(bytes, base + 0);
                    const std::uint16_t b = ReadU16(bytes, base + 2);
                    const std::uint16_t c = ReadU16(bytes, base + 4);

                    if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                    {
                        continue;
                    }

                    collision.faces.push_back({
                        static_cast<std::uint16_t>(meshVertexBase + a),
                        static_cast<std::uint16_t>(meshVertexBase + b),
                        static_cast<std::uint16_t>(meshVertexBase + c),
                        bytes[base + 6],
                        bytes[base + 7]});
                }
            }
        }
    }

    (void)sphereCount;
    (void)format;

    return collision.bounds.valid || !collision.faces.empty();
}

std::uint16_t ColLibraryReader::ReadU16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t ColLibraryReader::ReadU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::int16_t ColLibraryReader::ReadI16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return static_cast<std::int16_t>(ReadU16(bytes, offset));
}

float ColLibraryReader::ReadF32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    const std::uint32_t bits = ReadU32(bytes, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
