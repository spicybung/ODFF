#include "RenderWareReader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace
{
    constexpr std::uint32_t ChunkStruct = 0x00000001;
    constexpr std::uint32_t ChunkString = 0x00000002;
    constexpr std::uint32_t ChunkExtension = 0x00000003;
    constexpr std::uint32_t ChunkTexture = 0x00000006;
    constexpr std::uint32_t ChunkMaterial = 0x00000007;
    constexpr std::uint32_t ChunkMaterialList = 0x00000008;
    constexpr std::uint32_t ChunkFrameList = 0x0000000E;
    constexpr std::uint32_t ChunkGeometry = 0x0000000F;
    constexpr std::uint32_t ChunkClump = 0x00000010;
    constexpr std::uint32_t ChunkLight = 0x00000012;
    constexpr std::uint32_t ChunkAtomic = 0x00000014;
    constexpr std::uint32_t ChunkGeometryList = 0x0000001A;
    constexpr std::uint32_t PluginFrameName = 0x0253F2FE;
    constexpr std::uint32_t Plugin2DFX = 0x0253F2F8;
    constexpr std::uint32_t PluginNormalCollision = 0x0253F2FA;
    constexpr std::uint32_t PluginSampCollision = 0x0253F2FF;

    constexpr std::uint16_t GeometryFlagTextured = 0x0004;
    constexpr std::uint16_t GeometryFlagPrelit = 0x0008;
    constexpr std::uint16_t GeometryFlagNormals = 0x0010;
    constexpr std::uint16_t GeometryFlagTextured2 = 0x0080;
    constexpr std::uint16_t GeometryFlagNative = 0x0100;

    std::string NormalizeName(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    }

    bool IsTrafficLightSignature(const ModelData& model)
    {
        if (model.omniLightCount != 6 ||
            model.effect2dCount != 6)
        {
            return false;
        }

        std::size_t redCount = 0;
        std::size_t amberCount = 0;
        std::size_t greenCount = 0;
        bool hasTrafficTexture = false;

        for (const Geometry& geometry : model.geometries)
        {
            for (const MaterialInfo& material : geometry.materials)
            {
                const std::string textureName = NormalizeName(material.textureName);
                if (textureName.find("traffic") != std::string::npos ||
                    textureName.find("taffic") != std::string::npos)
                {
                    hasTrafficTexture = true;
                }
            }

            for (const Effect2D& effect : geometry.effects2d)
            {
                if (effect.type != 0)
                {
                    continue;
                }

                const Color4& color = effect.color;
                if (color.r >= 200 && color.g <= 64 && color.b <= 64)
                {
                    ++redCount;
                }
                else if (color.r >= 200 && color.g >= 80 && color.g <= 200 && color.b <= 64)
                {
                    ++amberCount;
                }
                else if (color.g >= 200 && color.r <= 64 && color.b <= 64)
                {
                    ++greenCount;
                }
            }
        }

        return
            hasTrafficTexture &&
            redCount == 2 &&
            amberCount == 2 &&
            greenCount == 2;
    }
}

RenderWareReader::BinaryReader::BinaryReader(std::vector<std::uint8_t> bytes)
    : data(std::move(bytes))
{
}

bool RenderWareReader::BinaryReader::CanRead(std::size_t count) const
{
    return position <= data.size() && count <= data.size() - position;
}

std::size_t RenderWareReader::BinaryReader::Position() const
{
    return position;
}

std::size_t RenderWareReader::BinaryReader::Size() const
{
    return data.size();
}

void RenderWareReader::BinaryReader::Seek(std::size_t offset)
{
    if (offset > data.size())
    {
        throw std::runtime_error("Seek past end of file");
    }
    position = offset;
}

void RenderWareReader::BinaryReader::Skip(std::size_t count)
{
    Seek(position + count);
}

std::uint8_t RenderWareReader::BinaryReader::ReadU8()
{
    if (!CanRead(1))
    {
        throw std::runtime_error("Unexpected end of file");
    }
    return data[position++];
}

