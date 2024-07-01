#ifndef BERTRAND_PYTHON_COMMON_FUNC_H
#define BERTRAND_PYTHON_COMMON_FUNC_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#elif defined(_MSC_VER)
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif


// TODO: ensure interpreter is properly initialized when a C++ function is converted
// into a Python equivalent.


// TODO: not sure if Arg.value() is being used correctly everywhere.


namespace bertrand {
namespace py {


// TODO: descriptors might also allow for multiple dispatch if I manage it correctly.
// -> The descriptor would store an internal map of types to Python/C++ functions, and
// could be appended to from either side.  This would replace pybind11's overloading
// mechanism, and would store signatures in topographical order.  When the descriptor
// is called, it would test each signature in order, and call the first one that
// fully matches

// TODO: This should be a separate class, which would basically be an overload set
// for py::Function instances, which would work at both the Python and C++ levels.
// Then, the descriptors would just store one of these natively, which would allow
// me to attach new implementations to the same descriptor via function overloading.
// I can also expose this to Python as a separate type, which would enable C++-style
// overloading in Python as well, possibly using annotations to specify the types.
// -> That's hard in current Python, but will be possible in a unified manner in
// Python 3.13, which allows deferred computation of type annotations.

// TODO: this would have to use type erasure to store the functions, which could make
// things somewhat challenging.  I wouldn't be able to template on the function
// signatures, and would have to store them as dynamic objects, and then use
// topological sorting to find the best match.  This is a lot of work, but would be
// an awesome feature to port back to Python, and would allow me to work in other
// features from DynamicFunc at the same time.  The composite would also have an
// attach() method, and would essentially implement the composite pattern.

// TODO: the best way to do this would be to allow both patterns to coexist, and have
// attach() work with both.  If You attach a py::Function to an object, it will
// avoid any overloading and call the function as fast as possible.  If you attach
// a py::OverloadSet, it will search the overloads topographically and call the best
// match.  This probably yields a set of descriptor classes like so:

// py::Method<py::Function<...>> -> calls the function directly
// py::Method<py::OverloadSet> -> calls the best match from the overload set
// py::ClassMethod<py::Function<...>>
// py::ClassMethod<py::OverloadSet>
// py::StaticMethod<py::Function<...>>
// py::StaticMethod<py::OverloadSet>
// py::Property<Return>  -> has a fixed signature for all 3 methods, which cannot be overloaded

// Then, in Python:

// @bertrand.function
// def foo(a, b):  # base implementation
//      raise NotImplementedError()

// Would create a py::OverloadSet object, which could be overloaded and attached like
// any other function.  Eventually, it would even be able to infer overloads from
// annotations:

// @foo.overload
// def foo(a: int, b: int) -> int:
//      return a + b

// For simplicity and backwards compatiblity, the appropriate types could also be
// specified in the overload decorator itself:

// @foo.overload(a = int, b = int)
// def foo(a, b):
//      return a + b

// This is more achievable in the short term, and correctly handling annotations
// will come later, once Python 3.13 is released and I figure out how to resolve
// annotations dynamically.


// -> Use a Trie to store the overloads, and then search it using a depth-first search
// to backtrack.  Each node in the trie will store a list of types from most to least
// specific, as determined by either a typecheck<> or issubclass() call (Maybe these can
// be unified as check_type<>?).  Each node will need the following:

// 1.   An ordered map of types to the next node in the trie, which is sorted in
//      topological order.
// 2.   An optional default value for this argument.  If we've hit the end of the
//      argument list and the current node does not have a terminal function, then
//      we search the above map from left to right and recur for each type that has
//      a default value.
// 3.   An unordered map of keywords to the next node in the trie, which is used when
//      positional arguments have been exhausted.  If no keyword is found, children
//      will be tried sequentially according to their default values before
//      backtracking.
// 4.   A terminal function to call if this is the last argument, which represents the
//      end of a search.

// The functions that are inserted into an overload set cannot have *args, **kwargs
// packs, and if no match can be found, the default implementation will be called,
// which can have an arbitrary signature and will most likely raise an error.

// -> Alternatively, the decorated function is analyzed just like every other one, and
// if it uses generic types, then it will serve as a catch-all fallback.  It (and only
// it) is allowed to have *args, **kwargs, and will be called if no other match is found.

// Maybe this makes DynamicFunc obsolete, since the primary use of that was to
// facilitate @dispatch?  Maybe the ability to modify defaults is built into
// py::Function, and extra arguments are handled by the dispatch mechanism.


namespace impl {

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda. */
    template <typename T>
    struct GetSignature;
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...)> { using type = R(A...); };
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...) noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...)> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile noexcept> { using type = R(A...); };
    template <impl::has_call_operator T>
    struct GetSignature<T> { using type = GetSignature<decltype(&T::operator())>::type; };

    enum class CallPolicy {
        no_args,
        one_arg,
        positional,
        keyword
    };

}


/* A universal function wrapper that can represent either a Python function exposed to
C++, or a C++ function exposed to Python with equivalent semantics.

This class contains 2 complementary components depending on how it was constructed:

    1.  A Python function object, which can be automatically generated from a C++
        function pointer, lambda, or object with an overloaded call operator.  If
        it was automatically generated, it will appear to Python as a
        `builtin_function_or_method` that forwards to the C++ function when called.

    2.  A C++ `std::function` that holds the C++ implementation in a type-erased
        form.  This is directly called when the function is invoked from C++, and it
        can be automatically generated from a Python function using the ::borrow() and
        ::steal() constructors.  If it was automatically generated, it will delegate to
        the Python function through the C API.

The combination of these two components allows the function to be passed and called
with identical semantics in both languages, including automatic type conversions, error
handling, keyword arguments, default values, and container unpacking, all of which is
resolved statically at compile time by introspecting the underlying signature (which
can be deduced using CTAD).  In order to facilitate this, bertrand provides a
lightweight `py::Arg` annotation, which can be placed directly in the function
signature to enable these features without impacting performance or type safety.

For instance, consider the following function:

    int subtract(int x, int y) {
        return x - y;
    }

We can directly wrap this as a `py::Function` if we want, which does not alter the
calling convention or signature in any way:

    py::Function func("subtract", subtract);
    func(1, 2);  // returns -1

If this function is exported to Python, it's call signature will remain unchanged,
meaning that both arguments must be supplied as positional-only arguments, and no
default values will be considered.

    static const py::Code script = R"(
        func(1, 2)  # ok
        # func(1)  # error: missing required positional argument
        # func(x = 1, y = 2)  # error: unexpected keyword argument
    )"_python;

    script({{"func", func}});  // prints -1

We can add parameter names and default values by annotating the C++ function (or a
wrapper around it) with `py::Arg` tags.  For instance:

    auto wrapper = [](py::Arg<"x", int> x, py::Arg<"y", int>::optional y = 2) {
        return subtract(x, y);
    };

    py::Function func("subtract", wrapper, py::arg<"y"> = 2);

Note that the annotations themselves are implicitly convertible to the underlying
argument types, so they should be acceptable as inputs to most functions without any
additional syntax.  If necessary, they can be explicitly dereferenced through the `*`
and `->` operators, or by accessing their `.value()` method directly, which comprises
their entire interface.  Also note that for each argument marked as `::optional`, we
must provide a default value within the function's constructor, which will be
substituted whenever we call the function without specifying that argument.

With this in place, we can now do the following:

    func(1);
    func(1, 2);
    func(1, py::arg<"y"> = 2);

    // or, equivalently:
    static constexpr auto x = py::arg<"x">;
    static constexpr auto y = py::arg<"y">;

    func(x = 1);
    func(x = 1, y = 2);
    func(y = 2, x = 1);  // keyword arguments can have arbitrary order

All of which will return the same result as before.  The function can also be passed to
Python and called with the same semantics:

    static const py::Code script = R"(
        func(1)
        func(1, 2)
        func(1, y = 2)
        func(x = 1)
        func(x = 1, y = 2)
        func(y = 2, x = 1)
    )"_python;

    script({{"func", func}});

What's more, all of the logic necessary to handle these cases is resolved statically at
compile time, meaning that there is no runtime overhead for using these annotations,
and no additional code is generated for the function itself.  When it is called from
C++, all we have to do is inspect the provided arguments and match them against the
underlying signature, which generates a compile time index sequence that can be used to
reorder the arguments and insert default values as needed.  In fact, each of the above
invocations will be transformed into the same underlying function call, with the same
performance characteristics as the original function (disregarding any overhead from
the `std::function` wrapper itself).

Additionally, since all arguments are evaluated purely at compile time, we can enforce
strong type safety guarantees on the function signature and disallow invalid calls
using a template constraint.  This means that proper call syntax is automatically
enforced throughout the codebase, in a way that allows static analyzers (like clangd)
to give proper syntax highlighting and LSP support.

Lastly, besides the `py::Arg` annotation, Bertrand also provides equivalent `py::Args`
and `py::Kwargs` tags, which represent variadic positional and keyword arguments,
respectively.  These will automatically capture an arbitrary number of arguments in
addition to those specified in the function signature itself, and will encapsulate them
in either a `std::vector<T>` or `std::unordered_map<std::string_view, T>`, respectively.
The allowable types can be specified by templating the annotation on the desired type,
to which all captured arguments must be convertible.

Similarly, Bertrand allows Pythonic unpacking of supported containers into the function
signature via the `*` and `**` operators, which emulate their Python equivalents.  Note
that in order to properly enable this, the `*` operator must be overloaded to return a
`py::Unpack` object or one of its subclasses, which is done automatically for any
iterable subclass of py::Object.  Additionally, unpacking a container like this carries
with it special restrictions, including the following:

    1.  The unpacked container must be the last argument in its respective category
        (positional or keyword), and there can only be at most one of each at the call
        site.  These are not reflected in ordinary Python, but are necessary to ensure
        that compile-time argument matching is unambiguous.
    2.  The container's value type must be convertible to each of the argument types
        that follow it in the function signature, or else a compile error will be
        raised.
    3.  If double unpacking is performed, then the container must yield key-value pairs
        where the key is implicitly convertible to a string, and the value is
        convertible to the corresponding argument type.  If this is not the case, a
        compile error will be raised.
    4.  If the container does not contain enough elements to satisfy the remaining
        arguments, or it contains too many, a runtime error will be raised when the
        function is called.  Since it is impossible to know the size of the container
        at compile time, this is the only way to enforce this constraint.
*/
template <typename F = Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)>
class Function : public Function<typename impl::GetSignature<F>::type> {};


