#pragma once

#include "CollisionBuilder.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct ColLibraryEntry
{
    std::string name;
    std::string format;
    std::vector<std::uint8_t> rawBytes;
    CollisionData collision;
    bool previewAvailable = false;
};

class ColLibraryReader
{
public:
    bool Load(
        const std::filesystem::path& path,
        std::unordered_map<std::string, ColLibraryEntry>& entries,
        std::string& error) const;

    static std::string NormalizeModelName(const std::string& name);

private:
    bool ParseEntry(
        const std::vector<std::uint8_t>& bytes,
        std::size_t entryOffset,
        std::size_t entrySize,
        ColLibraryEntry& entry,
        std::string& error) const;

    bool ParseModernCollision(
        const std::vector<std::uint8_t>& entryBytes,
        const std::string& format,
        CollisionData& collision) const;

    static std::uint16_t ReadU16(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset);

    static std::uint32_t ReadU32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset);

    static std::int16_t ReadI16(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset);

    static float ReadF32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset);
};
