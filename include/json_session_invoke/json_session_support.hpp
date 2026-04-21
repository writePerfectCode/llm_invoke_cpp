#pragma once

#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include <json_invoke/json_common.hpp>
#include <json_invoke/json_traits.hpp>
#include <func_registry/func_registry.hpp>
#include <type_meta/type_schema.hpp>

namespace json_session_invoke {

inline constexpr std::string_view default_handle_parameter_name = "handle";

inline std::string normalizeSessionToolNameToken(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());

    bool previous_was_separator = false;
    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0)
        {
            normalized.push_back(static_cast<char>(std::tolower(uch)));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator)
        {
            normalized.push_back('_');
            previous_was_separator = true;
        }
    }

    while (!normalized.empty() && normalized.front() == '_')
    {
        normalized.erase(normalized.begin());
    }

    while (!normalized.empty() && normalized.back() == '_')
    {
        normalized.pop_back();
    }

    return normalized.empty() ? std::string{"object"} : normalized;
}

inline std::string makeDefaultSessionFactoryToolName(std::string_view object_type_name)
{
    return "create_" + normalizeSessionToolNameToken(object_type_name);
}

inline std::string makeDefaultSessionDestroyToolName(std::string_view object_type_name)
{
    return "destroy_" + normalizeSessionToolNameToken(object_type_name);
}

struct ObjectHandle {
    std::string object_id;
    std::string object_type;
};

struct ObjectOptions {
    bool serialized{false};
    std::optional<std::chrono::milliseconds> idle_timeout{};
};

using SessionObjectHandle = ObjectHandle;
using SessionObjectOptions = ObjectOptions;

namespace detail {

template<typename T>
using stateful_remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<typename T>
struct is_shared_ptr : std::false_type {};

template<typename U>
struct is_shared_ptr<std::shared_ptr<U>> : std::true_type {};

template<typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<stateful_remove_cvref_t<T>>::value;

template<bool EnableThreadSafety>
class BasicStatefulObjectStore {
public:
    using Clock = std::chrono::steady_clock;
    using MutexType = std::conditional_t<EnableThreadSafety, std::shared_mutex, func_registry::NullSharedMutex>;
    using ObjectMutexType = std::recursive_mutex;

    template<typename T>
    struct ObjectAccess {
        std::shared_ptr<T> object;
        std::shared_ptr<ObjectMutexType> invocation_mutex;
    };

    template<typename T>
    void setObjectTypeName(std::string object_type_name)
    {
        if (object_type_name.empty())
        {
            throw std::invalid_argument("object type name must not be empty");
        }

        std::unique_lock<MutexType> lock(mutex_);
        object_type_names_[typeid(T)] = std::move(object_type_name);
        explicit_object_type_names_[typeid(T)] = object_type_names_[typeid(T)];
    }

    template<typename T>
    ObjectHandle emplace(std::shared_ptr<T> object, std::string object_type_name = {}, ObjectOptions options = {})
    {
        if (!object)
        {
            throw std::invalid_argument("object factory returned a null shared_ptr");
        }

        const std::string resolved_object_type_name = resolveOrRememberObjectTypeName<T>(std::move(object_type_name));
        const auto now = Clock::now();

        std::unique_lock<MutexType> lock(mutex_);
        cleanupExpiredObjectsLocked(now);
        const std::string object_id = makeObjectId();
        objects_.emplace(
            object_id,
            StoredObject{
                resolved_object_type_name,
                typeid(T),
                std::static_pointer_cast<void>(std::move(object)),
                options.serialized ? std::make_shared<ObjectMutexType>() : nullptr,
                std::move(options.idle_timeout),
                now,
                now});
        return ObjectHandle{object_id, resolved_object_type_name};
    }

    template<typename T, typename U>
    requires (!is_shared_ptr_v<U>)
    ObjectHandle emplace(U&& value, std::string object_type_name = {}, ObjectOptions options = {})
    {
        return emplace<T>(
            std::make_shared<T>(std::forward<U>(value)),
            std::move(object_type_name),
            std::move(options));
    }