template <typename Return, typename... Target>
class Function<Return(Target...)> : public Object, public impl::FunctionTag {
    using Base = Object;
    using Self = Function;

protected:

    /* Index into a parameter pack using template recursion. */
    template <size_t I>
    static void get_arg() {
        static_assert(false, "index out of range for parameter pack");
    }

    template <size_t I, typename T, typename... Ts>
    static decltype(auto) get_arg(T&& curr, Ts&&... next) {
        if constexpr (I == 0) {
            return std::forward<T>(curr);
        } else {
            return get_arg<I - 1>(std::forward<Ts>(next)...);
        }
    }

    template <typename T>
    struct Inspect {
        using type                          = T;
        static constexpr StaticStr name     = "";
        static constexpr bool opt           = false;
        static constexpr bool pos           = true;
        static constexpr bool kw            = false;
        static constexpr bool kw_only       = false;
        static constexpr bool args          = false;
        static constexpr bool kwargs        = false;
    };

    template <typename T> requires (std::derived_from<std::decay_t<T>, impl::ArgTag>)
    struct Inspect<T> {
        using U = std::decay_t<T>;
        using type                          = U::type;
        static constexpr StaticStr name     = U::name;
        static constexpr bool opt           = U::is_optional;
        static constexpr bool pos           = !U::is_variadic && !U::is_keyword;
        static constexpr bool kw            = !U::is_variadic && U::is_keyword;
        static constexpr bool kw_only       = !U::is_variadic && !U::is_positional;
        static constexpr bool args          = U::is_variadic && U::is_positional;
        static constexpr bool kwargs        = U::is_variadic && U::is_keyword;
    };

    template <typename... Args>
    class Signature {

        template <StaticStr name, typename... Ts>
        static constexpr size_t index_helper = 0;
        template <StaticStr name, typename T, typename... Ts>
        static constexpr size_t index_helper<name, T, Ts...> =
            (Inspect<T>::name == name) ? 0 : 1 + index_helper<name, Ts...>;

        template <typename... Ts>
        static constexpr size_t opt_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t opt_index_helper<T, Ts...> =
            Inspect<T>::opt ? 0 : 1 + opt_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t opt_count_helper = I;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t opt_count_helper<I, T, Ts...> =
            opt_count_helper<I + Inspect<T>::opt, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t pos_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t pos_count_helper<I, T, Ts...> =
            pos_count_helper<I + Inspect<T>::pos, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t pos_opt_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t pos_opt_count_helper<I, T, Ts...> =
            pos_opt_count_helper<I + (Inspect<T>::pos && Inspect<T>::opt), Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_index_helper<T, Ts...> =
            Inspect<T>::kw ? 0 : 1 + kw_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_count_helper<I, T, Ts...> =
            kw_count_helper<I + Inspect<T>::kw, Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_only_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_only_index_helper<T, Ts...> =
            Inspect<T>::kw_only ? 0 : 1 + kw_only_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_only_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_only_count_helper<I, T, Ts...> =
            kw_only_count_helper<I + Inspect<T>::kw_only, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_only_opt_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_only_opt_count_helper<I, T, Ts...> =
            kw_only_opt_count_helper<I + (Inspect<T>::kw_only && Inspect<T>::opt), Ts...>;

        template <typename... Ts>
        static constexpr size_t args_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t args_index_helper<T, Ts...> =
            Inspect<T>::args ? 0 : 1 + args_index_helper<Ts...>;

        template <typename... Ts>
        static constexpr size_t kwargs_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kwargs_index_helper<T, Ts...> =
            Inspect<T>::kwargs ? 0 : 1 + kwargs_index_helper<Ts...>;

    public:
        static constexpr size_t size = sizeof...(Args);

        /* Retrieve the (annotated) type at index I. */
        template <size_t I> requires (I < size)
        using type = std::tuple_element<I, std::tuple<Args...>>::type;

        /* Find the index of the named argument, or `size` if the argument is not
        present. */
        template <StaticStr name>
        static constexpr size_t index = index_helper<name, Args...>;
        template <StaticStr name>
        static constexpr bool contains = index<name> != size;

        /* Get the index of the first optional argument, or `size` if no optional
        arguments are present. */
        static constexpr size_t opt_index = opt_index_helper<Args...>;
        static constexpr size_t opt_count = opt_count_helper<0, Args...>;
        static constexpr bool has_opt = opt_index != size;

        /* Get the number of positional-only arguments. */
        static constexpr size_t pos_count = pos_count_helper<0, Args...>;
        static constexpr size_t pos_opt_count = pos_opt_count_helper<0, Args...>;
        static constexpr bool has_pos = pos_count > 0;

        /* Get the index of the first keyword argument, or `size` if no keywords are
        present. */
        static constexpr size_t kw_index = kw_index_helper<Args...>;
        static constexpr size_t kw_count = kw_count_helper<0, Args...>;
        static constexpr bool has_kw = kw_index != size;

        /* Get the index of the first keyword-only argument, or `size` if no
        keyword-only arguments are present. */
        static constexpr size_t kw_only_index = kw_only_index_helper<Args...>;
        static constexpr size_t kw_only_count = kw_only_count_helper<0, Args...>;
        static constexpr size_t kw_only_opt_count = kw_only_opt_count_helper<0, Args...>;
        static constexpr bool has_kw_only = kw_only_index != size;

        /* Get the index of the first variadic positional argument, or `size` if
        variadic positional arguments are not allowed. */
        static constexpr size_t args_index = args_index_helper<Args...>;
        static constexpr bool has_args = args_index != size;

        /* Get the index of the first variadic keyword argument, or `size` if
        variadic keyword arguments are not allowed. */
        static constexpr size_t kwargs_index = kwargs_index_helper<Args...>;
        static constexpr bool has_kwargs = kwargs_index != size;

    private:

