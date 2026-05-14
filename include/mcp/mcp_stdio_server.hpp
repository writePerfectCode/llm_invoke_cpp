#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <runtime/runtime_facade.hpp>

namespace mcp {

using json = runtime::json;

struct ServerInfo {
    std::string name{"llm_invoke_cpp"};
    std::string version{"0.1.0"};
    std::string protocol_version{"2025-03-26"};
    std::optional<std::string> instructions;
    bool tools_list_changed{false};
};

template<bool EnableThreadSafety = false>
class BasicMcpStdioServer {
public:
    using RuntimeType = runtime::BasicRuntimeFacade<EnableThreadSafety>;

    explicit BasicMcpStdioServer(RuntimeType& runtime)
        : runtime_(runtime)
    {
    }

    BasicMcpStdioServer(RuntimeType& runtime, ServerInfo server_info)
        : runtime_(runtime)
        , server_info_(std::move(server_info))
    {
    }

    std::optional<json> handleMessage(const json& message)
    {
        validateBaseMessage(message);

        const std::string method = message.at("method").get<std::string>();
        const auto params_it = message.find("params");
        const json& params = params_it == message.end() ? empty_object() : *params_it;

        const auto id_it = message.find("id");
        if (id_it == message.end())
        {
            handleNotification(method, params);
            return std::nullopt;
        }

        return handleRequest(*id_it, method, params);
    }

    bool processNextMessage(std::istream& input, std::ostream& output)
    {
        try
        {
            const auto message = readMessage(input);
            if (!message.has_value())
            {
                return false;
            }

            const auto response = handleMessage(*message);
            if (response.has_value())
            {
                writeMessage(output, *response);
            }
            return true;
        }
        catch (const json::parse_error& e)
        {
            writeMessage(output, makeErrorResponse(nullptr, -32700, std::string("Parse error: ") + e.what()));
            return true;
        }
        catch (const std::exception& e)
        {
            writeMessage(output, makeErrorResponse(nullptr, -32600, e.what()));
            return true;
        }
    }

    void serve(std::istream& input, std::ostream& output)
    {
        while (processNextMessage(input, output))
        {
        }
    }

    static std::optional<json> readMessage(std::istream& input)
    {
        std::optional<std::size_t> content_length;
        std::string line;
        bool saw_any_header = false;

        while (true)
        {
            if (!std::getline(input, line))
            {
                if (!saw_any_header)
                {
                    return std::nullopt;
                }
                throw std::runtime_error("unexpected end of stream while reading MCP headers");
            }

            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.empty() && !saw_any_header)
            {
                continue;
            }

            saw_any_header = true;
            if (line.empty())
            {
                break;
            }

            const auto separator = line.find(':');
            if (separator == std::string::npos)
            {
                continue;
            }

            std::string name = line.substr(0, separator);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            std::string value = line.substr(separator + 1);
            trimAsciiWhitespace(value);
            if (name == "content-length")
            {
                content_length = static_cast<std::size_t>(std::stoull(value));
            }
        }

        if (!content_length.has_value())
        {
            throw std::runtime_error("missing Content-Length header");
        }

        std::string payload(*content_length, '\0');
        input.read(payload.data(), static_cast<std::streamsize>(*content_length));
        if (static_cast<std::size_t>(input.gcount()) != *content_length)
        {
            throw std::runtime_error("unexpected end of stream while reading MCP body");
        }

        return json::parse(payload);
    }

    static void writeMessage(std::ostream& output, const json& message)
    {
        const std::string payload = message.dump();
        output << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        output.flush();
    }

private:
    static void validateBaseMessage(const json& message)
    {
        if (!message.is_object())
        {
            throw std::runtime_error("MCP message must be a JSON object");
        }

        if (message.value("jsonrpc", "") != "2.0")
        {
            throw std::runtime_error("MCP message must declare jsonrpc='2.0'");
        }

        const auto method_it = message.find("method");
        if (method_it == message.end() || !method_it->is_string())
        {
            throw std::runtime_error("MCP message must contain a string field 'method'");
        }
    }

    void handleNotification(const std::string& method, const json& params)
    {
        if (method == "notifications/initialized")
        {
            initialized_notification_seen_ = true;
            return;
        }

        if (!params.is_object())
        {
            throw std::runtime_error("notification params must be a JSON object");
        }
    }

    json handleRequest(const json& id, const std::string& method, const json& params)
    {
        if (!params.is_object())
        {
            return makeErrorResponse(id, -32602, "Request params must be a JSON object");
        }

        try
        {
            if (method == "initialize")
            {
                return makeSuccessResponse(id, handleInitialize(params));
            }

            if (method == "ping")
            {
                return makeSuccessResponse(id, json::object());
            }

            if (!initialize_seen_)
            {
                return makeErrorResponse(id, -32002, "Server not initialized");
            }

            if (method == "tools/list")
            {
                return makeSuccessResponse(id, handleToolsList(params));
            }

            if (method == "tools/call")
            {
                return handleToolsCall(id, params);
            }

            return makeErrorResponse(id, -32601, "Method not found: " + method);
        }
        catch (const json_invoke::JsonInvokeError& e)
        {
            return makeErrorResponse(id, -32602, e.what());
        }
        catch (const std::exception& e)
        {
            return makeErrorResponse(id, -32603, e.what());
        }
    }

