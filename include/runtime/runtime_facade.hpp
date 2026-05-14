#pragma once

#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <json_session_invoke/json_session_invoke.hpp>
#include <task_scheduler/task_scheduler_facade.hpp>

namespace runtime {

using json = json_session_invoke::json;

struct ToolDescriptor {
    std::string name;
    std::string description;
    json input_schema = json::object();
    json output_schema = json::object();
    json raw_schema = json::object();
};

struct InvokeRequest {
    std::string tool_name;
    json arguments = json::object();
};

struct RuntimeError {
    std::string code;
    std::string message;
};

struct InvokeResult {
    std::string tool_name;
    bool ok{false};
    json value = nullptr;
    std::optional<RuntimeError> error;
    json raw_response = json::object();
};

template<bool EnableThreadSafety = false>
class BasicRuntimeFacade {
public:
    using AdapterType = json_session_invoke::BasicJsonSessionInvokeAdapter<EnableThreadSafety>;

    explicit BasicRuntimeFacade(AdapterType& adapter)
        : adapter_(adapter)
        , scheduler_(adapter)
    {
    }

    BasicRuntimeFacade(AdapterType& adapter, std::size_t worker_count)
        : adapter_(adapter)
        , scheduler_(adapter, worker_count)
    {
    }

    std::vector<ToolDescriptor> listTools() const
    {
        std::vector<ToolDescriptor> tools;
        for (const auto& tool_schema : adapter_.getAllToolSchemasJson())
        {
            tools.push_back(describeSchema(tool_schema));
        }
        return tools;
    }

    ToolDescriptor describeTool(const std::string& tool_name) const
    {
        return describeSchema(adapter_.getToolSchemaJson(tool_name));
    }

    InvokeResult invoke(InvokeRequest request)
    {
        return submitInvoke(std::move(request)).get();
    }

    std::future<InvokeResult> submitInvoke(InvokeRequest request)
    {
        const std::string tool_name = request.tool_name;

        try
        {
            auto response_future = scheduler_.submitRequest(makeRequestJson(std::move(request)));
            return std::async(std::launch::deferred, [tool_name, response_future = std::move(response_future)]() mutable {
                try
                {
                    return normalizeResponse(tool_name, response_future.get());
                }
                catch (const json_invoke::JsonInvokeError& e)
                {
                    return makeErrorResult(tool_name, e.code(), e.what());
                }
                catch (const std::exception& e)
                {
                    return makeErrorResult(tool_name, "unknown_error", e.what());
                }
            });
        }
        catch (const json_invoke::JsonInvokeError& e)
        {
            return makeReadyFuture(makeErrorResult(tool_name, e.code(), e.what()));
        }
        catch (const std::exception& e)
        {
            return makeReadyFuture(makeErrorResult(tool_name, "unknown_error", e.what()));
        }
    }

private:
    static ToolDescriptor describeSchema(json tool_schema)
    {
        const auto& function = tool_schema.at("function");

        ToolDescriptor descriptor;
        descriptor.name = function.value("name", "");
        descriptor.description = function.value("description", "");
        descriptor.input_schema = function.value("parameters", json::object());

        const auto return_it = function.find("x-return");
        if (return_it != function.end() && return_it->is_object())
        {
            descriptor.output_schema = return_it->value("schema", json::object());
        }

        descriptor.raw_schema = std::move(tool_schema);
        return descriptor;
    }

    static json makeRequestJson(InvokeRequest request)
    {
        return json{
            {"name", std::move(request.tool_name)},
            {"args", std::move(request.arguments)},
        };
    }

    static InvokeResult normalizeResponse(const std::string& requested_tool_name, json response)
    {
        InvokeResult result;
        result.tool_name = response.value("name", requested_tool_name);
        result.ok = response.value("ok", false);
        result.raw_response = std::move(response);

        if (result.ok)
        {
            const auto value_it = result.raw_response.find("value");
            if (value_it != result.raw_response.end())
            {
                result.value = *value_it;
            }
            return result;
        }

        const auto error_it = result.raw_response.find("error");
        if (error_it != result.raw_response.end() && error_it->is_object())
        {
            RuntimeError error;
            error.code = error_it->value("code", "unknown_error");
            error.message = error_it->value("message", "unknown runtime error");
            result.error = std::move(error);
        }
        else
        {
            result.error = RuntimeError{"unknown_error", "unknown runtime error"};
        }

        return result;
    }

    static InvokeResult makeErrorResult(
        std::string tool_name,
        std::string code,
        std::string message)
    {
        InvokeResult result;
        result.tool_name = std::move(tool_name);
        result.ok = false;
        result.error = RuntimeError{std::move(code), std::move(message)};
        result.raw_response = json{
            {"ok", false},
            {"name", result.tool_name},
            {"error", {
                {"code", result.error->code},
                {"message", result.error->message},
            }},
        };
        return result;
    }

    static std::future<InvokeResult> makeReadyFuture(InvokeResult result)
    {
        return std::async(std::launch::deferred, [result = std::move(result)]() mutable {
            return std::move(result);
        });
    }

    AdapterType& adapter_;
    task_scheduler::BasicTaskSchedulerFacade<EnableThreadSafety> scheduler_;
};

using RuntimeFacadeThreadSafe = BasicRuntimeFacade<true>;
using RuntimeFacadeUnsafe = BasicRuntimeFacade<false>;

} // namespace runtime