        template <size_t I, typename... Ts>
        static constexpr bool validate = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate<I, T, Ts...> = [] {
            if constexpr (Inspect<T>::pos) {
                static_assert(
                    I == index<Inspect<T>::name> || Inspect<T>::name == "",
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_index,
                    "positional-only arguments must precede keywords"
                );
                static_assert(
                    I < args_index,
                    "positional-only arguments must precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_index,
                    "positional-only arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_index || Inspect<T>::opt,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Inspect<T>::kw) {
                static_assert(
                    I == index<Inspect<T>::name>,
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_only_index || Inspect<T>::kw_only,
                    "positional-or-keyword arguments must precede keyword-only arguments"
                );
                static_assert(
                    !(Inspect<T>::kw_only && has_args && I < args_index),
                    "keyword-only arguments must not precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_index,
                    "keyword arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_index || Inspect<T>::opt || Inspect<T>::kw_only,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Inspect<T>::args) {
                static_assert(
                    I < kwargs_index,
                    "variadic positional arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I == args_index,
                    "signature must not contain multiple variadic positional arguments"
                );

            } else if constexpr (Inspect<T>::kwargs) {
                static_assert(
                    I == kwargs_index,
                    "signature must not contain multiple variadic keyword arguments"
                );
            }

            return validate<I + 1, Ts...>;
        }();

    public:

        /* If the target signature does not conform to Python calling conventions, throw
        an informative compile error. */
        static constexpr bool valid = validate<0, Args...>;

        /* Check whether the signature contains a keyword with the given name at
        runtime.  Use contains<"name"> to check at compile time instead. */
        static bool has_keyword(const char* name) {
            return ((std::strcmp(Inspect<Args>::name, name) == 0) || ...);
        }

        /* Check whether the argument at index I is marked as optional. */
        template <size_t I> requires (I < size)
        static constexpr bool is_opt = Inspect<type<I>>::opt;

        /* Check whether the named argument is marked as optional. */
        template <StaticStr name> requires (index<name> < size)
        static constexpr bool is_opt_kw = Inspect<type<index<name>>>::opt;

    };

    using target = Signature<Target...>;
    static_assert(target::valid);

    class DefaultValues {

        template <size_t I>
        struct Value  {
            using annotation = target::template type<I>;
            using type = std::remove_cvref_t<typename Inspect<annotation>::type>;
            static constexpr StaticStr name = Inspect<annotation>::name;
            static constexpr size_t index = I;
            type value;
        };

        template <size_t I, typename Tuple, typename... Ts>
        struct CollectDefaults { using type = Tuple; };
        template <size_t I, typename... Defaults, typename T, typename... Ts>
        struct CollectDefaults<I, std::tuple<Defaults...>, T, Ts...> {
            template <typename U>
            struct Wrap {
                using type = CollectDefaults<
                    I + 1, std::tuple<Defaults...>, Ts...
                >::type;
            };
            template <typename U> requires (Inspect<U>::opt)
            struct Wrap<U> {
                using type = CollectDefaults<
                    I + 1, std::tuple<Defaults..., Value<I>>, Ts...
                >::type;
            };
            using type = Wrap<T>::type;
        };

        using tuple = CollectDefaults<0, std::tuple<>, Target...>::type;

        template <size_t I, typename... Ts>
        struct find_helper { static constexpr size_t value = 0; };
        template <size_t I, typename T, typename... Ts>
        struct find_helper<I, std::tuple<T, Ts...>> { static constexpr size_t value = 
            (I == T::index) ? 0 : 1 + find_helper<I, std::tuple<Ts...>>::value;
        };

        /* Statically analyzes the arguments that are supplied to the function's
        constructor, so that they fully satisfy the default value annotations. */
        template <typename... Source>
        struct Parse {
            using source = Signature<Source...>;

            /* Recursively check whether the default values fully satisfy the target
            signature. */
            template <size_t I, size_t J>
            static constexpr bool enable_recursive = true;
            template <size_t I, size_t J> requires (I < target::opt_count && J < source::size)
            static constexpr bool enable_recursive<I, J> = [] {
                using D = std::tuple_element<I, tuple>::type;
                using V = source::template type<J>;
                if constexpr (Inspect<V>::pos) {
                    if constexpr (
                        !std::convertible_to<typename Inspect<V>::type, typename D::type>
                    ) {
                        return false;
                    }
                } else if constexpr (Inspect<V>::kw) {
                    if constexpr (
                        !target::template contains<Inspect<V>::name> ||
                        !source::template contains<D::name>
                    ) {
                        return false;
                    } else {
                        constexpr size_t idx = target::template index<Inspect<V>::name>;
                        using D2 = std::tuple_element<find_helper<idx>::value, tuple>::type;
                        if constexpr (
                            !std::convertible_to<typename Inspect<V>::type, typename D2::type>
                        ) {
                            return false;
                        }
                    }
                } else {
                    return false;
                }
                return enable_recursive<I + 1, J + 1>;
            }();

            /* Constructor is only enabled if the default values are fully satisfied. */
            static constexpr bool enable =
                source::valid && target::opt_count == source::size && enable_recursive<0, 0>;

            template <size_t I>
            static constexpr decltype(auto) build_recursive(Source&&... values) {
                if constexpr (I < source::kw_index) {
                    return get_arg<I>(std::forward<Source>(values)...);
                } else {
                    using D = std::tuple_element<I, tuple>::type;
                    return get_arg<source::template index<D::name>>(
                        std::forward<Source>(values)...
                    ).value();
                }
            }

            /* Build the default values tuple from the provided arguments, reordering them
            as needed to account for keywords. */
            template <size_t... Is>
            static constexpr auto build(std::index_sequence<Is...>, Source&&... values) {
                return tuple(build_recursive<Is>(std::forward<Source>(values)...)...);
            }

        };

        tuple values;

    public:

        template <typename... Source> requires (Parse<Source...>::enable)
        DefaultValues(Source&&... source) : values(Parse<Source...>::build(
            std::make_index_sequence<sizeof...(Source)>{},
            std::forward<Source>(source)...
        )) {}

        DefaultValues(const DefaultValues& other) : values(other.values) {}
        DefaultValues(DefaultValues&& other) : values(std::move(other.values)) {}

        /* Constrain the function's constructor to enforce `::opt` annotations in
        the target signature. */
        template <typename... Source>
        static constexpr bool enable = Parse<Source...>::enable;

        /* Get the type of the default value associated with the target argument at
        index I. */
        template <size_t I>
        static constexpr size_t find = find_helper<I, tuple>::value;

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        const auto get() const {
            return std::get<find<I>>(values).value;
        };

    };

    template <typename... Source>
    class Arguments {
        using source = Signature<Source...>;

        template <size_t I>
        using T = target::template type<I>;
        template <size_t J>
        using S = source::template type<J>;

        template <size_t J, typename P>
        static constexpr bool check_target_args = true;
        template <size_t J, typename P> requires (J < source::kw_index)
        static constexpr bool check_target_args<J, P> = [] {
            if constexpr (Inspect<S<J>>::args) {
                if constexpr (!std::convertible_to<typename Inspect<S<J>>::type, P>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<S<J>, P>) {
                    return false;
                }
            }
            return check_target_args<J + 1, P>;
        }();

        template <size_t J, typename KW>
        static constexpr bool check_target_kwargs = true;
        template <size_t J, typename KW> requires (J < source::size)
        static constexpr bool check_target_kwargs<J, KW> = [] {
            if constexpr (Inspect<S<J>>::kwargs) {
                if constexpr (!std::convertible_to<typename Inspect<S<J>>::type, KW>) {
                    return false;
                }
            } else {
                if constexpr (
                    (
                        target::has_args &&
                        target::template index<Inspect<S<J>>::name> < target::args_index
                    ) || (
                        !target::template contains<Inspect<S<J>>::name> &&
                        !std::convertible_to<typename Inspect<S<J>>::type, KW>
                    )
                ) {
                    return false;
                }
            }
            return check_target_kwargs<J + 1, KW>;
        }();

        template <size_t I, typename P>
        static constexpr bool check_source_args = true;
        template <size_t I, typename P> requires (I < target::size && I <= target::args_index)
        static constexpr bool check_source_args<I, P> = [] {
            if constexpr (Inspect<T<I>>::args) {
                if constexpr (!std::convertible_to<P, typename Inspect<T<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (
                    !std::convertible_to<P, typename Inspect<T<I>>::type> ||
                    source::template contains<Inspect<T<I>>::name>
                ) {
                    return false;
                }
            }
            return check_source_args<I + 1, P>;
        }();

        template <size_t I, typename KW>
        static constexpr bool check_source_kwargs = true;
        template <size_t I, typename KW> requires (I < target::size)
        static constexpr bool check_source_kwargs<I, KW> = [] {
            // TODO: does this work as expected?
            if constexpr (Inspect<T<I>>::kwargs) {
                if constexpr (!std::convertible_to<KW, typename Inspect<T<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<KW, typename Inspect<T<I>>::type>) {
                    return false;
                }
            }
            return check_source_kwargs<I + 1, KW>;
        }();

        /* Recursively check whether the source arguments conform to Python calling
        conventions (i.e. no positional arguments after a keyword, no duplicate
        keywords, etc.), fully satisfy the target signature, and are convertible to the
        expected types, after accounting for parameter packs in both signatures. */
        template <size_t I, size_t J>
        static constexpr bool enable_recursive = true;
        template <size_t I, size_t J> requires (I < target::size && J >= source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            if constexpr (Inspect<T<I>>::args || Inspect<T<I>>::opt) {
                return enable_recursive<I + 1, J>;
            } else if constexpr (Inspect<T<I>>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T<I>>::type>;
            }
            return false;
        }();
        template <size_t I, size_t J> requires (I >= target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = false;
        template <size_t I, size_t J> requires (I < target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            // ensure target arguments are present & expand variadic parameter packs
            if constexpr (Inspect<T<I>>::pos) {
                if constexpr (
                    (J >= source::kw_index && !Inspect<T<I>>::opt) ||
                    (Inspect<T<I>>::name != "" && source::template contains<Inspect<T<I>>::name>)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::kw_only) {
                if constexpr (
                    J < source::kw_index ||
                    (!source::template contains<Inspect<T<I>>::name> && !Inspect<T<I>>::opt)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::kw) {
                if constexpr ((
                    J < source::kw_index &&
                    source::template contains<Inspect<T<I>>::name>
                ) || (
                    J >= source::kw_index &&
                    !source::template contains<Inspect<T<I>>::name> &&
                    !Inspect<T<I>>::opt
                )) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::args) {
                if constexpr (!check_target_args<J, typename Inspect<T<I>>::type>) {
                    return false;
                }
                return enable_recursive<I + 1, source::kw_index>;
            } else if constexpr (Inspect<T<I>>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T<I>>::type>;
            } else {
                return false;
            }

            // validate source arguments & expand unpacking operators
            if constexpr (Inspect<S<J>>::pos) {
                if constexpr (!std::convertible_to<S<J>, T<I>>) {
                    return false;
                }
            } else if constexpr (Inspect<S<J>>::kw) {
                if constexpr (target::template contains<Inspect<S<J>>::name>) {
                    using type = T<target::template index<Inspect<S<J>>::name>>;
                    if constexpr (!std::convertible_to<S<J>, type>) {
                        return false;
                    }
                } else if constexpr (!target::has_kwargs) {
                    using type = Inspect<T<target::kwargs_index>>::type;
                    if constexpr (!std::convertible_to<S<J>, type>) {
                        return false;
                    }
                }
            } else if constexpr (Inspect<S<J>>::args) {
                if constexpr (!check_source_args<I, typename Inspect<S<J>>::type>) {
                    return false;
                }
                return enable_recursive<target::args_index + 1, J + 1>;
            } else if constexpr (Inspect<S<J>>::kwargs) {
                return check_source_kwargs<I, typename Inspect<S<J>>::type>;
            } else {
                return false;
            }

            // advance to next argument pair
            return enable_recursive<I + 1, J + 1>;
        }();

        template <size_t I, typename T>
        static constexpr void build_kwargs(
            std::unordered_map<std::string, T>& map,
            Source&&... args
        ) {
            using Arg = S<source::kw_index + I>;
            if constexpr (!target::template contains<Inspect<Arg>::name>) {
                map.emplace(
                    Inspect<Arg>::name,
                    get_arg<source::kw_index + I>(std::forward<Source>(args)...)
                );
            }
        }

    public:

        /* Call operator is only enabled if source arguments are well-formed and match
        the target signature. */
        static constexpr bool enable = source::valid && enable_recursive<0, 0>;

        //////////////////////////
        ////    C++ -> C++    ////
        //////////////////////////

        /* The cpp() method is used to convert an index sequence over the target
         * signature into the corresponding values passed from the call site or drawn
         * from the function's defaults.  It is complicated by the presence of variadic
         * parameter packs in both the target signature and the call arguments, which
         * have to be handled as a cross product of possible combinations.  This yields
         * a total of 20 cases, which are represented as 5 separate specializations for
         * each of the possible target categories, as well as 4 different calling
         * conventions based on the presence of *args and/or **kwargs at the call site.
         *
         * Unless a variadic parameter pack is given at the call site, all of these
         * are resolved entirely at compile time by reordering the arguments using
         * template recursion.  However, because the size of a variadic parameter pack
         * cannot be determined at compile time, calls that use these will have to
         * extract values at runtime, and may therefore raise an error if a
         * corresponding value does not exist in the parameter pack, or if there are
         * extras that are not included in the target signature.
         */

        #define NO_UNPACK \
            template <size_t I>
        #define ARGS_UNPACK \
            template <size_t I, typename Iter, std::sentinel_for<Iter> End>
        #define KWARGS_UNPACK \
            template <size_t I, typename Mapping>
        #define ARGS_KWARGS_UNPACK \
            template <size_t I, typename Iter, std::sentinel_for<Iter> End, typename Mapping>

        NO_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Pack = std::vector<typename Inspect<T<I>>::type>;
            Pack vec;
            if constexpr (I < source::kw_index) {
                constexpr size_t diff = source::kw_index - I;
                vec.reserve(diff);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(get_arg<I + Js>(std::forward<Source>(args)...)), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
            }
            return vec;
        }

        NO_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T<I>>::type>;
            Pack pack;
            []<size_t... Js>(
                std::index_sequence<Js...>,
                Pack& pack,
                Source&&... args
            ) {
                (build_kwargs<Js>(pack, std::forward<Source>(args)...), ...);
            }(
                std::make_index_sequence<source::size - source::kw_index>{},
                pack,
                std::forward<Source>(args)...
            );
            return pack;
        }

        ARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "positional-only parameter at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        ARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            using Pack = std::vector<typename Inspect<T<I>>::type>;
            Pack vec;
            if constexpr (I < source::args_index) {
                constexpr size_t diff = source::args_index - I;
                vec.reserve(diff + size);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(get_arg<I + Js>(std::forward<Source>(args)...)), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
                vec.insert(vec.end(), iter, end);
            }
            return vec;
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T<I>>::type>;
            Pack pack;
            []<size_t... Js>(
                std::index_sequence<Js...>,
                Pack& pack,
                Source&&... args
            ) {
                (build_kwargs<Js>(pack, std::forward<Source>(args)...), ...);
            }(
                std::make_index_sequence<source::size - source::kw_index>{},
                pack,
                std::forward<Source>(args)...
            );
            for (const auto& [key, value] : map) {
                if (pack.contains(key)) {
                    throw TypeError("duplicate value for parameter '" + key + "'");
                }
                pack[key] = value;
            }
            return pack;
        }

        ARGS_KWARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter != end) {
                    return *(iter++);
                } else if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack args - no match for parameter '" +
                            std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        #undef NO_UNPACK
        #undef ARGS_UNPACK
        #undef KWARGS_UNPACK
        #undef ARGS_KWARGS_UNPACK

        /////////////////////////////
        ////    Python -> C++    ////
        /////////////////////////////

        /* NOTE: these wrappers will always use the vectorcall protocol where possible,
         * which is the fastest calling convention available in CPython.  This requires
         * us to parse or allocate an array of PyObject* pointers with a binary layout
         * that looks something like this:
         *
         *                          { kwnames tuple }
         *      -------------------------------------
         *      | x | p | p | p |...| k | k | k |...|
         *      -------------------------------------
         *            ^             ^
         *            |             nargs ends here
         *            *args starts here
         *
         * Where 'x' is an optional first element that can be temporarily written to
         * in order to efficiently forward the `self` argument for bound methods, etc.
         * The presence of this argument is determined by the
         * PY_VECTORCALL_ARGUMENTS_OFFSET flag, which is encoded in nargs.  You can
         * check for its presence by bitwise AND-ing against nargs, and the true
         * number of arguments must be extracted using `PyVectorcall_NARGS(nargs)`
         * to account for this.
         *
         * If PY_VECTORCALL_ARGUMENTS_OFFSET is set and 'x' is written to, then it must
         * always be reset to its original value before the function returns.  This
         * allows for nested forwarding/scoping using the same argument list, with no
         * extra allocations.
         */

        template <size_t I>
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (I < nargs) {
                return reinterpret_borrow<Object>(array[I]);
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required positional-only argument at index " +
                    std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::kw)
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (I < nargs) {
                return reinterpret_borrow<Object>(array[I]);
            } else if (kwnames != nullptr) {
                for (size_t i = 0; i < kwcount; ++i) {
                    const char* name = PyUnicode_AsUTF8(
                        PyTuple_GET_ITEM(kwnames, i)
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(name, Inspect<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[nargs + i]);
                    }
                }
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required argument '" + std::string(T<I>::name) +
                    "' at index " + std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::kw_only)
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (kwnames != nullptr) {
                for (size_t i = 0; i < kwcount; ++i) {
                    const char* name = PyUnicode_AsUTF8(
                        PyTuple_GET_ITEM(kwnames, i)
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(name, Inspect<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[i]);
                    }
                }
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required keyword-only argument '" +
                    std::string(T<I>::name) + "'"
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::args)
        static auto from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::vector<typename Inspect<T<I>>::type> vec;
            for (size_t i = I; i < nargs; ++i) {
                vec.push_back(reinterpret_borrow<Object>(array[i]));
            }
            return vec;
        }

        template <size_t I> requires (Inspect<T<I>>::kwargs)
        static auto from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::unordered_map<std::string, typename Inspect<T<I>>::type> map;
            if (kwnames != nullptr) {
                auto sequence = std::make_index_sequence<target::kw_count>{};
                for (size_t i = 0; i < kwcount; ++i) {
                    Py_ssize_t length;
                    const char* name = PyUnicode_AsUTF8AndSize(
                        PyTuple_GET_ITEM(kwnames, i),
                        &length
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (!target::has_keyword(sequence)) {
                        map.emplace(
                            std::string(name, length),
                            reinterpret_borrow<Object>(array[nargs + i])
                        );
                    }
                }
            }
            return map;
        }

        /////////////////////////////
        ////    C++ -> Python    ////
        /////////////////////////////

        template <size_t J>
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            try {
                array[J + 1] = as_object(
                    get_arg<J>(std::forward<Source>(args)...)
                ).release().ptr();
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::kw)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            try {
                PyTuple_SET_ITEM(
                    kwnames,
                    J - source::kw_index,
                    Py_NewRef(impl::TemplateString<Inspect<S<J>>::name>::ptr)
                );
                array[J + 1] = as_object(
                    get_arg<J>(std::forward<Source>(args)...)
                ).release().ptr();
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::args)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_args = get_arg<J>(std::forward<Source>(args)...);
                for (const auto& value : var_args) {
                    array[curr] = as_object(value).release().ptr();
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::kwargs)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_kwargs = get_arg<J>(std::forward<Source>(args)...);
                for (const auto& [key, value] : var_kwargs) {
                    PyObject* name = PyUnicode_FromStringAndSize(
                        key.data(),
                        key.size()
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(kwnames, curr - source::kw_index, name);
                    array[curr] = as_object(value).release().ptr();
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

    };

    template <typename Iter, std::sentinel_for<Iter> End>
    static void validate_args(Iter& iter, const End& end) {
        if (iter != end) {
            std::string message =
                "too many arguments in positional parameter pack: ['" + repr(*iter);
            while (++iter != end) {
                message += "', '";
                message += repr(*iter);
            }
            message += "']";
            throw TypeError(message);
        }
    }

    template <typename source, size_t... Is, typename Kwargs>
    static void validate_kwargs(std::index_sequence<Is...>, const Kwargs& kwargs) {
        std::vector<std::string> extra;
        for (const auto& [key, value] : kwargs) {
            bool is_empty = key == "";
            bool match = (
                (key == Inspect<typename target::template type<Is>>::name) || ...
            );
            if (is_empty || !match) {
                extra.push_back(key);
            }
        }
        if (!extra.empty()) {
            auto iter = extra.begin();
            auto end = extra.end();
            std::string message = "unexpected keyword arguments: ['" + repr(*iter);
            while (++iter != end) {
                message += "', '";
                message += repr(*iter);
            }
            message += "']";
            throw TypeError(message);
        }
    }

    /* A heap-allocated data structure that allows a C++ function to be efficiently
    passed between Python and C++ without losing any of its original properties.  A
    shared reference to this object is stored in both the `py::Function` instance and a
    special `PyCapsule` that is passed up to the PyCFunction wrapper.  It will be kept
    alive as long as either of these references are in scope, and allows additional
    references to be passed along whenever a `py::Function` is copied or moved,
    mirroring the reference count of the underlying PyObject*.

    The PyCapsule is annotated with the mangled function type, which includes the
    signature of the underlying C++ function.  By matching against this identifier,
    Bertrand can determine whether an arbitrary Python function is:
        1.  Backed by a py::Function object, in which case it will extract the
            underlying PyCapsule, and
        2.  Whether the receiving function exactly matches the signature of the
            original py::Function.

    If both of these conditions are met, Bertrand will unpack the C++ Capsule and take
    a new reference to it, extending its lifetime.  This avoids creating an additional
    wrapper around the Python function, and allows the function to be passed
    arbitrarily across the language boundary without introducing any overhead.

    If condition (1) is met but (2) is not, then a TypeError is raised that contains
    the mismatched signatures, which are demangled for clarity. */
    class Capsule {

        static std::string demangle(const char* name) {
            #if defined(__GNUC__) || defined(__clang__)
                int status = 0;
                std::unique_ptr<char, void(*)(void*)> res {
                    abi::__cxa_demangle(
                        name,
                        nullptr,
                        nullptr,
                        &status
                    ),
                    std::free
                };
                return (status == 0) ? res.get() : name;
            #elif defined(_MSC_VER)
                char undecorated_name[1024];
                if (UnDecorateSymbolName(
                    name,
                    undecorated_name,
                    sizeof(undecorated_name),
                    UNDNAME_COMPLETE
                )) {
                    return std::string(undecorated_name);
                } else {
                    return name;
                }
            #else
                return name; // fallback: no demangling
            #endif
        }

    public:
        static constexpr StaticStr capsule_name = "bertrand";
        static const char* capsule_id;
        std::string name;
        std::string docstring;
        std::function<Return(Target...)> func;
        DefaultValues defaults;
        PyMethodDef method_def;

        /* Construct a Capsule around a C++ function with the given name and default
        values. */
        Capsule(
            std::string&& func_name,
            std::string&& func_doc,
            std::function<Return(Target...)>&& func,
            DefaultValues&& defaults
        ) : name(std::move(func_name)),
            docstring(std::move(func_doc)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            method_def(
                name.c_str(),
                (PyCFunction) &Wrap<call_policy>::python,
                Wrap<call_policy>::flags,
                docstring.c_str()
            )
        {}

        /* This proxy is what's actually stored in the PyCapsule, so that it uses the
        same shared_ptr to the C++ Capsule at the Python level. */
        struct Reference {
            std::shared_ptr<Capsule> ptr;
        };

        /* Extract the Capsule from a Bertrand-enabled Python function or create a new
        one to represent a Python function at the C++ level. */
        static std::shared_ptr<Capsule> from_python(PyObject* func) {
            if (PyCFunction_Check(func)) {
                PyObject* self = PyCFunction_GET_SELF(func);
                if (PyCapsule_IsValid(self, capsule_name)) {
                    const char* id = (const char*)PyCapsule_GetContext(self);
                    if (id == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(id, capsule_id) == 0) {
                        auto result = reinterpret_cast<Reference*>(
                            PyCapsule_GetPointer(self, capsule_name)
                        );
                        return result->ptr;  // shared_ptr copy

                    } else {
                        std::string message = "Incompatible function signatures:";
                        message += "\n    Expected: " + demangle(capsule_id);
                        message += "\n    Received: " + demangle(id);
                        throw TypeError(message);
                    }
                }
            }

            return nullptr;
        }

        /* PyCapsule deleter that releases the shared_ptr reference held by the Python
        function when it is garbage collected. */
        static void deleter(PyObject* capsule) {
            auto contents = reinterpret_cast<Reference*>(
                PyCapsule_GetPointer(capsule, capsule_name)
            );
            delete contents;
        }

        /* Get the C++ Capsule from the PyCapsule object that's passed as the `self`
        argument to the PyCFunction wrapper. */
        static Capsule* get(PyObject* capsule) {
            auto result = reinterpret_cast<Reference*>(
                PyCapsule_GetPointer(capsule, capsule_name)
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return result->ptr.get();
        }

        /* Build a PyCFunction wrapper around the C++ function object.  Uses a
        PyCapsule to properly manage memory and ferry the C++ function into Python. */
        static PyObject* to_python(std::shared_ptr<Capsule> contents) {
            Reference* ref = new Reference{contents};
            PyObject* py_capsule = PyCapsule_New(
                ref,
                capsule_name,
                &deleter
            );
            if (py_capsule == nullptr) {
                delete ref;
                Exception::from_python();
            }

            if (PyCapsule_SetContext(py_capsule, (void*)capsule_id)) {
                Py_DECREF(py_capsule);
                Exception::from_python();
            }

            PyObject* result = PyCFunction_New(&contents->method_def, py_capsule);
            Py_DECREF(py_capsule);  // PyCFunction now owns the only reference
            if (result == nullptr) {
                Exception::from_python();
            }
            return result;
        }

        // TODO: try to lift a bunch of behavior out of this class to shorten error
        // messages.

        /* Choose an optimized Python call protocol based on the target signature. */
        static constexpr impl::CallPolicy call_policy = [] {
            if constexpr (target::size == 0) {
                return impl::CallPolicy::no_args;
            } else if constexpr (
                target::size == 1 && Inspect<typename target::template type<0>>::pos
            ) {
                return impl::CallPolicy::one_arg;
            } else if constexpr (!target::has_kw && !target::has_kwargs) {
                return impl::CallPolicy::positional;
            } else {
                return impl::CallPolicy::keyword;
            }
        }();

        template <impl::CallPolicy policy, typename Dummy = void>
        struct Wrap;

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::no_args, Dummy> {
            static constexpr int flags = METH_NOARGS;

            static PyObject* python(PyObject* capsule, PyObject* /* unused */) {
                try {
                    if constexpr (std::is_void_v<Return>) {
                        get(capsule)->func();
                        Py_RETURN_NONE;
                    } else {
                        return as_object(get(capsule)->func()).release().ptr();
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::one_arg, Dummy> {
            static constexpr int flags = METH_O;

            static PyObject* python(PyObject* capsule, PyObject* obj) {
                try {
                    Capsule* contents = get(capsule);
                    if constexpr (std::is_void_v<Return>) {
                        if (obj == nullptr) {
                            if constexpr (target::has_opt) {
                                contents->func(contents->defaults.template get<0>());
                            } else {
                                throw TypeError("missing required argument");
                            }
                        } else {
                            contents->func(reinterpret_borrow<Object>(obj));
                        }
                        Py_RETURN_NONE;
                    } else {
                        if (obj == nullptr) {
                            if constexpr (target::has_opt) {
                                return as_object(contents->func(
                                    contents->defaults.template get<0>()
                                )).release().ptr();
                            } else {
                                throw TypeError("missing required argument");
                            }
                        }
                        return as_object(contents->func(
                            reinterpret_borrow<Object>(obj)
                        )).release().ptr();
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::positional, Dummy> {
            static constexpr int flags = METH_FASTCALL;

            static PyObject* python(
                PyObject* capsule,
                PyObject* const* args,
                Py_ssize_t nargs
            ) {
                try {
                    return []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject* capsule,
                        PyObject* const* args,
                        Py_ssize_t nargs
                    ) {
                        Py_ssize_t true_nargs = PyVectorcall_NARGS(nargs);
                        if constexpr (!target::has_args) {
                            if (true_nargs > static_cast<Py_ssize_t>(target::size)) {
                                throw TypeError(
                                    "expected at most " + std::to_string(target::size) +
                                    " positional arguments, but received " +
                                    std::to_string(true_nargs)
                                );    
                            }
                        }
                        Capsule* contents = get(capsule);
                        if constexpr (std::is_void_v<Return>) {
                            contents->func(
                                Arguments<Target...>::template from_python<Is>(
                                    contents->defaults,
                                    args,
                                    true_nargs,
                                    nullptr,
                                    0
                                )...
                            );
                            Py_RETURN_NONE;
                        } else {
                            return as_object(
                                contents->func(
                                    Arguments<Target...>::template from_python<Is>(
                                        contents->defaults,
                                        args,
                                        true_nargs,
                                        nullptr,
                                        0
                                    )...
                                )
                            ).release().ptr();
                        }
                    }(
                        std::make_index_sequence<target::size>{},
                        capsule,
                        args,
                        nargs
                    );
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::keyword, Dummy> {
            static constexpr int flags = METH_FASTCALL | METH_KEYWORDS;

            static PyObject* python(
                PyObject* capsule,
                PyObject* const* args,
                Py_ssize_t nargs,
                PyObject* kwnames
            ) {
                try {
                    return []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject* capsule,
                        PyObject* const* args,
                        Py_ssize_t nargs,
                        PyObject* kwnames
                    ) {
                        size_t true_nargs = PyVectorcall_NARGS(nargs);
                        if constexpr (!target::has_args) {
                            if (true_nargs > target::size) {
                                throw TypeError(
                                    "expected at most " + std::to_string(target::size) +
                                    " positional arguments, but received " +
                                    std::to_string(true_nargs)
                                );
                            }
                        }
                        size_t kwcount = 0;
                        if (kwnames != nullptr) {
                            kwcount = PyTuple_GET_SIZE(kwnames);
                            if constexpr (!target::has_kwargs) {
                                for (size_t i = 0; i < kwcount; ++i) {
                                    Py_ssize_t length;
                                    const char* name = PyUnicode_AsUTF8AndSize(
                                        PyTuple_GET_ITEM(kwnames, i),
                                        &length
                                    );
                                    if (name == nullptr) {
                                        Exception::from_python();
                                    } else if (!target::has_keyword(name)) {
                                        throw TypeError(
                                            "unexpected keyword argument '" +
                                            std::string(name, length) + "'"
                                        );
                                    }
                                }
                            }
                        }
                        Capsule* contents = get(capsule);
                        if constexpr (std::is_void_v<Return>) {
                            contents->func(
                                Arguments<Target...>::template from_python<Is>(
                                    contents->defaults,
                                    args,
                                    true_nargs,
                                    kwnames,
                                    kwcount
                                )...
                            );
                            Py_RETURN_NONE;
                        } else {
                            return as_object(
                                contents->func(
                                    Arguments<Target...>::template from_python<Is>(
                                        contents->defaults,
                                        args,
                                        true_nargs,
                                        kwnames,
                                        kwcount
                                    )...
                                )
                            ).release().ptr();
                        }
                    }(
                        std::make_index_sequence<target::size>{},
                        capsule,
                        args,
                        nargs,
                        kwnames
                    );
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

    };

    std::shared_ptr<Capsule> contents;

    PyObject* unwrap_method() const {
        PyObject* result = this->ptr();
        if (PyMethod_Check(result)) {
            result = PyMethod_GET_FUNCTION(result);
        }
        return result;
    }

public:
    using ReturnType = Return;
    using ArgTypes = std::tuple<typename Inspect<Target>::type...>;
    using Annotations = std::tuple<Target...>;
    using Defaults = DefaultValues;

    /* Instantiate a new function type with the same arguments but a different return
    type. */
    template <typename R>
    using set_return = Function<R(Target...)>;

    /* Instantiate a new function type with the same return type but different
    argument annotations. */
    template <typename... Ts>
    using set_args = Function<Return(Ts...)>;

    /* Template constraint that evaluates to true if this function can be called with
    the templated argument types. */
    template <typename... Source>
    static constexpr bool invocable = Arguments<Source...>::enable;

    /* The total number of arguments that the function accepts, not including variadic
    positional or keyword arguments. */
    static constexpr size_t argcount =
        target::size - target::has_args - target::has_kwargs;

    /* The total number of optional arguments that the function accepts, not including
    variadic positional or keyword arguments. */
    static constexpr size_t opt_argcount = target::opt_count;

    /* The number of positional-only arguments that the function accepts, not including
    variadic positional arguments. */
    static constexpr size_t posonly_argcount = target::pos_count;

    /* The number of optional positional-only arguments that the function accepts, not
    including variadic positional arguments. */
    static constexpr size_t posonly_opt_argcount = target::pos_opt_count;

    /* The number of keyword-only arguments that the function accepts, not including
    variadic keyword arguments. */
    static constexpr size_t kwonly_argcount = target::kw_only_count;

    /* The number of optional keyword-only arguments that the function accepts, not
    including variadic keyword arguments. */
    static constexpr size_t kwonly_opt_argcount = target::kw_only_opt_count;

    /* Indicates whether the function accepts variadic positional arguments. */
    static constexpr bool has_args = target::has_args;

    /* Indicates whether the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = target::has_kwargs;

    /* Get the default value at index I of the target signature. */
    template <size_t I> requires (target::template is_opt<I>)
    auto& default_value() {
        if (contents == nullptr) {
            throw TypeError();  // TODO: search for the default value in the Python object?
        }
        return contents->defaults.template get<I>();
    }

    /* Get the default value at index I of the target signature. */
    template <size_t I> requires (target::template is_opt<I>)
    const auto& default_value() const {
        if (contents == nullptr) {
            throw TypeError();  // TODO: search for the default value in the Python object?
        }
        return contents->defaults.template get<I>();
    }

    /* Get the default value for the named argument. */
    template <StaticStr name> requires (target::template is_opt_kw<name>)
    auto& default_value() {
        if (contents == nullptr) {
            throw TypeError();  // TODO: search for the default value in the Python object?
        }
        return contents->defaults.template get<target::template index<name>>();
    }

    /* Get the default value for the named argument. */
    template <StaticStr name> requires (target::template is_opt_kw<name>)
    const auto& default_value() const {
        if (contents == nullptr) {
            throw TypeError();  // TODO: search for the default value in the Python object?
        }
        return contents->defaults.template get<target::template index<name>>();
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors.  Passes a shared_ptr reference to the capsule in tandem
    with the PyObject* pointer. */
    Function(const Function& other) : Base(other), contents(other.contents) {}
    Function(Function&& other) : Base(std::move(other)), contents(std::move(other.contents)) {}

    /* Reinterpret_borrow/reinterpret_steal constructors.  Attempts to unpack a
    shared_ptr to the function's capsule, if it has one. */
    Function(Handle h, borrowed_t t) : Base(h, t), contents(Capsule::from_python(h.ptr())) {}
    Function(Handle h, stolen_t t) : Base(h, t), contents(Capsule::from_python(h.ptr())) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Function, __init__<Function, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Function, std::remove_cvref_t<Args>...>::enable
        )
    Function(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Function, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Function, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Function, __explicit_init__<Function, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Function, std::remove_cvref_t<Args>...>::enable
        )
    explicit Function(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Function, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}




    // TODO: update these to use __init__, __explicit_init__




    // /* Convert an equivalent pybind11 type into a py::Function.  Attempts to unpack the
    // argument's capsule if it represents a py::Function instance. */
    // template <impl::pybind11_like T> requires (typecheck<T>())
    // Function(T&& other) : Base(std::forward<T>(other)) {
    //     contents = Capsule::from_python(m_ptr);
    // }

    /* Construct a py::Function from a pybind11 accessor.  Attempts to unpack the
    argument's capsule if it represents a py::Function instance. */
    template <typename Policy>
    Function(const pybind11::detail::accessor<Policy>& accessor) : Base(accessor) {
        contents = Capsule::from_python(m_ptr);
    }

    /* Construct a py::Function from a valid C++ function with the templated signature.
    Use CTAD to deduce the signature if not explicitly provided.  If the signature
    contains default value annotations, they must be specified here. */
    template <typename Func, typename... Values>
        requires (
            !impl::python_like<Func> &&
            std::is_invocable_r_v<Return, Func, Target...> &&
            Defaults::template enable<Values...>
        )
    Function(Func&& func, Values&&... defaults) :
        Base(nullptr, stolen_t{}),
        contents(new Capsule{
            "",
            "",
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        })
    {
        m_ptr = Capsule::to_python(contents);
    }

    /* Construct a py::Function from a valid C++ function with the templated signature.
    Use CTAD to deduce the signature if not explicitly provided.  If the signature
    contains default value annotations, they must be specified here. */
    template <typename Func, typename... Values>
        requires (
            !impl::python_like<Func> &&
            std::is_invocable_r_v<Return, Func, Target...> &&
            Defaults::template enable<Values...>
        )
    Function(std::string name, Func&& func, Values&&... defaults) :
        Base(nullptr, stolen_t{}),
        contents(new Capsule{
            std::move(name),
            "",
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        })
    {
        m_ptr = Capsule::to_python(contents);
    }

    /* Construct a py::Function from a valid C++ function with the templated signature.
    Use CTAD to deduce the signature if not explicitly provided.  If the signature
    contains default value annotations, they must be specified here. */
    template <typename Func, typename... Values>
        requires (
            !impl::python_like<Func> &&
            std::is_invocable_r_v<Return, Func, Target...> &&
            Defaults::template enable<Values...>
        )
    Function(std::string name, std::string doc, Func&& func, Values&&... defaults) :
        Base(nullptr, stolen_t{}),
        contents(new Capsule{
            std::move(name),
            std::move(doc),
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        })
    {
        m_ptr = Capsule::to_python(contents);
    }

    ~Function() noexcept {
        // NOTE: if the function is stored with static duration, then the PyCapsule's
        // deleter will not be called during interpreter shutdown, which leaves a
        // permanent reference to the C++ Capsule.  In this case, we need to manually
        // release the reference so that we avoid memory leaks.
        if (!Py_IsInitialized() && PyCFunction_Check(m_ptr)) {
            PyObject* self = PyCFunction_GET_SELF(m_ptr);
            if (PyCapsule_IsValid(self, Capsule::capsule_name)) {
                Capsule::deleter(self);
            }
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Call an external Python function that matches the target signature using
    Python-style arguments.  The optional `R` template parameter specifies a specific
    Python type to interpret the result of the CPython call.  It will always be
    implicitly converted to the function's final return type.  Setting it equal to the
    return type will avoid any extra overhead from dynamic type checks/implicit
    conversions. */
    template <typename R = Object, typename... Source> requires (invocable<Source...>)
    static Return invoke_py(Handle func, Source&&... args) {
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Handle& func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            PyObject* result;

            // if there are no arguments, we can use an optimized protocol
            if constexpr (source::size == 0) {
                result = PyObject_CallNoArgs(func.ptr());

            } else if constexpr (!source::has_args && !source::has_kwargs) {
                // if there is only one argument, we can use an optimized protocol
                if constexpr (source::size == 1) {
                    if constexpr (source::has_kw) {
                        result = PyObject_CallOneArg(
                            func.ptr(),
                            as_object(
                                get_arg<0>(std::forward<Source>(args)...).value()
                            ).ptr()
                        );
                    } else {
                        result = PyObject_CallOneArg(
                            func.ptr(),
                            as_object(
                                get_arg<0>(std::forward<Source>(args)...)
                            ).ptr()
                        );
                    }

                // if there are no *args, **kwargs, we can stack allocate the argument
                // array with a fixed size
                } else {
                    PyObject* array[source::size + 1];
                    array[0] = nullptr;
                    PyObject* kwnames;
                    if constexpr (source::has_kw) {
                        kwnames = PyTuple_New(source::kw_count);
                    } else {
                        kwnames = nullptr;
                    }
                    (
                        Arguments<Source...>::template to_python<Is>(
                            kwnames,
                            array,  // TODO: 1-indexed
                            std::forward<Source>(args)...
                        ),
                        ...
                    );
                    Py_ssize_t npos = source::size - source::kw_count;
                    result = PyObject_Vectorcall(
                        func.ptr(),
                        array + 1,
                        npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    for (size_t i = 1; i <= source::size; ++i) {
                        Py_XDECREF(array[i]);
                    }
                }

            // otherwise, we have to heap-allocate the array with a variable size
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_args.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames;
                if constexpr (source::has_kw) {
                    kwnames = PyTuple_New(source::kw_count);
                } else {
                    kwnames = nullptr;
                }
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count;
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 2 + var_args.size() + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                size_t npos = source::size - 2 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;
            }

            if (result == nullptr) {
                Exception::from_python();
            }
            if constexpr (std::is_void_v<Return>) {
                Py_DECREF(result);
            } else {
                return reinterpret_steal<R>(result);
            }
        }(
            std::make_index_sequence<sizeof...(Source)>{},
            func,
            std::forward<Source>(args)...
        );
    }

    /* Call an external C++ function that matches the target signature using the
    given defaults and Python-style arguments. */
    template <typename Func, typename... Source>
        requires (std::is_invocable_r_v<Return, Func, Target...> && invocable<Source...>)
    static Return invoke_cpp(const Defaults& defaults, Func&& func, Source&&... args) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Defaults& defaults,
            Func&& func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            if constexpr (!source::has_args && !source::has_kwargs) {
                if constexpr (std::is_void_v<Return>) {
                    func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            std::forward<Source>(args)...
                        )...
                    );
                } else {
                    return func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            std::forward<Source>(args)...
                        )...
                    );
                }

            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_args.size(),
                            iter,
                            end,
                            std::forward<Source>(args)...
                        )...
                    );
                    if constexpr (!target::has_args) {
                        validate_args(iter, end);
                    }
                } else {
                    decltype(auto) result = func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_args.size(),
                            iter,
                            end,
                            std::forward<Source>(args)...
                        )...
                    );
                    if constexpr (!target::has_args) {
                        validate_args(iter, end);
                    }
                    return result;
                }

            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                if constexpr (std::is_void_v<Return>) {
                    func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_kwargs,
                            std::forward<Source>(args)...
                        )...
                    );
                } else {
                    return func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_kwargs,
                            std::forward<Source>(args)...
                        )...
                    );
                }

            } else {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_args.size(),
                            iter,
                            end,
                            var_kwargs,
                            std::forward<Source>(args)...
                        )...
                    );
                    if constexpr (!target::has_args) {
                        validate_args(iter, end);
                    }
                } else {
                    decltype(auto) result = func(
                        Arguments<Source...>::template cpp<Is>(
                            defaults,
                            var_args.size(),
                            iter,
                            end,
                            var_kwargs,
                            std::forward<Source>(args)...
                        )...
                    );
                    if constexpr (!target::has_args) {
                        validate_args(iter, end);
                    }
                    return result;
                }
            }
        }(
            std::make_index_sequence<target::size>{},
            defaults,
            std::forward<Func>(func),
            std::forward<Source>(args)...
        );
    }

