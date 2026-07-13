#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct TxdTextureInfo
{
    std::string name;
    std::string maskName;
    std::uint32_t platformId = 0;
    std::uint32_t rasterFormat = 0;
    std::uint32_t d3dFormat = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t depth = 0;
    std::uint8_t mipmapCount = 0;
};

struct TxdData
{
    std::filesystem::path sourcePath;
    std::vector<TxdTextureInfo> textures;
    std::uint32_t renderWareVersion = 0;
};

class TxdReader
{
public:
    bool Load(const std::filesystem::path& path, TxdData& txd, std::string& error);
};
