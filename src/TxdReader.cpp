#include "TxdReader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
    constexpr std::uint32_t ChunkStruct = 0x00000001;
    constexpr std::uint32_t ChunkTextureDictionary = 0x00000016;
    constexpr std::uint32_t ChunkTextureNative = 0x00000015;

    constexpr std::uint32_t RasterFormatMask = 0x00000F00;
    constexpr std::uint32_t RasterFormat1555 = 0x00000100;
    constexpr std::uint32_t RasterFormat565 = 0x00000200;
    constexpr std::uint32_t RasterFormat4444 = 0x00000300;
    constexpr std::uint32_t RasterFormatLuminance8 = 0x00000400;
    constexpr std::uint32_t RasterFormat8888 = 0x00000500;
    constexpr std::uint32_t RasterFormat888 = 0x00000600;
    constexpr std::uint32_t RasterFormatPal8 = 0x00002000;
    constexpr std::uint32_t RasterFormatPal4 = 0x00004000;

    constexpr std::uint32_t D3dFormatR8G8B8 = 20;
    constexpr std::uint32_t D3dFormatA8R8G8B8 = 21;
    constexpr std::uint32_t D3dFormatX8R8G8B8 = 22;
    constexpr std::uint32_t D3dFormatR5G6B5 = 23;
    constexpr std::uint32_t D3dFormatX1R5G5B5 = 24;
    constexpr std::uint32_t D3dFormatA1R5G5B5 = 25;
    constexpr std::uint32_t D3dFormatA4R4G4B4 = 26;
    constexpr std::uint32_t D3dFormatL8 = 50;
    constexpr std::uint32_t D3dFormatA8L8 = 51;

    constexpr std::uint32_t MakeFourCc(char a, char b, char c, char d)
    {
        return
            static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
    }

    constexpr std::uint32_t D3dFormatDxt1 = MakeFourCc('D', 'X', 'T', '1');
    constexpr std::uint32_t D3dFormatDxt3 = MakeFourCc('D', 'X', 'T', '3');
    constexpr std::uint32_t D3dFormatDxt5 = MakeFourCc('D', 'X', 'T', '5');

    struct Rgba
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
    };

    std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t& offset)
    {
        if (offset + 2 > bytes.size())
        {
            throw std::runtime_error("TXD is truncated.");
        }

        const std::uint16_t value =
            static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1]) << 8);

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

    std::uint16_t ReadU16At(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset)
    {
        if (offset + 2 > bytes.size())
        {
            throw std::runtime_error("TXD mip data is truncated.");
        }

        return
            static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::uint32_t ReadU32At(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset)
    {
        if (offset + 4 > bytes.size())
        {
            throw std::runtime_error("TXD mip data is truncated.");
        }

        return
            static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    std::string ReadName(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::size_t length)
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

    std::uint8_t Expand5(std::uint16_t value)
    {
        return static_cast<std::uint8_t>((value << 3) | (value >> 2));
    }

    std::uint8_t Expand6(std::uint16_t value)
    {
        return static_cast<std::uint8_t>((value << 2) | (value >> 4));
    }

    Rgba Decode565(std::uint16_t value)
    {
        return {
            Expand5(static_cast<std::uint16_t>((value >> 11) & 31)),
            Expand6(static_cast<std::uint16_t>((value >> 5) & 63)),
            Expand5(static_cast<std::uint16_t>(value & 31)),
            255
        };
    }

    Rgba Interpolate(const Rgba& first, const Rgba& second, int firstWeight, int divisor)
    {
        const int secondWeight = divisor - firstWeight;
        return {
            static_cast<std::uint8_t>(
                (first.r * firstWeight + second.r * secondWeight) / divisor),
            static_cast<std::uint8_t>(
                (first.g * firstWeight + second.g * secondWeight) / divisor),
            static_cast<std::uint8_t>(
                (first.b * firstWeight + second.b * secondWeight) / divisor),
            static_cast<std::uint8_t>(
                (first.a * firstWeight + second.a * secondWeight) / divisor)
        };
    }

    void WritePixel(
        TxdMipLevel& mip,
        std::uint32_t x,
        std::uint32_t y,
        const Rgba& color)
    {
        if (x >= mip.width || y >= mip.height)
        {
            return;
        }

        const std::size_t pixelOffset =
            (static_cast<std::size_t>(y) * mip.width + x) * 4;

        mip.rgbaPixels[pixelOffset + 0] = color.r;
        mip.rgbaPixels[pixelOffset + 1] = color.g;
        mip.rgbaPixels[pixelOffset + 2] = color.b;
        mip.rgbaPixels[pixelOffset + 3] = color.a;
    }

    std::array<Rgba, 4> DecodeColorPalette(
        std::uint16_t endpoint0,
        std::uint16_t endpoint1,
        bool allowDxt1Transparency)
    {
        std::array<Rgba, 4> palette{};
        palette[0] = Decode565(endpoint0);
        palette[1] = Decode565(endpoint1);

        if (allowDxt1Transparency && endpoint0 <= endpoint1)
        {
            palette[2] = Interpolate(palette[0], palette[1], 1, 2);
            palette[3] = {0, 0, 0, 0};
        }
        else
        {
            palette[2] = Interpolate(palette[0], palette[1], 2, 3);
            palette[3] = Interpolate(palette[0], palette[1], 1, 3);
        }

        return palette;
    }

    bool DecodeDxt(
        const std::vector<std::uint8_t>& data,
        std::uint32_t format,
        TxdMipLevel& mip,
        std::string& error)
    {
        const std::size_t blockSize = format == D3dFormatDxt1 ? 8 : 16;
        const std::size_t blocksWide = (mip.width + 3) / 4;
        const std::size_t blocksHigh = (mip.height + 3) / 4;
        const std::size_t requiredSize = blocksWide * blocksHigh * blockSize;

        if (data.size() < requiredSize)
        {
            error = "Compressed mip data is shorter than its declared dimensions.";
            return false;
        }

        std::size_t offset = 0;

        for (std::size_t blockY = 0; blockY < blocksHigh; ++blockY)
        {
            for (std::size_t blockX = 0; blockX < blocksWide; ++blockX)
            {
                std::array<std::uint8_t, 16> alpha{};
                alpha.fill(255);

                if (format == D3dFormatDxt3)
                {
                    const std::uint32_t alphaLow = ReadU32At(data, offset);
                    const std::uint32_t alphaHigh = ReadU32At(data, offset + 4);
                    const std::uint64_t alphaBits =
                        static_cast<std::uint64_t>(alphaLow) |
                        (static_cast<std::uint64_t>(alphaHigh) << 32);

                    for (std::size_t pixel = 0; pixel < 16; ++pixel)
                    {
                        alpha[pixel] = static_cast<std::uint8_t>(
                            ((alphaBits >> (pixel * 4)) & 0x0F) * 17);
                    }

                    offset += 8;
                }
                else if (format == D3dFormatDxt5)
                {
                    const std::uint8_t alpha0 = data[offset + 0];
                    const std::uint8_t alpha1 = data[offset + 1];
                    std::array<std::uint8_t, 8> alphaPalette{};
                    alphaPalette[0] = alpha0;
                    alphaPalette[1] = alpha1;

                    if (alpha0 > alpha1)
                    {
                        for (int index = 1; index <= 6; ++index)
                        {
                            alphaPalette[index + 1] = static_cast<std::uint8_t>(
                                ((7 - index) * alpha0 + index * alpha1) / 7);
                        }
                    }
                    else
                    {
                        for (int index = 1; index <= 4; ++index)
                        {
                            alphaPalette[index + 1] = static_cast<std::uint8_t>(
                                ((5 - index) * alpha0 + index * alpha1) / 5);
                        }
                        alphaPalette[6] = 0;
                        alphaPalette[7] = 255;
                    }

                    std::uint64_t alphaBits = 0;
                    for (int byteIndex = 0; byteIndex < 6; ++byteIndex)
                    {
                        alphaBits |=
                            static_cast<std::uint64_t>(data[offset + 2 + byteIndex]) <<
                            (byteIndex * 8);
                    }

                    for (std::size_t pixel = 0; pixel < 16; ++pixel)
                    {
                        alpha[pixel] = alphaPalette[
                            static_cast<std::size_t>(
                                (alphaBits >> (pixel * 3)) & 0x07)];
                    }

                    offset += 8;
                }

                const std::uint16_t endpoint0 = ReadU16At(data, offset + 0);
                const std::uint16_t endpoint1 = ReadU16At(data, offset + 2);
                const std::uint32_t colorBits = ReadU32At(data, offset + 4);
                offset += 8;

                const std::array<Rgba, 4> palette = DecodeColorPalette(
                    endpoint0,
                    endpoint1,
                    format == D3dFormatDxt1);

                for (std::size_t localY = 0; localY < 4; ++localY)
                {
                    for (std::size_t localX = 0; localX < 4; ++localX)
                    {
                        const std::size_t pixel = localY * 4 + localX;
                        Rgba color = palette[
                            static_cast<std::size_t>(
                                (colorBits >> (pixel * 2)) & 0x03)];

                        if (format != D3dFormatDxt1 || color.a != 0)
                        {
                            color.a = alpha[pixel];
                        }

                        WritePixel(
                            mip,
                            static_cast<std::uint32_t>(blockX * 4 + localX),
                            static_cast<std::uint32_t>(blockY * 4 + localY),
                            color);
                    }
                }
            }
        }

        return true;
    }

    Rgba Decode16BitColor(std::uint16_t value, std::uint32_t format)
    {
        if (format == D3dFormatR5G6B5 || format == RasterFormat565)
        {
            return Decode565(value);
        }

        if (format == D3dFormatA4R4G4B4 || format == RasterFormat4444)
        {
            return {
                static_cast<std::uint8_t>(((value >> 8) & 15) * 17),
                static_cast<std::uint8_t>(((value >> 4) & 15) * 17),
                static_cast<std::uint8_t>((value & 15) * 17),
                static_cast<std::uint8_t>(((value >> 12) & 15) * 17)
            };
        }

        const bool hasAlpha =
            format == D3dFormatA1R5G5B5 || format == RasterFormat1555;

        return {
            Expand5(static_cast<std::uint16_t>((value >> 10) & 31)),
            Expand5(static_cast<std::uint16_t>((value >> 5) & 31)),
            Expand5(static_cast<std::uint16_t>(value & 31)),
            static_cast<std::uint8_t>(
                !hasAlpha || (value & 0x8000) != 0 ? 255 : 0)
        };
    }

    bool DecodePaletted(
        const std::vector<std::uint8_t>& data,
        const std::vector<Rgba>& palette,
        bool pal4,
        TxdMipLevel& mip,
        std::string& error)
    {
        const std::size_t pixelCount =
            static_cast<std::size_t>(mip.width) * mip.height;
        const std::size_t requiredSize = pal4 ? (pixelCount + 1) / 2 : pixelCount;

        if (data.size() < requiredSize || palette.empty())
        {
            error = "Paletted mip data or palette is truncated.";
            return false;
        }

        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            std::size_t paletteIndex = 0;
            if (pal4)
            {
                const std::uint8_t packed = data[pixel / 2];
                paletteIndex =
                    (pixel & 1) == 0 ? packed & 0x0F : packed >> 4;
            }
            else
            {
                paletteIndex = data[pixel];
            }

            if (paletteIndex >= palette.size())
            {
                error = "Mip references a palette entry that does not exist.";
                return false;
            }

            WritePixel(
                mip,
                static_cast<std::uint32_t>(pixel % mip.width),
                static_cast<std::uint32_t>(pixel / mip.width),
                palette[paletteIndex]);
        }

        return true;
    }

    bool DecodeUncompressed(
        const std::vector<std::uint8_t>& data,
        std::uint32_t d3dFormat,
        std::uint32_t rasterFormat,
        TxdMipLevel& mip,
        std::string& error)
    {
        std::uint32_t format = d3dFormat;
        if (format == 0)
        {
            format = rasterFormat & RasterFormatMask;
        }

        std::size_t bytesPerPixel = 0;
        switch (format)
        {
            case D3dFormatA8R8G8B8:
            case D3dFormatX8R8G8B8:
            case RasterFormat8888:
                bytesPerPixel = 4;
                break;

            case D3dFormatR8G8B8:
            case RasterFormat888:
                bytesPerPixel = 3;
                break;

            case D3dFormatR5G6B5:
            case D3dFormatX1R5G5B5:
            case D3dFormatA1R5G5B5:
            case D3dFormatA4R4G4B4:
            case RasterFormat1555:
            case RasterFormat565:
            case RasterFormat4444:
            case D3dFormatA8L8:
                bytesPerPixel = 2;
                break;

            case D3dFormatL8:
            case RasterFormatLuminance8:
                bytesPerPixel = 1;
                break;

            default:
                error = "Unsupported uncompressed D3D9 raster format " +
                    std::to_string(d3dFormat) + ".";
                return false;
        }

        const std::size_t minimumStride =
            static_cast<std::size_t>(mip.width) * bytesPerPixel;
        const std::size_t alignedStride = (minimumStride + 3) & ~std::size_t(3);
        const std::size_t stride =
            data.size() >= alignedStride * mip.height
                ? alignedStride
                : minimumStride;

        if (data.size() < stride * mip.height)
        {
            error = "Uncompressed mip data is truncated.";
            return false;
        }

        for (std::uint32_t y = 0; y < mip.height; ++y)
        {
            for (std::uint32_t x = 0; x < mip.width; ++x)
            {
                const std::size_t source =
                    static_cast<std::size_t>(y) * stride +
                    static_cast<std::size_t>(x) * bytesPerPixel;

                Rgba color{};
                if (bytesPerPixel == 4)
                {
                    color = {
                        data[source + 2],
                        data[source + 1],
                        data[source + 0],
                        static_cast<std::uint8_t>(
                            format == D3dFormatX8R8G8B8 ? 255 : data[source + 3])
                    };
                }
                else if (bytesPerPixel == 3)
                {
                    color = {
                        data[source + 2],
                        data[source + 1],
                        data[source + 0],
                        255
                    };
                }
                else if (format == D3dFormatA8L8)
                {
                    color = {
                        data[source],
                        data[source],
                        data[source],
                        data[source + 1]
                    };
                }
                else if (bytesPerPixel == 2)
                {
                    color = Decode16BitColor(ReadU16At(data, source), format);
                }
                else
                {
                    color = {
                        data[source],
                        data[source],
                        data[source],
                        255
                    };
                }

                WritePixel(mip, x, y, color);
            }
        }

        return true;
    }

    bool DecodeMip(
        const std::vector<std::uint8_t>& data,
        const TxdTextureInfo& texture,
        const std::vector<Rgba>& palette,
        TxdMipLevel& mip,
        std::string& error)
    {
        if (texture.d3dFormat == D3dFormatDxt1 ||
            texture.d3dFormat == D3dFormatDxt3 ||
            texture.d3dFormat == D3dFormatDxt5)
        {
            return DecodeDxt(data, texture.d3dFormat, mip, error);
        }

        if ((texture.rasterFormat & RasterFormatPal8) != 0)
        {
            return DecodePaletted(data, palette, false, mip, error);
        }

        if ((texture.rasterFormat & RasterFormatPal4) != 0)
        {
            return DecodePaletted(data, palette, true, mip, error);
        }

        return DecodeUncompressed(
            data,
            texture.d3dFormat,
            texture.rasterFormat,
            mip,
            error);
    }

    bool ContainsAlpha(const TxdMipLevel& mip)
    {
        for (std::size_t offset = 3; offset < mip.rgbaPixels.size(); offset += 4)
        {
            if (mip.rgbaPixels[offset] < 255)
            {
                return true;
            }
        }
        return false;
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

        if (structType != ChunkStruct || structLength > rootEnd - offset || structLength < 4)
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
            if (chunkType != ChunkTextureNative || offset + 12 > chunkEnd)
            {
                offset = chunkEnd;
                continue;
            }

            const std::uint32_t nativeStructType = ReadU32(bytes, offset);
            const std::uint32_t nativeStructLength = ReadU32(bytes, offset);
            ReadU32(bytes, offset);

            if (nativeStructType != ChunkStruct ||
                nativeStructLength > chunkEnd - offset ||
                nativeStructLength < 88)
            {
                offset = chunkEnd;
                continue;
            }

            const std::size_t nativeStructEnd = offset + nativeStructLength;
            TxdTextureInfo info{};
            info.platformId = ReadU32(bytes, offset);
            info.filterAddressing = ReadU32(bytes, offset);

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
            info.rasterType = bytes.at(offset++);
            info.compression = bytes.at(offset++);

            if (info.platformId != 8 && info.platformId != 9)
            {
                info.decodeError = "Only D3D8 and D3D9 native textures are currently decoded.";
            }

            std::vector<Rgba> palette;
            std::size_t paletteEntries = 0;
            if ((info.rasterFormat & RasterFormatPal8) != 0)
            {
                paletteEntries = 256;
            }
            else if ((info.rasterFormat & RasterFormatPal4) != 0)
            {
                paletteEntries = 16;
            }

            if (paletteEntries != 0)
            {
                if (offset + paletteEntries * 4 > nativeStructEnd)
                {
                    info.decodeError = "Texture palette is truncated.";
                    offset = nativeStructEnd;
                }
                else
                {
                    palette.reserve(paletteEntries);
                    for (std::size_t index = 0; index < paletteEntries; ++index)
                    {
                        const std::uint8_t blue = bytes[offset++];
                        const std::uint8_t green = bytes[offset++];
                        const std::uint8_t red = bytes[offset++];
                        const std::uint8_t alpha = bytes[offset++];
                        palette.push_back({red, green, blue, alpha});
                    }
                }
            }

            const std::uint8_t levelCount = std::max<std::uint8_t>(info.mipmapCount, 1);
            std::uint16_t levelWidth = std::max<std::uint16_t>(info.width, 1);
            std::uint16_t levelHeight = std::max<std::uint16_t>(info.height, 1);

            for (std::uint8_t levelIndex = 0;
                 levelIndex < levelCount && offset < nativeStructEnd;
                 ++levelIndex)
            {
                if (offset + 4 > nativeStructEnd)
                {
                    if (info.decodeError.empty())
                    {
                        info.decodeError = "Mipmap size table is truncated.";
                    }
                    break;
                }

                const std::uint32_t mipSize = ReadU32(bytes, offset);
                if (mipSize > nativeStructEnd - offset)
                {
                    if (info.decodeError.empty())
                    {
                        info.decodeError = "Mipmap extends past the native texture chunk.";
                    }
                    break;
                }

                std::vector<std::uint8_t> mipBytes(
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + mipSize));
                offset += mipSize;

                TxdMipLevel mip{};
                mip.width = levelWidth;
                mip.height = levelHeight;
                mip.rgbaPixels.resize(
                    static_cast<std::size_t>(levelWidth) * levelHeight * 4);

                std::string decodeError;
                if (info.decodeError.empty() &&
                    DecodeMip(mipBytes, info, palette, mip, decodeError))
                {
                    info.hasAlpha = info.hasAlpha || ContainsAlpha(mip);
                    info.mipLevels.push_back(std::move(mip));
                }
                else if (info.decodeError.empty())
                {
                    info.decodeError = decodeError;
                }

                levelWidth = std::max<std::uint16_t>(1, levelWidth / 2);
                levelHeight = std::max<std::uint16_t>(1, levelHeight / 2);
            }

            txd.textures.push_back(std::move(info));
            offset = chunkEnd;
        }

        if (txd.textures.size() != textureCount)
        {
            error = "TXD texture count does not match its native texture chunks.";
            return false;
        }

        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