    /* Call the function with the given arguments. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        if (contents == nullptr) {
            if constexpr (std::is_void_v<Return>) {
                invoke_py(this->ptr(), std::forward<Source>(args)...);
            } else {
                return invoke_py(this->ptr(), std::forward<Source>(args)...);
            }
        } else {
            if constexpr (std::is_void_v<Return>) {
                invoke_cpp(
                    contents->defaults,
                    contents->func,
                    std::forward<Source>(args)...
                );
            } else {
                return invoke_cpp(
                    contents->defaults,
                    contents->func,
                    std::forward<Source>(args)...
                );
            }
        }
    }

    // TODO: bring down Signature::has_keyword() and provide a compile-time equivalent
    // -> it can be overloaded for StaticStr and marked as consteval.


    // default<I>() returns a reference to the default value of the I-th argument
    // default<name>() returns a reference to the default value of the named argument

    /* Get the name of the wrapped function. */
    [[nodiscard]] std::string name() const {
        if (contents != nullptr) {
            return contents->name;
        }

        PyObject* result = PyObject_GetAttrString(this->ptr(), "__name__");
        if (result == nullptr) {
            Exception::from_python();
        }
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(result, &length);
        Py_DECREF(result);
        if (data == nullptr) {
            Exception::from_python();
        }
        return std::string(data, length);
    }