std::uint16_t RenderWareReader::BinaryReader::ReadU16()
{
    if (!CanRead(2))
    {
        throw std::runtime_error("Unexpected end of file");
    }

    const std::uint16_t value =
        static_cast<std::uint16_t>(data[position]) |
        static_cast<std::uint16_t>(data[position + 1] << 8);

    position += 2;
    return value;
}

std::uint32_t RenderWareReader::BinaryReader::ReadU32()
{
    if (!CanRead(4))
    {
        throw std::runtime_error("Unexpected end of file");
    }

    const std::uint32_t value =
        static_cast<std::uint32_t>(data[position]) |
        (static_cast<std::uint32_t>(data[position + 1]) << 8) |
        (static_cast<std::uint32_t>(data[position + 2]) << 16) |
        (static_cast<std::uint32_t>(data[position + 3]) << 24);

    position += 4;
    return value;
}

std::int32_t RenderWareReader::BinaryReader::ReadI32()
{
    return static_cast<std::int32_t>(ReadU32());
}

float RenderWareReader::BinaryReader::ReadF32()
{
    const std::uint32_t bits = ReadU32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string RenderWareReader::BinaryReader::ReadFixedString(std::size_t length)
{
    const std::vector<std::uint8_t> bytes = ReadBytes(length);
    std::size_t visibleLength = 0;

    while (visibleLength < bytes.size() && bytes[visibleLength] != 0)
    {
        ++visibleLength;
    }

    return std::string(reinterpret_cast<const char*>(bytes.data()), visibleLength);
}

std::vector<std::uint8_t> RenderWareReader::BinaryReader::ReadBytes(std::size_t count)
{
    if (!CanRead(count))
    {
        throw std::runtime_error("Unexpected end of file");
    }

    std::vector<std::uint8_t> result(
        data.begin() + static_cast<std::ptrdiff_t>(position),
        data.begin() + static_cast<std::ptrdiff_t>(position + count));

    position += count;
    return result;
}

bool RenderWareReader::LoadDff(const std::filesystem::path& path, ModelData& model, std::string& error)
{
    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Could not open DFF file.";
            return false;
        }

        const std::istreambuf_iterator<char> begin(stream);
        const std::istreambuf_iterator<char> end;
        std::vector<std::uint8_t> bytes(begin, end);

        BinaryReader reader(std::move(bytes));
        ChunkHeader root{};

        if (!ReadChunkHeader(reader, root) || root.type != ChunkClump)
        {
            error = "The file does not begin with a RenderWare clump chunk.";
            return false;
        }

        model = {};
        model.renderWareVersion = root.version;

        if (!ParseClump(reader, root, model, error))
        {
            return false;
        }

        for (const Geometry& geometry : model.geometries)
        {
            model.effect2dCount += geometry.effects2d.size();
            model.omniLightCount += static_cast<std::size_t>(std::count_if(
                geometry.effects2d.begin(),
                geometry.effects2d.end(),
                [](const Effect2D& effect)
                {
                    return effect.type == 0;
                }));
        }

        model.trafficLightSignature = IsTrafficLightSignature(model);

        BuildWorldTransforms(model);
        BuildBounds(model);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool RenderWareReader::ReadChunkHeader(BinaryReader& reader, ChunkHeader& header)
{
    if (!reader.CanRead(12))
    {
        return false;
    }

    header.type = reader.ReadU32();
    header.length = reader.ReadU32();
    header.version = reader.ReadU32();
    header.dataOffset = reader.Position();

    if (header.length > reader.Size() - header.dataOffset)
    {
        return false;
    }

    header.endOffset = header.dataOffset + header.length;
    return true;
}

bool RenderWareReader::FindChildChunk(
    BinaryReader& reader,
    std::size_t parentEnd,
    std::uint32_t wantedType,
    ChunkHeader& result)
{
    while (reader.Position() + 12 <= parentEnd)
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            return false;
        }

        if (child.endOffset > parentEnd)
        {
            return false;
        }

        if (child.type == wantedType)
        {
            result = child;
            return true;
        }

        reader.Seek(child.endOffset);
    }

    return false;
}

