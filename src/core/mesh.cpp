#include "core/mesh.h"
#include "core/errors.h"
#include "core/session.h"

#include <renderdoc_replay.h>

#include <cmath>
#include <cstring>
#include <sstream>

namespace renderdoc::core {

namespace {

// Map our MeshStage enum to RenderDoc's MeshDataStage enum.
::MeshDataStage toRdcMeshStage(MeshStage stage) {
    switch (stage) {
        case MeshStage::VSOut: return ::MeshDataStage::VSOut;
        case MeshStage::GSOut: return ::MeshDataStage::GSOut;
    }
    return ::MeshDataStage::VSOut;
}

// Map RenderDoc Topology to our MeshTopology.
MeshTopology fromRdcTopology(::Topology topo) {
    switch (topo) {
        case ::Topology::TriangleList:  return MeshTopology::TriangleList;
        case ::Topology::TriangleStrip: return MeshTopology::TriangleStrip;
        case ::Topology::TriangleFan:   return MeshTopology::TriangleFan;
        default:                        return MeshTopology::Other;
    }
}

// Decode a single float component from raw bytes based on format.
float decodeComponent(const byte* data, uint32_t compByteWidth, ::CompType compType) {
    if (compByteWidth == 4 && compType == ::CompType::Float) {
        float v;
        std::memcpy(&v, data, 4);
        return v;
    }
    if (compByteWidth == 2 && compType == ::CompType::Float) {
        // IEEE 754 half-float decode
        uint16_t h;
        std::memcpy(&h, data, 2);
        uint32_t sign = (h >> 15) & 0x1;
        uint32_t exp  = (h >> 10) & 0x1f;
        uint32_t frac = h & 0x3ff;
        float result;
        if (exp == 0) {
            result = std::ldexp(static_cast<float>(frac), -24);
        } else if (exp == 31) {
            result = frac ? std::numeric_limits<float>::quiet_NaN()
                          : std::numeric_limits<float>::infinity();
        } else {
            result = std::ldexp(static_cast<float>(frac + 1024), static_cast<int>(exp) - 25);
        }
        return sign ? -result : result;
    }
    if (compByteWidth == 1 && (compType == ::CompType::UNorm || compType == ::CompType::UNormSRGB)) {
        return static_cast<float>(*data) / 255.0f;
    }
    if (compByteWidth == 1 && compType == ::CompType::SNorm) {
        return std::max(static_cast<float>(static_cast<int8_t>(*data)) / 127.0f, -1.0f);
    }
    if (compByteWidth == 4 && (compType == ::CompType::UInt || compType == ::CompType::UScaled)) {
        uint32_t v;
        std::memcpy(&v, data, 4);
        return static_cast<float>(v);
    }
    if (compByteWidth == 4 && (compType == ::CompType::SInt || compType == ::CompType::SScaled)) {
        int32_t v;
        std::memcpy(&v, data, 4);
        return static_cast<float>(v);
    }
    if (compByteWidth == 2 && (compType == ::CompType::UNorm || compType == ::CompType::UNormSRGB)) {
        uint16_t v;
        std::memcpy(&v, data, 2);
        return static_cast<float>(v) / 65535.0f;
    }
    if (compByteWidth == 2 && compType == ::CompType::SNorm) {
        int16_t v;
        std::memcpy(&v, data, 2);
        return std::max(static_cast<float>(v) / 32767.0f, -1.0f);
    }
    if (compByteWidth == 2 && (compType == ::CompType::UInt || compType == ::CompType::UScaled)) {
        uint16_t v;
        std::memcpy(&v, data, 2);
        return static_cast<float>(v);
    }
    if (compByteWidth == 2 && (compType == ::CompType::SInt || compType == ::CompType::SScaled)) {
        int16_t v;
        std::memcpy(&v, data, 2);
        return static_cast<float>(v);
    }
    // Fallback: treat as float if 4 bytes
    if (compByteWidth == 4) {
        float v;
        std::memcpy(&v, data, 4);
        return v;
    }
    return 0.0f;
}

// Generate triangle faces from index list based on topology.
void generateFaces(const std::vector<uint32_t>& indices, MeshTopology topo,
                   std::vector<std::array<uint32_t, 3>>& faces) {
    if (topo == MeshTopology::TriangleList) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            faces.push_back({indices[i], indices[i + 1], indices[i + 2]});
        }
    } else if (topo == MeshTopology::TriangleStrip) {
        for (size_t i = 0; i + 2 < indices.size(); i++) {
            if (i % 2 == 0)
                faces.push_back({indices[i], indices[i + 1], indices[i + 2]});
            else
                faces.push_back({indices[i + 1], indices[i], indices[i + 2]});
        }
    } else if (topo == MeshTopology::TriangleFan) {
        for (size_t i = 1; i + 1 < indices.size(); i++) {
            faces.push_back({indices[0], indices[i], indices[i + 1]});
        }
    }
}

} // anonymous namespace