    /* Get the function's fully qualified (dotted) name. */
    [[nodiscard]] std::string qualname() const {
        PyObject* result = PyObject_GetAttrString(this->ptr(), "__qualname__");
        if (result == nullptr) {
            Exception::from_python();
        }
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(result, &length);
        Py_DECREF(result);
        if (data == nullptr) {
            Exception::from_python();
        }
        return std::string(data, length);
    }

    // TODO: when I construct a capsule, I can potentially use the stacktrace library
    // to get the filename and line number of the function definition.

    /* Get the name of the file in which the function was defined, or nullopt if it
    was defined from C/C++. */
    [[nodiscard]] std::optional<std::string> filename() const;

    /* Get the line number where the function was defined, or nullopt if it was
    defined from C/C++. */
    [[nodiscard]] std::optional<size_t> lineno() const;

    // /* Get a read-only dictionary mapping argument names to their default values. */
    // [[nodiscard]] MappingProxy<Dict<Str, Object>> defaults() const {
    //     Code code = this->code();

    //     // check for positional defaults
    //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
    //     if (pos_defaults == nullptr) {
    //         if (code.kwonlyargcount() > 0) {
    //             Object kwdefaults = attr<"__kwdefaults__">();
    //             if (kwdefaults.is(None)) {
    //                 return MappingProxy(Dict<Str, Object>{});
    //             } else {
    //                 return MappingProxy(reinterpret_steal<Dict<Str, Object>>(
    //                     kwdefaults.release())
    //                 );
    //             }
    //         } else {
    //             return MappingProxy(Dict<Str, Object>{});
    //         }
    //     }