bool RenderWareReader::ParseClump(
    BinaryReader& reader,
    const ChunkHeader& clump,
    ModelData& model,
    std::string& error)
{
    reader.Seek(clump.dataOffset);

    ChunkHeader clumpStruct{};
    if (!FindChildChunk(reader, clump.endOffset, ChunkStruct, clumpStruct))
    {
        error = "Clump struct chunk is missing.";
        return false;
    }

    reader.Seek(clumpStruct.dataOffset);
    if (!reader.CanRead(4))
    {
        error = "Clump struct is truncated.";
        return false;
    }

    const std::uint32_t atomicCount = reader.ReadU32();
    if (clumpStruct.endOffset - reader.Position() >= 8)
    {
        model.declaredRenderWareLightCount = reader.ReadU32();
        reader.ReadU32();
    }
    reader.Seek(clumpStruct.endOffset);

    while (reader.Position() + 12 <= clump.endOffset)
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            break;
        }

        switch (child.type)
        {
            case ChunkFrameList:
                if (!ParseFrameList(reader, child, model, error))
                {
                    return false;
                }
                break;

            case ChunkGeometryList:
                if (!ParseGeometryList(reader, child, model, error))
                {
                    return false;
                }
                break;

            case ChunkAtomic:
                ParseAtomic(reader, child, model);
                break;

            case ChunkLight:
                ++model.renderWareLightCount;
                break;

            case ChunkExtension:
                ParseClumpExtension(reader, child, model);
                break;

            default:
                break;
        }

        reader.Seek(child.endOffset);
    }

    if (atomicCount != 0 && model.atomics.empty())
    {
        error = "Clump reports atomics, but no atomic chunks were parsed.";
        return false;
    }

    return true;
}

void RenderWareReader::ParseClumpExtension(
    BinaryReader& reader,
    const ChunkHeader& extension,
    ModelData& model)
{
    reader.Seek(extension.dataOffset);

    while (reader.Position() + 12 <= extension.endOffset)
    {
        ChunkHeader plugin{};
        if (!ReadChunkHeader(reader, plugin) || plugin.endOffset > extension.endOffset)
        {
            break;
        }

        if (plugin.type == PluginNormalCollision)
        {
            model.hasNormalCollision = true;
            model.normalCollisionValid =
                model.normalCollisionValid || plugin.length != 0;
        }
        else if (plugin.type == PluginSampCollision)
        {
            model.hasSampCollision = true;

            if (plugin.length >= 32)
            {
                reader.Seek(plugin.dataOffset);
                const std::uint32_t magic = reader.ReadU32();
                const std::uint32_t declaredSize = reader.ReadU32();
                model.sampCollisionValid =
                    model.sampCollisionValid ||
                    (magic == 0x334C4F43 &&
                     declaredSize <= plugin.length - 8);
            }
        }

        reader.Seek(plugin.endOffset);
    }
}

