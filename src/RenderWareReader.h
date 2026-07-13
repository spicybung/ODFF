#pragma once

#include "ModelTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class RenderWareReader
{
public:
    bool LoadDff(const std::filesystem::path& path, ModelData& model, std::string& error);

private:
    struct ChunkHeader
    {
        std::uint32_t type = 0;
        std::uint32_t length = 0;
        std::uint32_t version = 0;
        std::size_t dataOffset = 0;
        std::size_t endOffset = 0;
    };

    class BinaryReader
    {
    public:
        explicit BinaryReader(std::vector<std::uint8_t> bytes);

        bool CanRead(std::size_t count) const;
        std::size_t Position() const;
        std::size_t Size() const;
        void Seek(std::size_t offset);
        void Skip(std::size_t count);

        std::uint8_t ReadU8();
        std::uint16_t ReadU16();
        std::uint32_t ReadU32();
        std::int32_t ReadI32();
        float ReadF32();
        std::string ReadFixedString(std::size_t length);
        std::vector<std::uint8_t> ReadBytes(std::size_t count);

    private:
        std::vector<std::uint8_t> data;
        std::size_t position = 0;
    };

    bool ReadChunkHeader(BinaryReader& reader, ChunkHeader& header);
    bool FindChildChunk(BinaryReader& reader, std::size_t parentEnd, std::uint32_t wantedType, ChunkHeader& result);
    bool ParseClump(BinaryReader& reader, const ChunkHeader& clump, ModelData& model, std::string& error);
    bool ParseFrameList(BinaryReader& reader, const ChunkHeader& frameList, ModelData& model, std::string& error);
    bool ParseGeometryList(BinaryReader& reader, const ChunkHeader& geometryList, ModelData& model, std::string& error);
    bool ParseGeometry(BinaryReader& reader, const ChunkHeader& geometryChunk, Geometry& geometry, std::string& error);
    bool ParseMaterialList(BinaryReader& reader, const ChunkHeader& materialList, Geometry& geometry);
    bool ParseMaterial(BinaryReader& reader, const ChunkHeader& materialChunk, MaterialInfo& material);
    bool ParseAtomic(BinaryReader& reader, const ChunkHeader& atomicChunk, ModelData& model);
    void ParseFrameExtension(BinaryReader& reader, const ChunkHeader& extension, Frame& frame);
    void BuildWorldTransforms(ModelData& model);
    void BuildBounds(ModelData& model);
};