MeshData exportMesh(const Session& session, uint32_t eventId, MeshStage stage) {
    auto* ctrl = session.controller();
    if (!ctrl)
        throw CoreError(CoreError::Code::NoCaptureOpen,
                        "No capture is open. Call open_capture first.");

    ctrl->SetFrameEvent(eventId, true);

    ::MeshFormat meshFmt = ctrl->GetPostVSData(0, 0, toRdcMeshStage(stage));

    // Validate that we got valid mesh data.
    if (meshFmt.vertexResourceId == ::ResourceId::Null())
        throw CoreError(CoreError::Code::InternalError,
                        "No post-VS mesh data available at event " +
                        std::to_string(eventId));

    MeshData result;
    result.eventId  = eventId;
    result.stage    = stage;
    result.topology = fromRdcTopology(meshFmt.topology);

    // Fetch the vertex buffer data.
    rdcarray<byte> vtxData = ctrl->GetBufferData(meshFmt.vertexResourceId,
                                                  meshFmt.vertexByteOffset, 0);

    const uint32_t stride = meshFmt.vertexByteStride;
    const uint32_t compByteWidth = meshFmt.format.compByteWidth;
    const ::CompType compType = meshFmt.format.compType;
    const uint32_t numComps = meshFmt.format.compCount;

    if (stride == 0)
        throw CoreError(CoreError::Code::InternalError,
                        "Vertex stride is 0 for mesh at event " +
                        std::to_string(eventId));

    const uint32_t numVerts = static_cast<uint32_t>(vtxData.size()) / stride;

    // Decode vertices — extract xyz from the first 3 components.
    result.vertices.reserve(numVerts);
    for (uint32_t i = 0; i < numVerts; i++) {
        size_t off = static_cast<size_t>(i) * stride;
        if (off + numComps * compByteWidth > static_cast<size_t>(vtxData.size()))
            break;

        MeshVertex v;
        const byte* base = vtxData.data() + off;
        v.x = (numComps >= 1) ? decodeComponent(base + 0 * compByteWidth, compByteWidth, compType) : 0.0f;
        v.y = (numComps >= 2) ? decodeComponent(base + 1 * compByteWidth, compByteWidth, compType) : 0.0f;
        v.z = (numComps >= 3) ? decodeComponent(base + 2 * compByteWidth, compByteWidth, compType) : 0.0f;
        result.vertices.push_back(v);
    }

    // Decode index buffer if present.
    if (meshFmt.indexResourceId != ::ResourceId::Null() && meshFmt.indexByteStride > 0) {
        rdcarray<byte> idxData = ctrl->GetBufferData(meshFmt.indexResourceId,
                                                      meshFmt.indexByteOffset, 0);

        const uint32_t idxStride = meshFmt.indexByteStride;
        const uint32_t numIdx = meshFmt.numIndices;
        result.indices.reserve(numIdx);

        for (uint32_t i = 0; i < numIdx; i++) {
            size_t off = static_cast<size_t>(i) * idxStride;
            if (off + idxStride > static_cast<size_t>(idxData.size()))
                break;

            uint32_t idx = 0;
            if (idxStride == 2) {
                uint16_t v;
                std::memcpy(&v, idxData.data() + off, 2);
                idx = v;
            } else if (idxStride == 4) {
                std::memcpy(&idx, idxData.data() + off, 4);
            } else if (idxStride == 1) {
                idx = idxData[static_cast<int>(off)];
            }
            result.indices.push_back(idx);
        }
    } else {
        // No index buffer — generate sequential indices.
        result.indices.reserve(meshFmt.numIndices);
        for (uint32_t i = 0; i < meshFmt.numIndices && i < numVerts; i++)
            result.indices.push_back(i);
    }

    // Generate faces from topology.
    generateFaces(result.indices, result.topology, result.faces);

    return result;
}

std::string meshToObj(const MeshData& data) {
    std::ostringstream oss;
    oss << "# RenderDoc mesh export - event " << data.eventId << "\n";
    oss << "# Vertices: " << data.vertices.size()
        << "  Faces: " << data.faces.size() << "\n\n";

    for (const auto& v : data.vertices) {
        oss << "v " << v.x << " " << v.y << " " << v.z << "\n";
    }

    oss << "\n";

    // OBJ uses 1-based indices.
    for (const auto& face : data.faces) {
        oss << "f " << (face[0] + 1) << " " << (face[1] + 1)
            << " " << (face[2] + 1) << "\n";
    }

    return oss.str();
}

} // namespace renderdoc::core