bool RenderWareReader::ParseFrameList(
    BinaryReader& reader,
    const ChunkHeader& frameList,
    ModelData& model,
    std::string& error)
{
    reader.Seek(frameList.dataOffset);

    ChunkHeader frameStruct{};
    if (!FindChildChunk(reader, frameList.endOffset, ChunkStruct, frameStruct))
    {
        error = "Frame list struct is missing.";
        return false;
    }

    reader.Seek(frameStruct.dataOffset);
    const std::uint32_t frameCount = reader.ReadU32();

    model.frames.clear();
    model.frames.reserve(frameCount);

    for (std::uint32_t index = 0; index < frameCount; ++index)
    {
        Frame frame{};

        const Vec3 right{reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
        const Vec3 up{reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
        const Vec3 at{reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
        const Vec3 position{reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};

        frame.localTransform.m[0] = right.x;
        frame.localTransform.m[1] = right.y;
        frame.localTransform.m[2] = right.z;

        frame.localTransform.m[4] = up.x;
        frame.localTransform.m[5] = up.y;
        frame.localTransform.m[6] = up.z;

        frame.localTransform.m[8] = at.x;
        frame.localTransform.m[9] = at.y;
        frame.localTransform.m[10] = at.z;

        frame.localTransform.m[12] = position.x;
        frame.localTransform.m[13] = position.y;
        frame.localTransform.m[14] = position.z;

        frame.parentIndex = reader.ReadI32();
        reader.ReadU32();

        model.frames.push_back(frame);
    }

    reader.Seek(frameStruct.endOffset);

    std::size_t frameExtensionIndex = 0;
    while (reader.Position() + 12 <= frameList.endOffset && frameExtensionIndex < model.frames.size())
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            break;
        }

        if (child.type == ChunkExtension)
        {
            ParseFrameExtension(reader, child, model.frames[frameExtensionIndex]);
            ++frameExtensionIndex;
        }

        reader.Seek(child.endOffset);
    }

    return true;
}

void RenderWareReader::ParseFrameExtension(
    BinaryReader& reader,
    const ChunkHeader& extension,
    Frame& frame)
{
    reader.Seek(extension.dataOffset);

    while (reader.Position() + 12 <= extension.endOffset)
    {
        ChunkHeader plugin{};
        if (!ReadChunkHeader(reader, plugin))
        {
            break;
        }

        if (plugin.type == PluginFrameName)
        {
            reader.Seek(plugin.dataOffset);
            frame.name = reader.ReadFixedString(plugin.length);
        }

        reader.Seek(plugin.endOffset);
    }
}

bool RenderWareReader::ParseGeometryList(
    BinaryReader& reader,
    const ChunkHeader& geometryList,
    ModelData& model,
    std::string& error)
{
    reader.Seek(geometryList.dataOffset);

    ChunkHeader listStruct{};
    if (!FindChildChunk(reader, geometryList.endOffset, ChunkStruct, listStruct))
    {
        error = "Geometry list struct is missing.";
        return false;
    }

    reader.Seek(listStruct.dataOffset);
    const std::uint32_t geometryCount = reader.ReadU32();
    reader.Seek(listStruct.endOffset);

    model.geometries.clear();
    model.geometries.reserve(geometryCount);

    while (reader.Position() + 12 <= geometryList.endOffset && model.geometries.size() < geometryCount)
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            break;
        }

        if (child.type == ChunkGeometry)
        {
            Geometry geometry{};
            geometry.name = "Geometry " + std::to_string(model.geometries.size());

            if (!ParseGeometry(reader, child, geometry, error))
            {
                return false;
            }

            model.geometries.push_back(std::move(geometry));
        }

        reader.Seek(child.endOffset);
    }

    if (model.geometries.size() != geometryCount)
    {
        std::ostringstream message;
        message << "Expected " << geometryCount << " geometries but parsed "
                << model.geometries.size() << ".";
        error = message.str();
        return false;
    }

    return true;
}

