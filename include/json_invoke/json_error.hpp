#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace json_invoke {

class JsonInvokeError : public std::runtime_error {
public:
    JsonInvokeError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code))
    {
    }

    const std::string& code() const noexcept
    {
        return code_;
    }

private:
    std::string code_;
};

} // namespace json_invoke