    bool destroy(const ObjectHandle& handle)
    {
        ensureObjectId(handle.object_id);
        std::unique_lock<MutexType> lock(mutex_);
        cleanupExpiredObjectsLocked(Clock::now());
        return objects_.erase(handle.object_id) != 0;
    }

    bool destroy(std::string_view object_id)
    {
        ensureObjectId(object_id);
        std::unique_lock<MutexType> lock(mutex_);
        cleanupExpiredObjectsLocked(Clock::now());
        return objects_.erase(std::string(object_id)) != 0;
    }

    std::size_t cleanupExpiredObjects()
    {
        std::unique_lock<MutexType> lock(mutex_);
        return cleanupExpiredObjectsLocked(Clock::now());
    }

    template<typename T>
    ObjectAccess<T> checkoutObject(const ObjectHandle& handle)
    {
        return checkoutObject<T>(handle, objectTypeName<T>());
    }

    template<typename T>
    ObjectAccess<T> checkoutObject(const ObjectHandle& handle, std::string_view expected_object_type)
    {
        ensureObjectId(handle.object_id);

        std::unique_lock<MutexType> lock(mutex_);
        const auto now = Clock::now();
        cleanupExpiredObjectsLocked(now);
        StoredObject& entry = findObjectLocked(handle.object_id);
        validateObjectType(entry, handle, expected_object_type, typeid(T));
        entry.last_used_at = now;
        return ObjectAccess<T>{std::static_pointer_cast<T>(entry.object), entry.invocation_mutex};
    }

    template<typename T>
    std::string objectTypeName() const
    {
        std::shared_lock<MutexType> lock(mutex_);
        const auto it = object_type_names_.find(typeid(T));
        if (it != object_type_names_.end())
        {
            return it->second;
        }

        return fallbackObjectTypeName<T>();
    }

    template<typename T>
    std::optional<std::string> configuredObjectTypeName() const
    {
        std::shared_lock<MutexType> lock(mutex_);
        const auto it = explicit_object_type_names_.find(typeid(T));
        if (it == explicit_object_type_names_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

private:
    struct StoredObject {
        std::string object_type;
        std::type_index cpp_type{typeid(void)};
        std::shared_ptr<void> object;
        std::shared_ptr<ObjectMutexType> invocation_mutex;
        std::optional<std::chrono::milliseconds> idle_timeout;
        Clock::time_point created_at{};
        Clock::time_point last_used_at{};
    };

    template<typename T>
    static std::string fallbackObjectTypeName()
    {
        return typeid(T).name();
    }

    static void ensureObjectId(std::string_view object_id)
    {
        if (object_id.empty())
        {
            throw json_invoke::JsonInvokeError("invalid_object", "object handle is missing 'object_id'");
        }
    }

    template<typename T>
    std::string resolveOrRememberObjectTypeName(std::string object_type_name)
    {
        std::unique_lock<MutexType> lock(mutex_);
        auto& stored_name = object_type_names_[typeid(T)];
        if (!object_type_name.empty())
        {
            stored_name = std::move(object_type_name);
            explicit_object_type_names_[typeid(T)] = stored_name;
        }
        else if (stored_name.empty())
        {
            stored_name = fallbackObjectTypeName<T>();
        }

        return stored_name;
    }

    std::string makeObjectId()
    {
        std::array<unsigned int, 16> bytes{};
        std::uniform_int_distribution<unsigned int> distribution(0, 255);
        for (auto& byte : bytes)
        {
            byte = distribution(random_engine_);
        }

        bytes[6] = (bytes[6] & 0x0fU) | 0x40U;
        bytes[8] = (bytes[8] & 0x3fU) | 0x80U;

        std::ostringstream builder;
        builder << std::hex << std::setfill('0');

        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            builder << std::setw(2) << bytes[index];
            if (index == 3 || index == 5 || index == 7 || index == 9)
            {
                builder << '-';
            }
        }

        return builder.str();
    }

    std::size_t cleanupExpiredObjectsLocked(Clock::time_point now)
    {
        std::size_t removed = 0;
        for (auto it = objects_.begin(); it != objects_.end();)
        {
            const auto& timeout = it->second.idle_timeout;
            if (timeout.has_value() && now - it->second.last_used_at >= *timeout)
            {
                it = objects_.erase(it);
                ++removed;
                continue;
            }

            ++it;
        }

        return removed;
    }

    StoredObject& findObjectLocked(const std::string& object_id)
    {
        const auto it = objects_.find(object_id);
        if (it == objects_.end())
        {
            throw json_invoke::JsonInvokeError("invalid_object", "object not found: " + object_id);
        }

        return it->second;
    }

    static void validateObjectType(
        const StoredObject& entry,
        const ObjectHandle& handle,
        std::string_view expected_object_type,
        std::type_index expected_cpp_type)
    {
        if (entry.cpp_type != expected_cpp_type)
        {
            throw json_invoke::JsonInvokeError(
                "object_type_mismatch",
                "object '" + handle.object_id + "' is not compatible with the requested C++ type");
        }

        if (!expected_object_type.empty() && entry.object_type != expected_object_type)
        {
            throw json_invoke::JsonInvokeError(
                "object_type_mismatch",
                "object '" + handle.object_id + "' has type '" + entry.object_type + "', expected '" +
                    std::string(expected_object_type) + "'");
        }

        if (!handle.object_type.empty() && entry.object_type != handle.object_type)
        {
            throw json_invoke::JsonInvokeError(
                "object_type_mismatch",
                "object '" + handle.object_id + "' has type '" + entry.object_type + "', not '" +
                    handle.object_type + "'");
        }
    }

    mutable MutexType mutex_;
    std::unordered_map<std::string, StoredObject> objects_;
    std::unordered_map<std::type_index, std::string> object_type_names_;
    std::unordered_map<std::type_index, std::string> explicit_object_type_names_;
    mutable std::mt19937_64 random_engine_{std::random_device{}()};
};

} // namespace detail

} // namespace json_session_invoke