bool RenderWareReader::ParseGeometry(
    BinaryReader& reader,
    const ChunkHeader& geometryChunk,
    Geometry& geometry,
    std::string& error)
{
    reader.Seek(geometryChunk.dataOffset);

    ChunkHeader geometryStruct{};
    if (!FindChildChunk(reader, geometryChunk.endOffset, ChunkStruct, geometryStruct))
    {
        error = "Geometry struct is missing.";
        return false;
    }

    reader.Seek(geometryStruct.dataOffset);

    const std::uint16_t flags = reader.ReadU16();
    geometry.flags = flags;
    std::uint8_t texCoordSetCount = reader.ReadU8();
    reader.ReadU8();

    const std::uint32_t triangleCount = reader.ReadU32();
    const std::uint32_t vertexCount = reader.ReadU32();
    const std::uint32_t morphTargetCount = reader.ReadU32();

    if ((flags & GeometryFlagNative) != 0)
    {
        error = "Native/pre-instanced geometry is not supported by the generic reader yet.";
        return false;
    }

    if (texCoordSetCount == 0)
    {
        if ((flags & GeometryFlagTextured2) != 0)
        {
            texCoordSetCount = 2;
        }
        else if ((flags & GeometryFlagTextured) != 0)
        {
            texCoordSetCount = 1;
        }
    }

    if ((flags & GeometryFlagPrelit) != 0)
    {
        geometry.colors.resize(vertexCount);
        for (Color4& color : geometry.colors)
        {
            color.r = reader.ReadU8();
            color.g = reader.ReadU8();
            color.b = reader.ReadU8();
            color.a = reader.ReadU8();
        }
    }

    if (texCoordSetCount > 0)
    {
        geometry.texCoords.resize(vertexCount);

        for (std::uint8_t setIndex = 0; setIndex < texCoordSetCount; ++setIndex)
        {
            for (std::uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            {
                const Vec2 value{reader.ReadF32(), reader.ReadF32()};
                if (setIndex == 0)
                {
                    geometry.texCoords[vertexIndex] = value;
                }
            }
        }
    }

    geometry.triangles.resize(triangleCount);
    for (Triangle& triangle : geometry.triangles)
    {
        const std::uint16_t vertex2 = reader.ReadU16();
        const std::uint16_t vertex1 = reader.ReadU16();
        const std::uint16_t material = reader.ReadU16();
        const std::uint16_t vertex0 = reader.ReadU16();

        triangle.a = vertex0;
        triangle.b = vertex1;
        triangle.c = vertex2;
        triangle.materialIndex = material;
    }

    bool foundVertices = false;

    for (std::uint32_t morphIndex = 0; morphIndex < morphTargetCount; ++morphIndex)
    {
        reader.ReadF32();
        reader.ReadF32();
        reader.ReadF32();
        reader.ReadF32();

        const std::uint32_t hasVertices = reader.ReadU32();
        const std::uint32_t hasNormals = reader.ReadU32();

        if (hasVertices != 0)
        {
            std::vector<Vec3> vertices(vertexCount);
            for (Vec3& vertex : vertices)
            {
                vertex = {reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
            }

            if (!foundVertices)
            {
                geometry.vertices = std::move(vertices);
                foundVertices = true;
            }
        }

        if (hasNormals != 0)
        {
            std::vector<Vec3> normals(vertexCount);
            for (Vec3& normal : normals)
            {
                normal = {reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
            }

            if (geometry.normals.empty())
            {
                geometry.normals = std::move(normals);
            }
        }
    }

    if (!foundVertices)
    {
        error = "Geometry contains no morph target vertices.";
        return false;
    }

    for (const Vec3& vertex : geometry.vertices)
    {
        geometry.bounds.Expand(vertex);
    }

    reader.Seek(geometryStruct.endOffset);

    while (reader.Position() + 12 <= geometryChunk.endOffset)
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            break;
        }

        if (child.type == ChunkMaterialList)
        {
            if (!ParseMaterialList(reader, child, geometry))
            {
                error = "Geometry material list is invalid or incomplete.";
                return false;
            }
        }
        else if (child.type == ChunkExtension)
        {
            ParseGeometryExtension(reader, child, geometry);
        }

        reader.Seek(child.endOffset);
    }

    return true;
}

void RenderWareReader::ParseGeometryExtension(
    BinaryReader& reader,
    const ChunkHeader& extension,
    Geometry& geometry)
{
    reader.Seek(extension.dataOffset);

    while (reader.Position() + 12 <= extension.endOffset)
    {
        ChunkHeader plugin{};
        if (!ReadChunkHeader(reader, plugin) || plugin.endOffset > extension.endOffset)
        {
            break;
        }

        if (plugin.type == Plugin2DFX)
        {
            Parse2DFX(reader, plugin, geometry);
        }

        reader.Seek(plugin.endOffset);
    }
}

void RenderWareReader::Parse2DFX(
    BinaryReader& reader,
    const ChunkHeader& plugin,
    Geometry& geometry)
{
    reader.Seek(plugin.dataOffset);
    if (!reader.CanRead(4))
    {
        return;
    }

    const std::uint32_t count = reader.ReadU32();
    geometry.effects2d.reserve(geometry.effects2d.size() + count);

    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (reader.Position() + 20 > plugin.endOffset)
        {
            break;
        }

        Effect2D effect{};
        effect.position = {reader.ReadF32(), reader.ReadF32(), reader.ReadF32()};
        effect.type = reader.ReadU32();
        const std::size_t payloadSize = reader.ReadU32();

        if (payloadSize > plugin.endOffset - reader.Position())
        {
            break;
        }

        const std::size_t payloadStart = reader.Position();

        if (effect.type == 0 && payloadSize >= 76)
        {
            effect.color = {
                reader.ReadU8(), reader.ReadU8(),
                reader.ReadU8(), reader.ReadU8()};
            effect.coronaFarClip = reader.ReadF32();
            effect.pointLightRange = reader.ReadF32();
            effect.coronaSize = reader.ReadF32();
            effect.shadowSize = reader.ReadF32();
            reader.Skip(4);
            effect.flags1 = reader.ReadU8();
            reader.Skip(48);
            reader.ReadU8();
            effect.flags2 = reader.ReadU8();
        }

        geometry.effects2d.push_back(effect);
        reader.Seek(payloadStart + payloadSize);
    }
}