    //     // extract positional defaults
    //     size_t argcount = code.argcount();
    //     Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
    //     Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
    //     Dict result;
    //     for (size_t i = 0; i < defaults.size(); ++i) {
    //         result[names[i]] = defaults[i];
    //     }

    //     // merge keyword-only defaults
    //     if (code.kwonlyargcount() > 0) {
    //         Object kwdefaults = attr<"__kwdefaults__">();
    //         if (!kwdefaults.is(None)) {
    //             result.update(Dict(kwdefaults));
    //         }
    //     }
    //     return result;
    // }

    // /* Set the default value for one or more arguments.  If nullopt is provided,
    // then all defaults will be cleared. */
    // void defaults(Dict&& dict) {
    //     Code code = this->code();

    //     // TODO: clean this up.  The logic should go as follows:
    //     // 1. check for positional defaults.  If found, build a dictionary with the new
    //     // values and remove them from the input dict.
    //     // 2. check for keyword-only defaults.  If found, build a dictionary with the
    //     // new values and remove them from the input dict.
    //     // 3. if any keys are left over, raise an error and do not update the signature
    //     // 4. set defaults to Tuple(positional_defaults.values()) and update kwdefaults
    //     // in-place.

    //     // account for positional defaults
    //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
    //     if (pos_defaults != nullptr) {
    //         size_t argcount = code.argcount();
    //         Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
    //         Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
    //         Dict positional_defaults;
    //         for (size_t i = 0; i < defaults.size(); ++i) {
    //             positional_defaults[*names[i]] = *defaults[i];
    //         }

