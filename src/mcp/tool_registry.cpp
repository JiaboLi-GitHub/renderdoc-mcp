#include "mcp/tool_registry.h"
#include "core/session.h"

using json = nlohmann::json;

namespace renderdoc::mcp {

void ToolRegistry::registerTool(ToolDef def)
{
    m_toolIndex[def.name] = m_tools.size();
    m_tools.push_back(std::move(def));
}

bool ToolRegistry::hasTool(const std::string& name) const
{
    return m_toolIndex.find(name) != m_toolIndex.end();
}

json ToolRegistry::getToolDefinitions() const
{
    auto tools = json::array();
    for(const auto& t : m_tools)
    {
        tools.push_back({
            {"name", t.name},
            {"description", t.description},
            {"inputSchema", t.inputSchema}
        });
    }
    return tools;
}

json ToolRegistry::callTool(const std::string& name,
                            core::Session& session,
                            const json& args)
{
    auto it = m_toolIndex.find(name);
    if(it == m_toolIndex.end())
        throw InvalidParamsError("Unknown tool: " + name);

    const auto& tool = m_tools[it->second];
    validateArgs(tool, args);
    return tool.handler(session, args);
}

void ToolRegistry::validateArgs(const ToolDef& tool, const json& args) const
{
    const auto& schema = tool.inputSchema;
    if(!schema.contains("properties"))
        return;

    // Root must be object
    if(!args.is_object())
        throw InvalidParamsError("Arguments must be an object");

    // Required fields check
    if(schema.contains("required") && schema["required"].is_array())
    {
        for(const auto& req : schema["required"])
        {
            const auto& fieldName = req.get<std::string>();
            if(!args.contains(fieldName))
                throw InvalidParamsError("Missing required parameter: " + fieldName);
        }
    }

    // Type + enum checks for present fields
    const auto& props = schema["properties"];
    for(auto it = args.begin(); it != args.end(); ++it)
    {
        if(!props.contains(it.key()))
            continue;
        const auto& propSchema = props[it.key()];
        const auto& val = it.value();

        // Type check
        if(propSchema.contains("type"))
        {
            const auto& expectedType = propSchema["type"].get<std::string>();
            bool ok = false;
            if(expectedType == "string")       ok = val.is_string();
            else if(expectedType == "integer") ok = val.is_number_integer();
            else if(expectedType == "number")  ok = val.is_number();
            else if(expectedType == "boolean") ok = val.is_boolean();
            else if(expectedType == "object")  ok = val.is_object();
            else if(expectedType == "array")   ok = val.is_array();
            else ok = true;
            if(!ok)
                throw InvalidParamsError("Parameter '" + it.key() + "' must be " + expectedType);
        }

        // Enum check
        if(propSchema.contains("enum") && propSchema["enum"].is_array())
        {
            bool found = false;
            for(const auto& allowed : propSchema["enum"])
            {
                if(val == allowed) { found = true; break; }
            }
            if(!found)
            {
                std::string allowedStr;
                for(const auto& a : propSchema["enum"])
                {
                    if(!allowedStr.empty()) allowedStr += ", ";
                    allowedStr += a.dump();
                }
                throw InvalidParamsError("Parameter '" + it.key() + "' must be one of: " + allowedStr);
            }
        }
    }
}

} // namespace renderdoc::mcp
