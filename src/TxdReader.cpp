#include "TxdReader.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace
{
    constexpr std::uint32_t ChunkStruct = 0x00000001;
    constexpr std::uint32_t ChunkTextureDictionary = 0x00000016;
    constexpr std::uint32_t ChunkTextureNative = 0x00000015;

    std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t& offset)
    {
        if (offset + 2 > bytes.size())
        {
            throw std::runtime_error("TXD is truncated.");
        }

        const std::uint16_t value =
            static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(bytes[offset + 1] << 8);

        offset += 2;
        return value;
    }

    std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t& offset)
    {
        if (offset + 4 > bytes.size())
        {
            throw std::runtime_error("TXD is truncated.");
        }

        const std::uint32_t value =
            static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);

        offset += 4;
        return value;
    }

    std::string ReadName(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length)
    {
        if (offset + length > bytes.size())
        {
            throw std::runtime_error("TXD name is truncated.");
        }

        std::size_t visibleLength = 0;
        while (visibleLength < length && bytes[offset + visibleLength] != 0)
        {
            ++visibleLength;
        }

        return std::string(
            reinterpret_cast<const char*>(bytes.data() + offset),
            visibleLength);
    }
}

bool TxdReader::Load(const std::filesystem::path& path, TxdData& txd, std::string& error)
{
    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Could not open TXD file.";
            return false;
        }

        const std::istreambuf_iterator<char> begin(stream);
        const std::istreambuf_iterator<char> end;
        std::vector<std::uint8_t> bytes(begin, end);

        std::size_t offset = 0;
        const std::uint32_t rootType = ReadU32(bytes, offset);
        const std::uint32_t rootLength = ReadU32(bytes, offset);
        const std::uint32_t rootVersion = ReadU32(bytes, offset);

        if (rootType != ChunkTextureDictionary || rootLength > bytes.size() - offset)
        {
            error = "The file does not begin with a RenderWare texture dictionary.";
            return false;
        }

        txd = {};
        txd.sourcePath = path;
        txd.renderWareVersion = rootVersion;

        const std::size_t rootEnd = offset + rootLength;

        if (offset + 12 > rootEnd)
        {
            error = "TXD dictionary struct is missing.";
            return false;
        }

        const std::uint32_t structType = ReadU32(bytes, offset);
        const std::uint32_t structLength = ReadU32(bytes, offset);
        ReadU32(bytes, offset);

        if (structType != ChunkStruct || structLength > rootEnd - offset)
        {
            error = "TXD dictionary struct is invalid.";
            return false;
        }

        const std::size_t structEnd = offset + structLength;
        const std::uint16_t textureCount = ReadU16(bytes, offset);
        ReadU16(bytes, offset);
        offset = structEnd;

        while (offset + 12 <= rootEnd && txd.textures.size() < textureCount)
        {
            const std::uint32_t chunkType = ReadU32(bytes, offset);
            const std::uint32_t chunkLength = ReadU32(bytes, offset);
            ReadU32(bytes, offset);

            if (chunkLength > rootEnd - offset)
            {
                error = "TXD child chunk extends past the dictionary.";
                return false;
            }

            const std::size_t chunkEnd = offset + chunkLength;

            if (chunkType == ChunkTextureNative && offset + 12 <= chunkEnd)
            {
                const std::uint32_t nativeStructType = ReadU32(bytes, offset);
                const std::uint32_t nativeStructLength = ReadU32(bytes, offset);
                ReadU32(bytes, offset);

                if (nativeStructType == ChunkStruct && nativeStructLength <= chunkEnd - offset)
                {
                    const std::size_t nativeStructEnd = offset + nativeStructLength;
                    TxdTextureInfo info{};

                    if (nativeStructLength >= 88)
                    {
                        info.platformId = ReadU32(bytes, offset);
                        ReadU32(bytes, offset);

                        info.name = ReadName(bytes, offset, 32);
                        offset += 32;

                        info.maskName = ReadName(bytes, offset, 32);
                        offset += 32;

                        info.rasterFormat = ReadU32(bytes, offset);
                        info.d3dFormat = ReadU32(bytes, offset);
                        info.width = ReadU16(bytes, offset);
                        info.height = ReadU16(bytes, offset);
                        info.depth = bytes.at(offset++);
                        info.mipmapCount = bytes.at(offset++);
                        offset += 2;
                    }

                    txd.textures.push_back(std::move(info));
                    offset = nativeStructEnd;
                }
            }

            offset = chunkEnd;
        }

        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
