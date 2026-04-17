#pragma once

#include <string>
#include <string_view>
#include <utility>
#include "json_common.hpp"
#include "../func_registry/registry_introspection.hpp"

namespace json_invoke {

inline json toolParameterSpecToJson(const func_registry::ToolParameterSpec& spec)
{
    return json{
        {"name", spec.name},
        {"cpp_type_name", spec.cpp_type_name},
        {"llm_type", spec.llm_type},
        {"required", spec.required},
    };
}

inline json toolSummaryToJson(const func_registry::ToolSpec& spec)
{
    return json{
        {"name", spec.tool_name},
        {"description", spec.description},
    };
}

inline json toolSpecToJson(const func_registry::ToolSpec& spec)
{
    json parameters = json::array();
    for (const auto& parameter : spec.parameters)
    {
        parameters.push_back(toolParameterSpecToJson(parameter));
    }

    return json{
        {"tool_name", spec.tool_name},
        {"function_name", spec.function_name},
        {"prototype", spec.prototype},
        {"description", spec.description},
        {"parameters", std::move(parameters)},
        {"return_cpp_type_name", spec.return_cpp_type_name},
        {"return_llm_type", spec.return_llm_type},
    };
}

inline std::string jsonSchemaType(std::string_view llm_type)
{
    if (llm_type == "string" || llm_type == "integer" || llm_type == "number" ||
        llm_type == "boolean" || llm_type == "array" || llm_type == "object")
    {
        return std::string(llm_type);
    }

    return "string";
}

inline json toolSchemaToJson(const func_registry::ToolSpec& spec)
{
    json properties = json::object();
    json required = json::array();

    for (const auto& parameter : spec.parameters)
    {
        properties[parameter.name] = {
            {"type", jsonSchemaType(parameter.llm_type)},
            {"description", "C++ type: " + parameter.cpp_type_name},
            {"x-cpp-type", parameter.cpp_type_name},
            {"x-llm-type", parameter.llm_type},
        };

        if (parameter.required)
        {
            required.push_back(parameter.name);
        }
    }

    return json{
        {"type", "function"},
        {"function", {
            {"name", spec.tool_name},
            {"description", spec.description.empty() ? spec.prototype : spec.description},
            {"parameters", {
                {"type", "object"},
                {"properties", std::move(properties)},
                {"required", std::move(required)},
                {"additionalProperties", false},
            }},
            {"strict", true},
            {"x-prototype", spec.prototype},
            {"x-return", {
                {"cpp_type_name", spec.return_cpp_type_name},
                {"llm_type", spec.return_llm_type},
            }},
        }},
    };
}

template<typename Registry>
json getToolSpecJson(const Registry& registry, const std::string& name)
{
    try
    {
        return toolSpecToJson(func_registry::getToolSpec(registry, name));
    }
    catch (const std::exception& e)
    {
        throw JsonInvokeError("function_not_found", e.what());
    }
}

template<typename Registry>
json getAllToolSpecsJson(const Registry& registry)
{
    json tools = json::array();
    for (const auto& spec : func_registry::getAllToolSpecs(registry))
    {
        tools.push_back(toolSpecToJson(spec));
    }
    return tools;
}

template<typename Registry>
json getAllToolSummariesJson(const Registry& registry)
{
    json tools = json::array();
    for (const auto& spec : func_registry::getAllToolSpecs(registry))
    {
        tools.push_back(toolSummaryToJson(spec));
    }
    return tools;
}

template<typename Registry>
json getToolSchemaJson(const Registry& registry, const std::string& name)
{
    try
    {
        return toolSchemaToJson(func_registry::getToolSpec(registry, name));
    }
    catch (const std::exception& e)
    {
        throw JsonInvokeError("function_not_found", e.what());
    }
}

template<typename Registry>
json getAllToolSchemasJson(const Registry& registry)
{
    json tools = json::array();
    for (const auto& spec : func_registry::getAllToolSpecs(registry))
    {
        tools.push_back(toolSchemaToJson(spec));
    }
    return tools;
}

} // namespace json_invoke