    //         // merge new defaults with old ones
    //         for (const Object& key : positional_defaults) {
    //             if (dict.contains(key)) {
    //                 positional_defaults[key] = dict.pop(key);
    //             }
    //         }
    //     }

    //     // check for keyword-only defaults
    //     if (code.kwonlyargcount() > 0) {
    //         Object kwdefaults = attr<"__kwdefaults__">();
    //         if (!kwdefaults.is(None)) {
    //             Dict temp = {};
    //             for (const Object& key : kwdefaults) {
    //                 if (dict.contains(key)) {
    //                     temp[key] = dict.pop(key);
    //                 }
    //             }
    //             if (dict) {
    //                 throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //             }
    //             kwdefaults |= temp;
    //         } else if (dict) {
    //             throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //         }
    //     } else if (dict) {
    //         throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //     }

    //     // TODO: set defaults to Tuple(positional_defaults.values()) and update kwdefaults

    // }

    // /* Get a read-only dictionary holding type annotations for the function. */
    // [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const {
    //     PyObject* result = PyFunction_GetAnnotations(self());
    //     if (result == nullptr) {
    //         return MappingProxy(Dict<Str, Object>{});
    //     }
    //     return reinterpret_borrow<MappingProxy<Dict<Str, Object>>>(result);
    // }

    // /* Set the type annotations for the function.  If nullopt is provided, then the
    // current annotations will be cleared.  Otherwise, the values in the dictionary will
    // be used to update the current values in-place. */
    // void annotations(std::optional<Dict> annotations) {
    //     if (!annotations.has_value()) {  // clear all annotations
    //         if (PyFunction_SetAnnotations(self(), Py_None)) {
    //             Exception::from_python();
    //         }

    //     } else if (!annotations.value()) {  // do nothing
    //         return;

    //     } else {  // update annotations in-place
    //         Code code = this->code();
    //         Tuple<Str> args = code.varnames()[{0, code.argcount() + code.kwonlyargcount()}];
    //         MappingProxy existing = this->annotations();