    json handleInitialize(const json& params)
    {
        const auto version_it = params.find("protocolVersion");
        if (version_it == params.end() || !version_it->is_string())
        {
            throw json_invoke::JsonInvokeError("invalid_request", "initialize params must contain string protocolVersion");
        }

        initialize_seen_ = true;

        json result{
            {"protocolVersion", negotiateProtocolVersion(version_it->get<std::string>())},
            {"capabilities", {
                {"tools", {
                    {"listChanged", server_info_.tools_list_changed},
                }},
            }},
            {"serverInfo", {
                {"name", server_info_.name},
                {"version", server_info_.version},
            }},
        };

        if (server_info_.instructions.has_value())
        {
            result["instructions"] = *server_info_.instructions;
        }

        return result;
    }

    json handleToolsList(const json& params)
    {
        const auto cursor_it = params.find("cursor");
        if (cursor_it != params.end() && !cursor_it->is_null())
        {
            if (!cursor_it->is_string())
            {
                throw json_invoke::JsonInvokeError("invalid_request", "tools/list cursor must be a string when provided");
            }

            if (!cursor_it->get<std::string>().empty())
            {
                throw json_invoke::JsonInvokeError("invalid_request", "tools/list pagination cursor is not supported by this server");
            }
        }

        json tools = json::array();
        for (const auto& tool : runtime_.listTools())
        {
            tools.push_back(json{
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema},
            });
        }

        return json{{"tools", std::move(tools)}};
    }

    json handleToolsCall(const json& id, const json& params)
    {
        const auto name_it = params.find("name");
        if (name_it == params.end() || !name_it->is_string())
        {
            return makeErrorResponse(id, -32602, "tools/call params must contain string name");
        }

        json arguments = json::object();
        const auto arguments_it = params.find("arguments");
        if (arguments_it != params.end() && !arguments_it->is_null())
        {
            if (!arguments_it->is_object())
            {
                return makeErrorResponse(id, -32602, "tools/call arguments must be a JSON object when provided");
            }
            arguments = *arguments_it;
        }

        const auto invoke_result = runtime_.invoke(runtime::InvokeRequest{
            name_it->get<std::string>(),
            std::move(arguments),
        });

        if (!invoke_result.ok && invoke_result.error.has_value() && isProtocolErrorCode(invoke_result.error->code))
        {
            return makeErrorResponse(id, -32602, invoke_result.error->message);
        }

        return makeSuccessResponse(id, json{
            {"content", json::array({json{{"type", "text"}, {"text", renderTextResult(invoke_result)}}})},
            {"isError", !invoke_result.ok},
        });
    }

    std::string negotiateProtocolVersion(const std::string& requested_version) const
    {
        if (requested_version == server_info_.protocol_version)
        {
            return requested_version;
        }

        return server_info_.protocol_version;
    }

    static std::string renderTextResult(const runtime::InvokeResult& invoke_result)
    {
        if (invoke_result.ok)
        {
            return renderJsonText(invoke_result.value);
        }

        if (invoke_result.error.has_value())
        {
            return invoke_result.error->message;
        }

        return "unknown runtime error";
    }

    static std::string renderJsonText(const json& value)
    {
        if (value.is_string())
        {
            return value.get<std::string>();
        }

        return value.dump(2);
    }

    static bool isProtocolErrorCode(const std::string& code)
    {
        return code == "function_not_found"
            || code == "invalid_request"
            || code == "conversion_failed"
            || code == "invalid_object";
    }

    static json makeSuccessResponse(const json& id, json result)
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::move(result)},
        };
    }

    static json makeErrorResponse(const json& id, std::int32_t code, std::string message)
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", std::move(message)},
            }},
        };
    }

    static json makeErrorResponse(std::nullptr_t, std::int32_t code, std::string message)
    {
        return json{
            {"jsonrpc", "2.0"},
            {"id", nullptr},
            {"error", {
                {"code", code},
                {"message", std::move(message)},
            }},
        };
    }

    static const json& empty_object()
    {
        static const json value = json::object();
        return value;
    }

    static void trimAsciiWhitespace(std::string& value)
    {
        const auto not_space = [](unsigned char c) {
            return !std::isspace(c);
        };

        const auto begin = std::find_if(value.begin(), value.end(), not_space);
        const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
        if (begin >= end)
        {
            value.clear();
            return;
        }

        value = std::string(begin, end);
    }

    RuntimeType& runtime_;
    ServerInfo server_info_{};
    bool initialize_seen_{false};
    bool initialized_notification_seen_{false};
};

using McpStdioServerThreadSafe = BasicMcpStdioServer<true>;
using McpStdioServerUnsafe = BasicMcpStdioServer<false>;

} // namespace mcp