bool RenderWareReader::ParseMaterialList(
    BinaryReader& reader,
    const ChunkHeader& materialList,
    Geometry& geometry)
{
    reader.Seek(materialList.dataOffset);

    ChunkHeader listStruct{};
    if (!FindChildChunk(reader, materialList.endOffset, ChunkStruct, listStruct))
    {
        return false;
    }

    reader.Seek(listStruct.dataOffset);
    const std::uint32_t materialCount = reader.ReadU32();

    std::vector<std::int32_t> materialReferences;
    materialReferences.reserve(materialCount);
    for (std::uint32_t index = 0; index < materialCount; ++index)
    {
        materialReferences.push_back(reader.ReadI32());
    }

    reader.Seek(listStruct.endOffset);

    std::vector<MaterialInfo> serializedMaterials;
    while (reader.Position() + 12 <= materialList.endOffset)
    {
        ChunkHeader child{};
        if (!ReadChunkHeader(reader, child))
        {
            break;
        }

        if (child.type == ChunkMaterial)
        {
            MaterialInfo material{};
            ParseMaterial(reader, child, material);
            serializedMaterials.push_back(std::move(material));
        }

        reader.Seek(child.endOffset);
    }

    geometry.materials.clear();
    geometry.materials.reserve(materialCount);

    std::size_t serializedIndex = 0;
    for (std::size_t slot = 0; slot < materialReferences.size(); ++slot)
    {
        const std::int32_t reference = materialReferences[slot];
        if (reference < 0)
        {
            if (serializedIndex >= serializedMaterials.size())
            {
                return false;
            }

            geometry.materials.push_back(
                std::move(serializedMaterials[serializedIndex++]));
        }
        else if (static_cast<std::size_t>(reference) < geometry.materials.size())
        {
            geometry.materials.push_back(
                geometry.materials[static_cast<std::size_t>(reference)]);
        }
        else
        {
            return false;
        }
    }

    return geometry.materials.size() == materialCount;
}