    //         // build new dict
    //         Dict result = {};
    //         for (const Object& arg : args) {
    //             if (annotations.value().contains(arg)) {
    //                 result[arg] = annotations.value().pop(arg);
    //             } else if (existing.contains(arg)) {
    //                 result[arg] = existing[arg];
    //             }
    //         }

    //         // account for return annotation
    //         static const Str s_return = "return";
    //         if (annotations.value().contains(s_return)) {
    //             result[s_return] = annotations.value().pop(s_return);
    //         } else if (existing.contains(s_return)) {
    //             result[s_return] = existing[s_return];
    //         }

    //         // check for unmatched keys
    //         if (annotations.value()) {
    //             throw ValueError(
    //                 "no match for arguments " +
    //                 Str(List(annotations.value().keys()))
    //             );
    //         }

    //         // push changes
    //         if (PyFunction_SetAnnotations(self(), result.ptr())) {
    //             Exception::from_python();
    //         }
    //     }
    // }

    /////////////////////////////////////
    ////    PyFunctionObject* API    ////
    /////////////////////////////////////

    /* NOTE: these methods rely on properties of a PyFunctionObject* which are not
     * available for built-in function types, or functions exposed from C/C++.  They
     * will throw a TypeError if used on such objects.
     */

    /* Get the module in which this function was defined, or nullopt if it is not
    associated with any module. */
    [[nodiscard]] std::optional<Module> module_() const;

    /* Get the Python code object that backs this function, or nullopt if the function
    was not defined from Python. */
    [[nodiscard]] std::optional<Code> code() const;

    /* Get the globals dictionary from the function object, or nullopt if it could not
    be introspected. */
    [[nodiscard]] std::optional<Dict<Str, Object>> globals() const;

    /* Get a read-only mapping of argument names to their default values. */
    [[nodiscard]] MappingProxy<Dict<Str, Object>> defaults() const;

    /* Set the default value for one or more arguments. */
    template <typename... Values> requires (sizeof...(Values) > 0)  // and all are valid
    void defaults(Values&&... values);

    /* Get a read-only mapping of argument names to their type annotations. */
    [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const;

    /* Set the type annotation for one or more arguments. */
    template <typename... Annotations> requires (sizeof...(Annotations) > 0)  // and all are valid
    void annotations(Annotations&&... annotations);

    /* Get the closure associated with the function.  This is a Tuple of cell objects
    encapsulating data to be used by the function body. */
    [[nodiscard]] std::optional<Tuple<Object>> closure() const;

    /* Set the closure associated with the function.  If nullopt is given, then the
    closure will be deleted. */
    void closure(std::optional<Tuple<Object>> closure);





    // TODO: if only it were this simple.  Because I'm using a PyCapsule to ensure the
    // lifecycle of the function object, the only way to get everything to work
    // correctly is to write a wrapper class that forwards the `self` argument to a
    // nested py::Function as the first argument.  That class would also need to be
    // exposed to Python, and would have to work the same way.  It would probably
    // also need to be a descriptor, so that it can be correctly attached to the
    // class as an instance method.  This ends up looking a lot like my current
    // @attachable decorator, but lower level and with more boilerplate.  I would also
    // only be able to expose the (*args, **kwargs) version, since templates can't be
    // transmitted up to Python.

    // enum class Descr {
    //     METHOD,
    //     CLASSMETHOD,
    //     STATICMETHOD,
    //     PROPERTY
    // };

    // TODO: rather than using an enum, just make separate C++ types and template
    // attach accordingly.

    // function.attach<py::Method>(type);
    // function.attach<py::Method>(type, "foo");

    // /* Attach a descriptor with the same name as this function to the type, which
    // forwards to this function when accessed. */
    // void attach(Type& type, Descr policy = Descr::METHOD) {
    //     PyObject* descriptor = PyDescr_NewMethod(type.ptr(), contents->method_def);
    //     if (descriptor == nullptr) {
    //         Exception::from_python();
    //     }
    //     int rc = PyObject_SetAttrString(type.ptr(), contents->name.data(), descriptor);
    //     Py_DECREF(descriptor);
    //     if (rc) {
    //         Exception::from_python();
    //     }
    // };

    // /* Attach a descriptor with a custom name to the type, which forwards to this
    // function when accessed. */
    // void attach(Type& type, std::string name, Descr policy = Descr::METHOD) {
    //     // TODO: same as above.
    // };

    // TODO: Perhaps `py::DynamicFunction` is a separate class after all, and only
    // represents a PyFunctionObject.

    // TODO: What if DynamicFunc was implemented such that adding or removing an
    // argument at the C++ level yielded a new `py::Function` object with the updated
    // signature?  This would enable me to enforce the argument validation at compile
    // time, and register new arguments with equivalent semantics.

    // TODO: syntax would be something along the lines of:

    // py::Function updated = func.arg(x = []{}, y = []{}, ...))

    // -> I would probably need to allocate a new Capsule for each call, since the
    // underlying std::function<> would need to be updated to reflect the new signature.
    // Otherwise, the argument lists wouldn't match.  That might be able to just
    // delegate to the existing Capsule (which would necessitate another shared_ptr
    // reference), which would allow for kwargs-based forwarding, just like Python.

    // -> This would come with the extra overhead of creating a new Python function
    // every time a function's signature is changed, since it might reference the old
    // function when it is called from Python.

    // -> It's probably best to make this is a subclass that just stores a reference
    // to an existing Capsule* and then generates new wrappers that splice in values
    // dynamically from a map.  That way I don't alter the original function at all.
    // Then, these custom DynamicFuncs could be passed around just like other functions
    // and converting them to a py::Function will recover the original function.  That
    // also makes the implementation look closer to how it currently works on the
    // Python side.

};


template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(F, D...) -> Function<typename impl::GetSignature<std::decay_t<F>>::type>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;
template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, std::string, F, D...) -> Function<
    typename impl::GetSignature<std::decay_t<F>>::type
>;


template <typename R, typename... T>
inline const char* Function<R(T...)>::Capsule::capsule_id = typeid(Function).name();


// TODO: if default specialization is given, type checks should be fully generic, right?
// issubclass<T, Function>() should check impl::is_callable_any<T>;

template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>>                 : Returns<bool> {
    static consteval bool operator()() {
        return std::is_invocable_r_v<R, T, A...>;
    }
    static consteval bool operator()(const T&) {
        return operator()();
    }
};


template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>>                  : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if (impl::cpp_like<T>) {
            return issubclass<T, Function<R(A...)>>();
        } else if constexpr (issubclass<T, Function<R(A...)>>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && (
                PyFunction_Check(obj.ptr()) ||
                PyMethod_Check(obj.ptr()) ||
                PyCFunction_Check(obj.ptr())
            );
        } else {
            return false;
        }
    }
};



namespace impl {

    /* A concept meant to be used in conjunction with impl::call_method or
    impl::call_static which enstures that the arguments given at the call site match
    the definition found in __getattr__. */
    template <typename Self, StaticStr Name, typename... Args>
    concept invocable =
        __getattr__<std::decay_t<Self>, Name>::enable &&
        std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
        __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>;

    /* A convenience function that calls a named method of a Python object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <StaticStr name, typename Self, typename... Args>
        requires (invocable<std::decay_t<Self>, name, Args...>)
    inline decltype(auto) call_method(Self&& self, Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, name>::type;
        auto meth = reinterpret_steal<pybind11::object>(
            PyObject_GetAttr(self.ptr(), TemplateString<name>::ptr)
        );
        if (meth.ptr() == nullptr) {
            Exception::from_python();
        }
        try {
            return Func::template invoke_py<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /* A convenience function that calls a named method of a Python type object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <typename Self, StaticStr name, typename... Args>
        requires (invocable<std::decay_t<Self>, name, Args...>)
    inline decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, name>::type;
        auto meth = reinterpret_steal<pybind11::object>(
            PyObject_GetAttr(Self::type.ptr(), TemplateString<name>::ptr)
        );
        if (meth.ptr() == nullptr) {
            Exception::from_python();
        }
        try {
            return Func::template invoke_py<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /* A convenience macro that is meant to be invoked in the body of a py::Object
    subclass, which generates a thin wrapper that forwards to the named method using
    the argument conventions defined in its __getattr__ specialization. */
    #define BERTRAND_METHOD(attributes, name, qualifiers)                               \
        template <typename... Args> requires (                                          \
            ::bertrand::py::impl::invocable<Self, BERTRAND_STRINGIFY(name), Args...>    \
        )                                                                               \
        attributes decltype(auto) name(Args&&... args) qualifiers {                     \
            return ::bertrand::py::impl::call_method<BERTRAND_STRINGIFY(name)>(         \
                *this,                                                                  \
                std::forward<Args>(args)...                                             \
            );                                                                          \
        }

    /* A convenience macro that is meant to be invoked in the body of a py::Object
    subclass, which generates a thin wrapper that forwards to the named class or
    static method using the argument conventions defined in its __getattr__
    specialization. */
    #define BERTRAND_STATIC_METHOD(attributes, name)                                    \
        template <typename... Args> requires (                                          \
            ::bertrand::py::impl::invocable<Self, BERTRAND_STRINGIFY(name), Args...>    \
        )                                                                               \
        attributes static decltype(auto) name(Args&&... args) {                         \
            return ::bertrand::py::impl::call_static<Self, BERTRAND_STRINGIFY(name)>(   \
                std::forward<Args>(args)...                                             \
            );                                                                          \
        }

}


}  // namespace py
}  // namespace bertrand


#endif // BERTRAND_PYTHON_COMMON_FUNC_H
