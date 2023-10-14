// include guard: BERTRAND_STRUCTS_UTIL_FUNC_H
#ifndef BERTRAND_STRUCTS_UTIL_FUNC_H
#define BERTRAND_STRUCTS_UTIL_FUNC_H

#include <type_traits>  // std::enable_if_t<>, std::invoke_result_t<>, etc.
#include <utility>  // std::tuple<>, std::index_sequence_for<>, etc.
#include <Python.h>  // CPython API


/* NOTE: This file contains utilities for working with C++ function pointers and Python
 * callables, including various traits for compile-time introspection.
 */


namespace bertrand {
namespace structs {
namespace util {


/* A placeholder function that returns a single unmodified argument. */
struct identity {
    template <typename T>
    inline constexpr T&& operator()(T&& value) const noexcept {
        return std::forward<T>(value);
    }
};


/* A collection of traits to allow compile-time introspection of C++ function pointers
and Python callables. */
template <typename Func, typename... Args>
class FuncTraits {
public:

    /* An enum listing all the possible values for the category trait */
    enum class Category {
        FUNCTION_PTR,
        MEMBER_FUNCTION_PTR,
        LAMBDA,  // specifically those that do not capture any local variables
        FUNCTOR,  // any callable object that is not a lambda
        PYTHON_CALLABLE,  // A PyObject* of any kind (no type checking is done)
        UNKNOWN
    };

private:

    static constexpr Category cat = []() {
        constexpr bool is_python = std::is_convertible_v<Func, PyObject*>;
        constexpr bool is_pointer = std::is_pointer_v<Func>;
        constexpr bool is_member = std::is_member_function_pointer_v<Func>;
        constexpr bool is_lambda = std::is_class_v<Func> && std::is_empty_v<Func>;
        constexpr bool is_functor = std::is_class_v<Func> && !std::is_empty_v<Func>;

        if constexpr (is_python) {
            return Category::PYTHON_CALLABLE;
        } else if constexpr (is_pointer) {
            if constexpr (is_member) {
                return Category::MEMBER_FUNCTION_PTR;
            } else if constexpr (std::is_function_v<std::remove_pointer_t<Func>>) {
                return Category::FUNCTION_PTR;
            } else {
                return Category::UNKNOWN;
            }
        } else if constexpr (is_lambda) {
            return Category::LAMBDA;
        } else if constexpr (is_functor) {
            return Category::FUNCTOR;
        } else {
            return Category::UNKNOWN;
        }
    }();

    // sanity check
    static_assert(
        cat != Category::UNKNOWN,
        "Func is not a valid function pointer or Python callable"
    );
    static_assert(
        (cat == Category::PYTHON_CALLABLE) || std::is_invocable_v<Func, Args...>,
        "Func cannot be called with the provided arguments"
    );

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename _Func>
    struct Qualifiers {
        using Parent = void;
        static constexpr bool is_const = false;
        static constexpr bool is_volatile = false;
        static constexpr bool is_lvalue_ref = false;
        static constexpr bool is_rvalue_ref = false;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_volatile = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) &> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_lvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const &> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
        static constexpr bool is_lvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile &> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_volatile = true;
        static constexpr bool is_lvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile &> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
        static constexpr bool is_lvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) &&> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_rvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const &&> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
        static constexpr bool is_rvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile &&> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_volatile = true;
        static constexpr bool is_rvalue_ref = true;
    };

    /* Detect the presence of const, volatile, and reference qualifiers. */
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile &&> : public Qualifiers<R(P::*)(A...)> {
        using Parent = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
        static constexpr bool is_rvalue_ref = true;
    };

    /* Detect whether a function is a free function or static member of its parent
    class (i.e. does not have a `this` pointer). */
    template <typename T, typename = void>
    struct _is_static : std:: false_type {};

    /* Detect whether a function is a free function or static member of its parent
    class (i.e. does not have a `this` pointer). */
    template <typename R, typename... A>
    struct _is_static<R(*)(A...)> : std::true_type {};

    /* Detect whether a Python callable is static (true by definition). */
    template <typename T>
    struct _is_static<T, std::enable_if_t<cat == Category::PYTHON_CALLABLE>> :
        std::true_type
    {};

    /* Infer the return type for a C++ function or lambda. */
    template <typename T, bool cond = (cat == Category::PYTHON_CALLABLE), typename = void>
    struct _ReturnType {
        using type = std::invoke_result_t<T, Args...>;
    };

    /* Infer the return type for a Python callable. */
    template <typename T, bool cond>
    struct _ReturnType<T, cond, std::enable_if_t<cond, void>> {
        using type = PyObject*;
    };

public:
    using Parent = typename Qualifiers<Func>::Parent;
    using ArgTypes = std::tuple<Args...>;
    using ArgIndices = std::index_sequence_for<Args...>;
    using ReturnType = typename _ReturnType<Func>::type;
    using Signature = ReturnType(*)(Args...);

    static constexpr Category category = cat;
    static constexpr int n_args = sizeof...(Args);
    static constexpr int arity = n_args;  // alias
    static constexpr bool is_const = Qualifiers<Func>::is_const;
    static constexpr bool is_volatile = Qualifiers<Func>::is_volatile;
    static constexpr bool is_lvalue_ref = Qualifiers<Func>::is_lvalue_ref;
    static constexpr bool is_rvalue_ref = Qualifiers<Func>::is_rvalue_ref;
    static constexpr bool is_noexcept = std::is_nothrow_invocable_v<Func, Args...>;
    static constexpr bool is_static = _is_static<Func>::value;

    /* Check whether the templated method is an immediate member of the given type. */
    template <typename Class>
    static constexpr bool defined_in() {
        // NOTE: this does not consider inheritance, and requires an exact match for
        // the given type.
        static_assert(
            category == Category::MEMBER_FUNCTION_PTR,
            "Function is not a member of any class"
        );
        return std::is_same_v<Class, Parent>;
    }

    /* Check whether the templated function is a member (directly or indirectly) of the
    given type. */
    template <typename Class>
    static constexpr bool member_of() {
        // NOTE: this takes inheritance into account, and will return true if the
        // method is defined in any of the given type's bases.
        static_assert(
            category == Category::MEMBER_FUNCTION_PTR,
            "Function is not a member of any class"
        );
        return std::is_base_of_v<Class, Parent>;
    }

};


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_FUNC_H