bool RenderWareReader::ParseMaterial(
    BinaryReader& reader,
    const ChunkHeader& materialChunk,
    MaterialInfo& material)
{
    reader.Seek(materialChunk.dataOffset);

    ChunkHeader materialStruct{};
    if (!FindChildChunk(reader, materialChunk.endOffset, ChunkStruct, materialStruct))
    {
        return false;
    }

    reader.Seek(materialStruct.dataOffset);

    reader.ReadU32();
    material.color.r = reader.ReadU8();
    material.color.g = reader.ReadU8();
    material.color.b = reader.ReadU8();
    material.color.a = reader.ReadU8();
    reader.ReadU32();

    const std::uint32_t textured = reader.ReadU32();

    if (materialStruct.endOffset - reader.Position() >= 12)
    {
        reader.ReadF32();
        reader.ReadF32();
        reader.ReadF32();
    }

    reader.Seek(materialStruct.endOffset);

    if (textured == 0)
    {
        return true;
    }

    ChunkHeader textureChunk{};
    if (!FindChildChunk(reader, materialChunk.endOffset, ChunkTexture, textureChunk))
    {
        return true;
    }

    reader.Seek(textureChunk.dataOffset);

    ChunkHeader textureStruct{};
    if (FindChildChunk(reader, textureChunk.endOffset, ChunkStruct, textureStruct))
    {
        reader.Seek(textureStruct.endOffset);
    }

    ChunkHeader nameChunk{};
    if (FindChildChunk(reader, textureChunk.endOffset, ChunkString, nameChunk))
    {
        reader.Seek(nameChunk.dataOffset);
        material.textureName = reader.ReadFixedString(nameChunk.length);
        reader.Seek(nameChunk.endOffset);
    }

    ChunkHeader maskChunk{};
    if (FindChildChunk(reader, textureChunk.endOffset, ChunkString, maskChunk))
    {
        reader.Seek(maskChunk.dataOffset);
        material.maskName = reader.ReadFixedString(maskChunk.length);
    }

    return true;
}

bool RenderWareReader::ParseAtomic(
    BinaryReader& reader,
    const ChunkHeader& atomicChunk,
    ModelData& model)
{
    reader.Seek(atomicChunk.dataOffset);

    ChunkHeader atomicStruct{};
    if (!FindChildChunk(reader, atomicChunk.endOffset, ChunkStruct, atomicStruct))
    {
        return false;
    }

    reader.Seek(atomicStruct.dataOffset);

    Atomic atomic{};
    atomic.frameIndex = reader.ReadI32();
    atomic.geometryIndex = reader.ReadI32();
    atomic.flags = reader.ReadU32();
    reader.ReadU32();

    model.atomics.push_back(atomic);
    return true;
}

void RenderWareReader::BuildWorldTransforms(ModelData& model)
{
    for (std::size_t index = 0; index < model.frames.size(); ++index)
    {
        Frame& frame = model.frames[index];

        if (frame.parentIndex >= 0 &&
            static_cast<std::size_t>(frame.parentIndex) < model.frames.size() &&
            static_cast<std::size_t>(frame.parentIndex) != index)
        {
            frame.worldTransform = Multiply(
                model.frames[static_cast<std::size_t>(frame.parentIndex)].worldTransform,
                frame.localTransform);
        }
        else
        {
            frame.worldTransform = frame.localTransform;
        }
    }
}

void RenderWareReader::BuildBounds(ModelData& model)
{
    model.bounds = {};

    if (!model.atomics.empty())
    {
        for (const Atomic& atomic : model.atomics)
        {
            if (atomic.geometryIndex < 0 ||
                static_cast<std::size_t>(atomic.geometryIndex) >= model.geometries.size())
            {
                continue;
            }

            Mat4 transform{};
            if (atomic.frameIndex >= 0 &&
                static_cast<std::size_t>(atomic.frameIndex) < model.frames.size())
            {
                transform = model.frames[static_cast<std::size_t>(atomic.frameIndex)].worldTransform;
            }

            const Geometry& geometry = model.geometries[static_cast<std::size_t>(atomic.geometryIndex)];
            for (const Vec3& vertex : geometry.vertices)
            {
                model.bounds.Expand(TransformPoint(transform, vertex));
            }
        }
    }
    else
    {
        for (const Geometry& geometry : model.geometries)
        {
            for (const Vec3& vertex : geometry.vertices)
            {
                model.bounds.Expand(vertex);
            }
        }
    }
}