namespace json_invoke {

template<>
struct json_traits<json_session_invoke::ObjectHandle> {
    static json_session_invoke::ObjectHandle from_json_value(const json& value)
    {
        if (value.is_string())
        {
            const std::string object_id = value.get<std::string>();
            if (object_id.empty())
            {
                throw JsonInvokeError("conversion_failed", "object handle string must not be empty");
            }

            return json_session_invoke::ObjectHandle{object_id, {}};
        }

        if (!value.is_object())
        {
            throw JsonInvokeError("conversion_failed", "object handle expects a string or object value");
        }

        const auto object_id_it = value.find("object_id");
        if (object_id_it == value.end() || !object_id_it->is_string())
        {
            throw JsonInvokeError("conversion_failed", "object handle requires a string 'object_id'");
        }

        json_session_invoke::ObjectHandle handle;
        handle.object_id = object_id_it->get<std::string>();
        if (handle.object_id.empty())
        {
            throw JsonInvokeError("conversion_failed", "object handle 'object_id' must not be empty");
        }

        const auto object_type_it = value.find("object_type");
        if (object_type_it != value.end() && !object_type_it->is_null())
        {
            if (!object_type_it->is_string())
            {
                throw JsonInvokeError("conversion_failed", "object handle 'object_type' must be a string when provided");
            }

            handle.object_type = object_type_it->get<std::string>();
        }

        return handle;
    }

    static json to_json_value(const json_session_invoke::ObjectHandle& value)
    {
        json result{{"object_id", value.object_id}};
        if (!value.object_type.empty())
        {
            result["object_type"] = value.object_type;
        }

        return result;
    }
};

} // namespace json_invoke

template<>
struct func_registry::schema_traits<json_session_invoke::ObjectHandle> {
    static func_registry::TypeSchema schema()
    {
        return func_registry::objectSchema(
            {
                func_registry::property(
                    "object_id",
                    func_registry::described(
                        func_registry::stringSchema(),
                        "Opaque in-memory object identifier returned by a create tool.")),
                func_registry::property(
                    "object_type",
                    func_registry::described(
                        func_registry::stringSchema(),
                        "Logical object type name used for validation and debugging."),
                    false),
            },
            false);
    }
};