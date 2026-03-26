#include "tools.h"
#include "../tool_registry.h"
#include "../renderdoc_wrapper.h"
#include "renderdoc_replay.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

static ResourceId parseResourceId(const std::string& str)
{
    uint64_t raw = 0;
    size_t pos = str.find("::");
    if(pos != std::string::npos)
        raw = std::stoull(str.substr(pos + 2));
    else
        raw = std::stoull(str);
    ResourceId id;
    memcpy(&id, &raw, sizeof(raw));
    return id;
}

void registerExportTools(ToolRegistry& registry)
{
    // export_render_target
    registry.registerTool({
        "export_render_target",
        "Export the current event's render target as a PNG file. The file is saved to an auto-generated path in the capture's directory. Call goto_event first to select which event to export.",
        {
            {"type", "object"},
            {"properties", {
                {"index", {{"type", "integer"}, {"description", "Render target index (0-7), defaults to 0"}, {"default", 0}}}
            }}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            int index = args.value("index", 0);
            return w.exportRenderTarget(index);
        }
    });

    // export_texture
    registry.registerTool({
        "export_texture",
        "Export a texture resource as PNG image by resource ID",
        {
            {"type", "object"},
            {"properties", {
                {"resourceId", {{"type", "string"}, {"description", "Resource ID (e.g. ResourceId::123)"}}},
                {"mip", {{"type", "integer"}, {"description", "Mip level, default 0"}}},
                {"layer", {{"type", "integer"}, {"description", "Array layer, default 0"}}}
            }},
            {"required", json::array({"resourceId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string idStr = args["resourceId"].get<std::string>();
            ResourceId texId = parseResourceId(idStr);

            int mip = args.value("mip", 0);
            int layer = args.value("layer", 0);

            TextureSave save = {};
            save.resourceId = texId;
            save.mip = mip;
            save.slice.sliceIndex = layer;
            save.destType = FileType::PNG;
            save.alpha = AlphaMapping::Preserve;

            std::string dir = w.getExportDir();
            fs::create_directories(dir);
            std::string path = (fs::path(dir) / ("tex_" + idStr + "_mip" + std::to_string(mip) + ".png")).string();

            // Sanitize path: replace :: with _ for filesystem safety
            for(size_t i = 0; i < path.size(); i++)
            {
                if(path[i] == ':' && i + 1 < path.size() && path[i + 1] == ':')
                {
                    path[i] = '_';
                    path[i + 1] = '_';
                }
            }

            ResultDetails result = ctrl->SaveTexture(save, rdcstr(path.c_str()));
            if(result.code != ResultCode::Succeeded)
                throw std::runtime_error("Failed to save texture: " + std::string(result.Message().c_str()));

            json out;
            out["path"] = path;
            out["resourceId"] = idStr;
            out["mip"] = mip;
            out["layer"] = layer;
            return out;
        }
    });

    // export_buffer
    registry.registerTool({
        "export_buffer",
        "Export buffer data to a binary file",
        {
            {"type", "object"},
            {"properties", {
                {"resourceId", {{"type", "string"}, {"description", "Resource ID"}}},
                {"offset", {{"type", "integer"}, {"description", "Byte offset, default 0"}}},
                {"size", {{"type", "integer"}, {"description", "Byte count (0 = all), default 0"}}}
            }},
            {"required", json::array({"resourceId"})}
        },
        [](RenderdocWrapper& w, const json& args) -> json {
            auto* ctrl = w.getController();
            if(!ctrl)
                throw std::runtime_error("No capture is open. Call open_capture first.");

            std::string idStr = args["resourceId"].get<std::string>();
            ResourceId bufId = parseResourceId(idStr);
            uint64_t offset = args.value("offset", (uint64_t)0);
            uint64_t size = args.value("size", (uint64_t)0);

            rdcarray<byte> data = ctrl->GetBufferData(bufId, offset, size);

            std::string dir = w.getExportDir();
            fs::create_directories(dir);

            // Build a safe filename from the resource ID
            std::string safeName = idStr;
            for(auto& c : safeName)
            {
                if(c == ':')
                    c = '_';
            }
            std::string path = (fs::path(dir) / ("buf_" + safeName + ".bin")).string();

            std::ofstream ofs(path, std::ios::binary);
            if(!ofs)
                throw std::runtime_error("Failed to open output file: " + path);
            ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
            ofs.close();

            json out;
            out["path"] = path;
            out["resourceId"] = idStr;
            out["byteSize"] = data.size();
            out["offset"] = offset;
            out["requestedSize"] = size;
            return out;
        }
    });
}
