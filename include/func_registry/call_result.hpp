#pragma once

#include <any>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>

namespace func_registry {

namespace func_call_wrap_detail {

template<class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<class T>
constexpr bool is_arithmetic_v = std::is_arithmetic_v<remove_cvref_t<T>>;

template<class T>
T any_to_arithmetic(const std::any& a)
{
    using Out = remove_cvref_t<T>;
    static_assert(std::is_arithmetic_v<Out>, "Out must be arithmetic");

    const std::type_info& ti = a.type();

    if (ti == typeid(bool)) return static_cast<Out>(std::any_cast<bool>(a));
    if (ti == typeid(char)) return static_cast<Out>(std::any_cast<char>(a));
    if (ti == typeid(signed char)) return static_cast<Out>(std::any_cast<signed char>(a));
    if (ti == typeid(unsigned char)) return static_cast<Out>(std::any_cast<unsigned char>(a));
    if (ti == typeid(short)) return static_cast<Out>(std::any_cast<short>(a));
    if (ti == typeid(unsigned short)) return static_cast<Out>(std::any_cast<unsigned short>(a));
    if (ti == typeid(int)) return static_cast<Out>(std::any_cast<int>(a));
    if (ti == typeid(unsigned int)) return static_cast<Out>(std::any_cast<unsigned int>(a));
    if (ti == typeid(long)) return static_cast<Out>(std::any_cast<long>(a));
    if (ti == typeid(unsigned long)) return static_cast<Out>(std::any_cast<unsigned long>(a));
    if (ti == typeid(long long)) return static_cast<Out>(std::any_cast<long long>(a));
    if (ti == typeid(unsigned long long)) return static_cast<Out>(std::any_cast<unsigned long long>(a));
    if (ti == typeid(float)) return static_cast<Out>(std::any_cast<float>(a));
    if (ti == typeid(double)) return static_cast<Out>(std::any_cast<double>(a));
    if (ti == typeid(long double)) return static_cast<Out>(std::any_cast<long double>(a));

    throw std::runtime_error("any arithmetic conversion failed: stored type is not arithmetic");
}

} // namespace func_call_wrap_detail

template<class T>
struct any_result_converter
{
    static func_call_wrap_detail::remove_cvref_t<T> convert(const std::any& a)
    {
        using Out = func_call_wrap_detail::remove_cvref_t<T>;

        if constexpr (std::is_void_v<Out>)
        {
            if (a.has_value())
            {
                throw std::runtime_error("expected void result");
            }
            return;
        }
        else if constexpr (func_call_wrap_detail::is_arithmetic_v<Out>)
        {
            if (a.type() == typeid(Out))
            {
                return std::any_cast<Out>(a);
            }
            return func_call_wrap_detail::any_to_arithmetic<Out>(a);
        }
        else
        {
            return std::any_cast<Out>(a);
        }
    }
};

class FuncCallResult
{
public:
    FuncCallResult(std::any value, std::type_index declaredRet)
        : value_(std::move(value)), declared_ret_(declaredRet)
    {
    }

    const std::any& any() const noexcept { return value_; }
    std::type_index declaredReturnType() const noexcept { return declared_ret_; }

    template<class T>
    T as() const
    {
        return any_result_converter<T>::convert(value_);
    }

    template<class T>
    operator T() const
    {
        return as<T>();
    }

private:
    std::any value_;
    std::type_index declared_ret_{typeid(void)};
};

} // namespace func_registry