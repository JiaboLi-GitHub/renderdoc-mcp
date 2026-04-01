#include "mcp/serialization.h"
#include <stdexcept>
#include <sstream>

namespace renderdoc::mcp {

std::string resourceIdToString(core::ResourceId id) {
    return "ResourceId::" + std::to_string(id);
}

core::ResourceId parseResourceId(const std::string& str) {
    const std::string prefix = "ResourceId::";
    if (str.rfind(prefix, 0) != 0)
        throw std::invalid_argument("Invalid ResourceId format: " + str);
    return std::stoull(str.substr(prefix.size()));
}

// actionFlagsToString is in src/mcp/action_flags.cpp (renderdoc-mcp-lib)
// because it needs RenderDoc headers. See below after serialization.cpp.

std::string graphicsApiToString(core::GraphicsApi api) {
    switch (api) {
        case core::GraphicsApi::D3D11: return "D3D11";
        case core::GraphicsApi::D3D12: return "D3D12";
        case core::GraphicsApi::OpenGL: return "OpenGL";
        case core::GraphicsApi::Vulkan: return "Vulkan";
        default: return "Unknown";
    }
}

std::string shaderStageToString(core::ShaderStage stage) {
    switch (stage) {
        case core::ShaderStage::Vertex: return "vs";
        case core::ShaderStage::Hull: return "hs";
        case core::ShaderStage::Domain: return "ds";
        case core::ShaderStage::Geometry: return "gs";
        case core::ShaderStage::Pixel: return "ps";
        case core::ShaderStage::Compute: return "cs";
    }
    return "unknown";
}

core::ShaderStage parseShaderStage(const std::string& str) {
    if (str == "vs") return core::ShaderStage::Vertex;
    if (str == "hs") return core::ShaderStage::Hull;
    if (str == "ds") return core::ShaderStage::Domain;
    if (str == "gs") return core::ShaderStage::Geometry;
    if (str == "ps") return core::ShaderStage::Pixel;
    if (str == "cs") return core::ShaderStage::Compute;
    throw std::invalid_argument("Invalid shader stage: " + str);
}

nlohmann::json to_json(const core::CaptureInfo& info) {
    nlohmann::json j;
    j["path"] = info.path;
    j["api"] = graphicsApiToString(info.api);
    j["degraded"] = info.degraded;
    j["totalEvents"] = info.totalEvents;
    j["totalDraws"] = info.totalDraws;
    j["machineIdent"] = info.machineIdent;
    j["driverName"] = info.driverName;
    j["hasCallstacks"] = info.hasCallstacks;
    j["timestampBase"] = info.timestampBase;
    auto gpus = nlohmann::json::array();
    for (const auto& g : info.gpus) {
        gpus.push_back({{"name", g.name}, {"vendor", g.vendor},
                        {"deviceID", g.deviceID}, {"driver", g.driver}});
    }
    j["gpus"] = gpus;
    return j;
}

nlohmann::json to_json(const core::SessionStatus& s) {
    return {{"isOpen", s.isOpen}, {"capturePath", s.capturePath},
            {"api", graphicsApiToString(s.api)},
            {"currentEventId", s.currentEventId}, {"totalEvents", s.totalEvents}};
}

nlohmann::json to_json(const core::EventInfo& e) {
    nlohmann::json j;
    j["eventId"] = e.eventId;
    j["name"] = e.name;
    j["flags"] = actionFlagsToString(e.flags);
    j["numIndices"] = e.numIndices;
    j["numInstances"] = e.numInstances;
    j["drawIndex"] = e.drawIndex;
    if (!e.outputs.empty()) {
        auto arr = nlohmann::json::array();
        for (auto id : e.outputs) arr.push_back(resourceIdToString(id));
        j["outputs"] = arr;
    }
    return j;
}

nlohmann::json to_json(const core::RenderTargetInfo& rt) {
    return {{"resourceId", resourceIdToString(rt.id)}, {"name", rt.name},
            {"width", rt.width}, {"height", rt.height}, {"format", rt.format}};
}

nlohmann::json to_json(const core::PipelineState& state) {
    nlohmann::json j;
    j["api"] = graphicsApiToString(state.api);
    for (const auto& s : state.shaders) {
        std::string key = shaderStageToString(s.stage) == "ps" ? "pixelShader" :
                          shaderStageToString(s.stage) == "vs" ? "vertexShader" :
                          shaderStageToString(s.stage) + "Shader";
        j[key] = {{"resourceId", resourceIdToString(s.shaderId)}, {"entryPoint", s.entryPoint}};
    }
    j["renderTargets"] = to_json_array(state.renderTargets);
    if (state.depthTarget) j["depthTarget"] = to_json(*state.depthTarget);
    auto vps = nlohmann::json::array();
    for (const auto& vp : state.viewports) {
        vps.push_back({{"x", vp.x}, {"y", vp.y}, {"width", vp.width}, {"height", vp.height},
                       {"minDepth", vp.minDepth}, {"maxDepth", vp.maxDepth}});
    }
    j["viewports"] = vps;
    return j;
}

nlohmann::json to_json(const core::StageBindings& b) {
    nlohmann::json j;
    j["shader"] = resourceIdToString(b.shaderId);
    auto serializeBindings = [](const std::vector<core::ShaderBindingDetail>& vec) {
        auto arr = nlohmann::json::array();
        for (const auto& bd : vec) {
            nlohmann::json item = {{"name", bd.name}, {"bindPoint", bd.bindPoint}};
            if (bd.byteSize > 0) item["byteSize"] = bd.byteSize;
            if (bd.variableCount > 0) item["variables"] = bd.variableCount;
            arr.push_back(item);
        }
        return arr;
    };
    if (!b.constantBuffers.empty()) j["constantBuffers"] = serializeBindings(b.constantBuffers);
    if (!b.readOnlyResources.empty()) j["readOnlyResources"] = serializeBindings(b.readOnlyResources);
    if (!b.readWriteResources.empty()) j["readWriteResources"] = serializeBindings(b.readWriteResources);
    if (!b.samplers.empty()) j["samplers"] = serializeBindings(b.samplers);
    return j;
}

nlohmann::json to_json(const core::ResourceInfo& r) {
    nlohmann::json j;
    j["resourceId"] = resourceIdToString(r.id);
    j["name"] = r.name;
    j["type"] = r.type;
    if (r.width) j["width"] = *r.width;
    if (r.height) j["height"] = *r.height;
    if (r.depth) j["depth"] = *r.depth;
    if (r.format) j["format"] = *r.format;
    if (r.mips) j["mips"] = *r.mips;
    if (r.arraySize) j["arraysize"] = *r.arraySize;
    if (r.dimension) j["dimension"] = *r.dimension;
    if (r.cubemap) j["cubemap"] = *r.cubemap;
    if (r.msSamp) j["msSamp"] = *r.msSamp;
    if (r.formatDetails) {
        j["formatDetails"] = {{"name", r.formatDetails->name},
                              {"compCount", r.formatDetails->compCount},
                              {"compByteWidth", r.formatDetails->compByteWidth},
                              {"compType", r.formatDetails->compType}};
    }
    j["byteSize"] = r.byteSize;
    if (r.gpuAddress) j["gpuAddress"] = *r.gpuAddress;
    return j;
}

nlohmann::json to_json(const core::PassInfo& p) {
    nlohmann::json j;
    j["name"] = p.name;
    j["eventId"] = p.eventId;
    j["drawCount"] = p.drawCount;
    j["dispatchCount"] = p.dispatchCount;
    if (!p.draws.empty()) j["draws"] = to_json_array(p.draws);
    return j;
}

nlohmann::json to_json(const core::DebugMessage& m) {
    return {{"eventId", m.eventId}, {"severity", m.severity},
            {"category", m.category}, {"message", m.message}};
}

nlohmann::json to_json(const core::CaptureStats& s) {
    auto pp = nlohmann::json::array();
    for (const auto& p : s.perPass)
        pp.push_back({{"name", p.name}, {"drawCount", p.drawCount},
                      {"dispatchCount", p.dispatchCount}, {"totalTriangles", p.totalTriangles}});
    auto td = nlohmann::json::array();
    for (const auto& d : s.topDraws)
        td.push_back({{"eventId", d.eventId}, {"name", d.name}, {"numIndices", d.numIndices}});
    auto lr = nlohmann::json::array();
    for (const auto& r : s.largestResources)
        lr.push_back({{"name", r.name}, {"byteSize", r.byteSize}, {"type", r.type},
                      {"width", r.width}, {"height", r.height}});
    return {{"perPass", pp}, {"topDraws", td}, {"largestResources", lr}};
}

nlohmann::json to_json(const core::ShaderReflection& r) {
    nlohmann::json j;
    j["resourceId"] = resourceIdToString(r.id);
    j["stage"] = shaderStageToString(r.stage);
    j["entryPoint"] = r.entryPoint;
    auto serializeSig = [](const std::vector<core::SignatureElement>& sig) {
        auto arr = nlohmann::json::array();
        for (const auto& s : sig)
            arr.push_back({{"varName", s.varName}, {"semanticName", s.semanticName},
                           {"semanticIndex", s.semanticIndex}, {"regIndex", s.regIndex}});
        return arr;
    };
    j["inputSignature"] = serializeSig(r.inputSignature);
    j["outputSignature"] = serializeSig(r.outputSignature);
    auto cbs = nlohmann::json::array();
    for (const auto& cb : r.constantBlocks)
        cbs.push_back({{"name", cb.name}, {"bindPoint", cb.bindPoint},
                       {"byteSize", cb.byteSize}, {"variableCount", cb.variableCount}});
    j["constantBlocks"] = cbs;
    return j;
}

nlohmann::json to_json(const core::ShaderDisassembly& d) {
    return {{"resourceId", resourceIdToString(d.id)}, {"stage", shaderStageToString(d.stage)},
            {"disassembly", d.disassembly}, {"target", d.target}};
}

nlohmann::json to_json(const core::ShaderUsageInfo& u) {
    return {{"shaderId", resourceIdToString(u.shaderId)}, {"stage", shaderStageToString(u.stage)},
            {"entryPoint", u.entryPoint}, {"usageCount", u.usageCount}};
}

nlohmann::json to_json(const core::ShaderSearchMatch& m) {
    auto lines = nlohmann::json::array();
    for (const auto& ml : m.matchingLines)
        lines.push_back({{"line", ml.line}, {"text", ml.text}});
    return {{"shaderId", resourceIdToString(m.shaderId)}, {"stage", shaderStageToString(m.stage)},
            {"entryPoint", m.entryPoint}, {"matchingLines", lines}};
}

nlohmann::json to_json(const core::ExportResult& e) {
    nlohmann::json j;
    j["path"] = e.outputPath;
    j["byteSize"] = e.byteSize;
    if (e.rtIndex >= 0) {
        j["eventId"] = e.eventId;
        j["rtIndex"] = e.rtIndex;
        j["width"] = e.width;
        j["height"] = e.height;
    }
    if (e.resourceId != 0) j["resourceId"] = resourceIdToString(e.resourceId);
    if (e.mip > 0) j["mip"] = e.mip;
    if (e.layer > 0) j["layer"] = e.layer;
    if (e.offset > 0) j["offset"] = e.offset;
    if (e.requestedSize > 0) j["requestedSize"] = e.requestedSize;
    return j;
}

nlohmann::json to_json(const core::BoundResource& b) {
    return {{"resourceId", resourceIdToString(b.id)}, {"name", b.name},
            {"typeName", b.typeName}, {"bindPoint", b.bindPoint}};
}

nlohmann::json to_json(const core::CaptureResult& r) {
    return {{"capturePath", r.capturePath}, {"pid", r.pid}};
}

// --- Phase 1: Pixel, Debug, TexStats serialization ---

nlohmann::json to_json(const core::PixelValue& val) {
    return {
        {"floatValue", {val.floatValue[0], val.floatValue[1], val.floatValue[2], val.floatValue[3]}},
        {"uintValue",  {val.uintValue[0],  val.uintValue[1],  val.uintValue[2],  val.uintValue[3]}},
        {"intValue",   {val.intValue[0],   val.intValue[1],   val.intValue[2],   val.intValue[3]}}
    };
}

nlohmann::json to_json(const core::PixelModification& mod) {
    nlohmann::json j;
    j["eventId"]       = mod.eventId;
    j["fragmentIndex"] = mod.fragmentIndex;
    j["primitiveId"]   = mod.primitiveId;
    j["shaderOut"]     = to_json(mod.shaderOut);
    j["postMod"]       = to_json(mod.postMod);
    if (mod.depth.has_value())
        j["depth"] = *mod.depth;
    else
        j["depth"] = nullptr;
    j["passed"] = mod.passed;
    j["flags"]  = mod.flags;
    return j;
}

nlohmann::json to_json(const core::PixelHistoryResult& result) {
    nlohmann::json j;
    j["x"]           = result.x;
    j["y"]           = result.y;
    j["eventId"]     = result.eventId;
    j["targetIndex"] = result.targetIndex;
    j["targetId"]    = resourceIdToString(result.targetId);
    j["modifications"] = to_json_array(result.modifications);
    return j;
}

nlohmann::json to_json(const core::PickPixelResult& result) {
    return {
        {"x",           result.x},
        {"y",           result.y},
        {"eventId",     result.eventId},
        {"targetIndex", result.targetIndex},
        {"targetId",    resourceIdToString(result.targetId)},
        {"color",       to_json(result.color)}
    };
}

nlohmann::json to_json(const core::DebugVariable& var) {
    nlohmann::json j;
    j["name"]  = var.name;
    j["type"]  = var.type;
    j["rows"]  = var.rows;
    j["cols"]  = var.cols;
    j["flags"] = var.flags;

    if (!var.floatValues.empty()) j["floatValues"] = var.floatValues;
    else                         j["floatValues"] = nlohmann::json::array();

    if (!var.uintValues.empty())  j["uintValues"] = var.uintValues;
    else                          j["uintValues"] = nlohmann::json::array();

    if (!var.intValues.empty())   j["intValues"] = var.intValues;
    else                          j["intValues"] = nlohmann::json::array();

    if (!var.members.empty())     j["members"] = to_json_array(var.members);
    else                          j["members"] = nlohmann::json::array();

    return j;
}

nlohmann::json to_json(const core::DebugVariableChange& change) {
    return {
        {"before", to_json(change.before)},
        {"after",  to_json(change.after)}
    };
}

nlohmann::json to_json(const core::DebugStep& step) {
    nlohmann::json j;
    j["step"]        = step.step;
    j["instruction"] = step.instruction;
    j["file"]        = step.file;
    j["line"]        = step.line;
    j["changes"]     = to_json_array(step.changes);
    return j;
}

nlohmann::json to_json(const core::ShaderDebugResult& result) {
    nlohmann::json j;
    j["eventId"]    = result.eventId;
    j["stage"]      = result.stage;
    j["totalSteps"] = result.totalSteps;
    j["inputs"]     = to_json_array(result.inputs);
    j["outputs"]    = to_json_array(result.outputs);
    if (!result.trace.empty())
        j["trace"] = to_json_array(result.trace);
    return j;
}

nlohmann::json to_json(const core::TextureStats& stats) {
    nlohmann::json j;
    j["id"]      = resourceIdToString(stats.id);
    j["eventId"] = stats.eventId;
    j["mip"]     = stats.mip;
    j["slice"]   = stats.slice;
    j["min"]     = to_json(stats.minVal);
    j["max"]     = to_json(stats.maxVal);
    if (!stats.histogram.empty()) {
        auto arr = nlohmann::json::array();
        for (const auto& b : stats.histogram) {
            arr.push_back({{"r", b.r}, {"g", b.g}, {"b", b.b}, {"a", b.a}});
        }
        j["histogram"] = arr;
    }
    return j;
}

// --- Phase 2: Shader edit, mesh, snapshot, usage, assertion serialization ---

nlohmann::json to_json(const core::ShaderBuildResult& result) {
    return {{"shaderId", result.shaderId}, {"warnings", result.warnings}};
}

nlohmann::json to_json(const core::MeshVertex& v) {
    return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

nlohmann::json to_json(const core::MeshData& data) {
    nlohmann::json j;
    j["eventId"] = data.eventId;
    j["stage"] = (data.stage == core::MeshStage::VSOut) ? "vs-out" : "gs-out";
    switch (data.topology) {
    case core::MeshTopology::TriangleList:  j["topology"] = "TriangleList"; break;
    case core::MeshTopology::TriangleStrip: j["topology"] = "TriangleStrip"; break;
    case core::MeshTopology::TriangleFan:   j["topology"] = "TriangleFan"; break;
    default:                                j["topology"] = "Other"; break;
    }
    j["vertexCount"] = data.vertices.size();
    j["faceCount"] = data.faces.size();
    j["vertices"] = to_json_array(data.vertices);
    auto indicesArr = nlohmann::json::array();
    for (auto idx : data.indices) indicesArr.push_back(idx);
    j["indices"] = indicesArr;
    auto facesArr = nlohmann::json::array();
    for (const auto& f : data.faces) facesArr.push_back({f[0], f[1], f[2]});
    j["faces"] = facesArr;
    return j;
}

nlohmann::json to_json(const core::SnapshotResult& result) {
    return {{"manifestPath", result.manifestPath}, {"files", result.files}, {"errors", result.errors}};
}

nlohmann::json to_json(const core::ResourceUsageEntry& entry) {
    return {{"eventId", entry.eventId}, {"usage", entry.usage}};
}

nlohmann::json to_json(const core::ResourceUsageResult& result) {
    return {{"resourceId", resourceIdToString(result.resourceId)},
            {"entries", to_json_array(result.entries)}};
}

nlohmann::json to_json(const core::AssertResult& result) {
    nlohmann::json j;
    j["pass"] = result.pass;
    j["message"] = result.message;
    for (const auto& [key, val] : result.details) j[key] = val;
    return j;
}

nlohmann::json to_json(const core::PixelAssertResult& result) {
    nlohmann::json j;
    j["pass"] = result.pass;
    j["message"] = result.message;
    j["actual"] = {result.actual[0], result.actual[1], result.actual[2], result.actual[3]};
    j["expected"] = {result.expected[0], result.expected[1], result.expected[2], result.expected[3]};
    j["tolerance"] = result.tolerance;
    return j;
}

nlohmann::json to_json(const core::CleanAssertResult& result) {
    nlohmann::json j = to_json(result.result);
    if (!result.messages.empty()) {
        auto arr = nlohmann::json::array();
        for (const auto& msg : result.messages) arr.push_back(to_json(msg));
        j["messages"] = arr;
    }
    return j;
}

nlohmann::json to_json(const core::ImageCompareResult& result) {
    nlohmann::json j;
    j["pass"] = result.pass;
    j["diffPixels"] = result.diffPixels;
    j["totalPixels"] = result.totalPixels;
    j["diffRatio"] = result.diffRatio;
    j["message"] = result.message;
    if (!result.diffOutputPath.empty()) j["diffOutputPath"] = result.diffOutputPath;
    return j;
}

} // namespace renderdoc::mcp
