#ifndef BERTRAND_STRUCTS_UTIL_FUNC_H
#define BERTRAND_STRUCTS_UTIL_FUNC_H

#include <type_traits>  // std::enable_if_t<>, std::invoke_result_t<>, etc.
#include <utility>  // std::tuple<>, std::index_sequence_for<>, etc.
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>


namespace bertrand {
namespace util {


/* A placeholder function that returns a single, unmodified argument. */
struct identity {
    template <typename T>
    inline decltype(auto) operator()(T&& arg) const noexcept {
        return std::forward<T>(arg);
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
        LAMBDA,  // specifically those that do not capture
        FUNCTOR,  // any callable object that encapsulates state
        PYTHON_CALLABLE,  // A PyObject* of any kind (no compile-time checks)
        UNKNOWN
    };

private:

    static constexpr Category cat = [] {
        constexpr bool is_pointer = std::is_pointer_v<Func>;
        constexpr bool is_member = std::is_member_function_pointer_v<Func>;
        constexpr bool is_lambda = std::is_class_v<Func> && std::is_empty_v<Func>;
        constexpr bool is_functor = std::is_class_v<Func> && !std::is_empty_v<Func>;

        if constexpr (is_pyobject<Func>) {
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

    static_assert(
        cat != Category::UNKNOWN,
        "Func is not a valid C++ function or Python callable"
    );
    static_assert(
        std::is_invocable_v<Func, Args...> || (
            cat == Category::PYTHON_CALLABLE && std::conjunction_v<
                std::is_convertible<Args, PyObject*>...
            >
        ),
        "Func cannot be called with the provided arguments"
    );

    /* Detect the presence of const, volatile, and reference qualifiers for member
    methods of a class. */
    template <typename _Func>
    struct Qualifiers {
        using Class = void;
        static constexpr bool is_const = false;
        static constexpr bool is_volatile = false;
        static constexpr bool is_lvalue_ref = false;
        static constexpr bool is_rvalue_ref = false;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_volatile = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) &> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_lvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const &> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
        static constexpr bool is_lvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile &> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_volatile = true;
        static constexpr bool is_lvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile &> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
        static constexpr bool is_lvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) &&> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_rvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const &&> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
        static constexpr bool is_rvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) volatile &&> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_volatile = true;
        static constexpr bool is_rvalue_ref = true;
    };
    template <typename R, typename P, typename... A>
    struct Qualifiers<R(P::*)(A...) const volatile &&> : public Qualifiers<R(P::*)(A...)> {
        using Class = P;
        static constexpr bool is_const = true;
        static constexpr bool is_volatile = true;
        static constexpr bool is_rvalue_ref = true;
    };

    /* Detect whether a function is a free function or static member of its parent
    class (i.e. does not have a `this` pointer). */
    template <typename T, bool cond = cat == Category::PYTHON_CALLABLE, typename = void>
    struct _is_static : std:: false_type {};
    template <typename R, typename... A>
    struct _is_static<R(*)(A...)> : std::true_type {};
    template <typename T, bool cond>
    struct _is_static<T, cond, std::enable_if_t<cond>> : std::true_type {};

    /* Infer the return type for a C++ function or lambda. */
    template <typename T, bool cond = cat == Category::PYTHON_CALLABLE, typename = void>
    struct _ReturnType {
        using type = std::invoke_result_t<T, Args...>;
    };
    template <typename T, bool cond>
    struct _ReturnType<T, cond, std::enable_if_t<cond, void>> {
        using type = PyObject*;
    };

public:
    using Class = typename Qualifiers<Func>::Class;
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

    /* Check whether the templated method is an immediate member of the given type.
    NOTE: this does not consider inheritance, and requires an exact match for the given
    type. */
    template <typename Type>
    static constexpr bool defined_in = [] {
        static_assert(
            category == Category::MEMBER_FUNCTION_PTR,
            "Function is not a member of any class"
        );
        return std::is_same_v<Type, Class>;
    }();

    /* Check whether the templated function is a member (directly or indirectly) of the
    given type.  NOTE: this takes inheritance into account, and will return true if the
    method is defined in any of the given type's bases. */
    template <typename Type>
    static constexpr bool member_of = [] {
        static_assert(
            category == Category::MEMBER_FUNCTION_PTR,
            "Function is not a member of any class"
        );
        return std::is_base_of_v<Type, Class>;
    }();

};


}  // namespace util
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_FUNC_H
