#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"


namespace py {


/////////////////////////
////    ARGUMENTS    ////
/////////////////////////


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function.  Uses aggregate initialization to extend the lifetime of
temporaries. */
template <StaticStr Name, typename T>
struct Arg : impl::ArgTag {
private:

    template <bool positional, bool keyword>
    struct Optional : impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = positional;
        static constexpr bool is_kw = keyword;
        static constexpr bool is_opt = true;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    template <bool optional>
    struct Positional : impl::ArgTag {
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = true;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = optional;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    template <bool optional>
    struct Keyword : impl::ArgTag {
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = true;
        static constexpr bool is_opt = optional;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = false;

        T value;
    };

    struct Args : impl::ArgTag {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = true;
        static constexpr bool is_kwargs = false;

        std::vector<type> value;
    };

    struct Kwargs : impl::ArgTag {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr bool is_pos = false;
        static constexpr bool is_kw = false;
        static constexpr bool is_opt = false;
        static constexpr bool is_args = false;
        static constexpr bool is_kwargs = true;

        std::unordered_map<std::string, T> value;
    };

public:
    static_assert(Name != "", "Argument must have a name.");

    using type = T;
    using pos = Positional<false>;
    using kw = Keyword<false>;
    using opt = Optional<true, true>;
    using args = Args;
    using kwargs = Kwargs;

    static constexpr StaticStr name = Name;
    static constexpr bool is_pos = true;
    static constexpr bool is_kw = true;
    static constexpr bool is_opt = false;
    static constexpr bool is_args = false;
    static constexpr bool is_kwargs = false;

    T value;
};


namespace impl {

    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr auto operator=(T&& value) const {
            return Arg<name, T>{std::forward<T>(value)};  // aggregate initialization
        }
    };

    /* Represents a keyword parameter pack obtained by double-dereferencing a Python
    object. */
    template <std::derived_from<Object> T> requires (impl::mapping_like<T>)
    struct KwargPack {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;

        T value;

    private:

        static constexpr bool can_iterate =
            impl::yields_pairs_with<T, key_type, mapped_type> ||
            impl::has_items<T> ||
            (impl::has_keys<T> && impl::has_values<T>) ||
            (impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>) ||
            (impl::has_keys<T> && impl::lookup_yields<T, mapped_type, key_type>);

        auto transform() const {
            if constexpr (impl::yields_pairs_with<T, key_type, mapped_type>) {
                return value;

            } else if constexpr (impl::has_items<T>) {
                return value.items();

            } else if constexpr (impl::has_keys<T> && impl::has_values<T>) {
                return std::ranges::views::zip(value.keys(), value.values());

            } else if constexpr (
                impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>
            ) {
                return std::ranges::views::transform(
                    value,
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );

            } else {
                return std::ranges::views::transform(
                    value.keys(),
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );
            }
        }

    public:

        template <typename U = T> requires (can_iterate)
        auto begin() const { return std::ranges::begin(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cbegin() const { return begin(); }
        template <typename U = T> requires (can_iterate)
        auto end() const { return std::ranges::end(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cend() const { return end(); }

    };

    /* Represents a positional parameter pack obtained by dereferencing a Python
    object. */
    template <std::derived_from<Object> T> requires (impl::iterable<T>)
    struct ArgPack {
        T value;

        auto begin() const { return std::ranges::begin(value); }
        auto cbegin() const { return begin(); }
        auto end() const { return std::ranges::end(value); }
        auto cend() const { return end(); }

        template <typename U = T> requires (impl::mapping_like<U>)
        auto operator*() const {
            return KwargPack<U>{value};
        }        
    };

    /// TODO: perhaps the solution is to write the implementation, but leave the
    /// export script as a forward declaration until types have been fully defined.

    /* A base Python type that is subclassed by all `py::Function` specializations in
    order to model various template signatures at the Python level.  This corresponds
    to the publicly-visible `bertrand.Function` type. */
    struct PyFunctionBase {
        PyObject_HEAD
        vectorcallfunc vectorcall;
        std::string name;
        std::string docstring;

        static std::unordered_map<py::Tuple<py::Object>, py::Type<py::Object>> instances;
        static PyObject* __class_getitem__(PyObject* cls, PyObject* spec);

        static PyObject* __repr__(PyFunctionBase* self) {
            std::string s =
                "<py::Function<...> at " +
                std::to_string(reinterpret_cast<size_t>(self)) +
                ">";
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

        // TODO: getset accessors for type annotations, default values, etc.  These
        // should be implemented as closely as possible to the Python data model.

        static PyTypeObject type;

        inline static PyMethodDef methods[] = {
            {
                    "__class_getitem__",
                    (PyCFunction) &__class_getitem__,
                    METH_O,
R"doc(Navigate the C++ template hierarchy from Python.

Parameters
----------
*args : Any
    The template signature to resolve.

Returns
-------
type
    The unique Python type that corresponds to the given template signature.

Raises
------
TypeError
    If the template signature does not correspond to an existing instantiation at the
    C++ level.

Notes
-----
This method is essentially equivalent to a dictionary search against a table that is
determined at compile time.  Since Python cannot directly instantiate C++ templates
itself, this is the closest approximation that can be achieved.)doc"
                },
            {nullptr, nullptr, 0, nullptr}
        };

    };

    inline PyTypeObject PyFunctionBase::type = {
        .ob_base = PyVarObject_HEAD_INIT(nullptr, 0)
        .tp_name = "bertrand.Function",
        .tp_basicsize = sizeof(PyFunctionBase),
        .tp_dealloc = (destructor) &PyFunctionBase::__dealloc__,
        .tp_repr = (reprfunc) &PyFunctionBase::__repr__,
        .tp_call = (ternaryfunc) &PyVectorcall_Call,
        .tp_str = (reprfunc) &PyFunctionBase::__repr__,
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE |
            Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_METHOD_DESCRIPTOR |
            Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc =
R"doc(A Python wrapper around a templated C++ `std::function`.

Notes
-----
This type is not directly instantiable from Python.  Instead, it serves as an entry
point to the template hierarchy, which can be navigated by subscripting the type
just like the `Callable` type hint.

Examples
--------
>>> from bertrand import Function
>>> Function[None, [int, str]]
py::Function<void(const py::Int&, const py::Str&)>
>>> Function[int, ["x": int, "y": int]]
py::Function<py::Int(py::Arg<"x", const py::Int&>, py::Arg<"y", const py::Int&>)>

As long as these types have been instantiated somewhere at the C++ level, then these
accessors will resolve to a unique Python type that wraps that instantiation.  If no
instantiation could be found, then a `TypeError` will be raised.

Because each of these types is unique, they can be used with Python's `isinstance()`
and `issubclass()` functions to check against the exact template signature.  A global
check against this root type will determine whether the function is implemented in C++
(True) or Python (False).)doc",
        .tp_methods = PyFunctionBase::methods,
        .tp_getset = nullptr, // TODO
        .tp_vectorcall_offset = offsetof(PyFunctionBase, vectorcall),

    };

}


/* Dereference operator is used to emulate Python container unpacking. */
template <std::derived_from<Object> Container> requires (impl::iterable<Container>)
[[nodiscard]] auto operator*(const Container& self) {
    return impl::ArgPack<Container>{self};
}


/* A compile-time factory for binding keyword arguments with Python syntax.  A
constexpr instance of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::UnboundArg<name> arg {};


////////////////////////
////    FUNCTION    ////
////////////////////////


namespace impl {

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda type. */
    template <typename R, typename... A>
    struct GetSignature<R(A...)> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename... A>
    struct GetSignature<R(A...) noexcept> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...)> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...) noexcept> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...)> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) &> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) noexcept> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) & noexcept > {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const &> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const noexcept> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const & noexcept> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile &> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile noexcept> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile & noexcept> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile &> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile noexcept> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile & noexcept> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this C&, A...)> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this C&, A...) noexcept> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this const C&, A...)> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this const C&, A...) noexcept> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this volatile C&, A...)> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this volatile C&, A...) noexcept> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this const volatile C&, A...)> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(*)(this const volatile C&, A...) noexcept> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
    };
    template <impl::has_call_operator T>
    struct GetSignature<T> {
        using type = GetSignature<decltype(&T::operator())>::type;
        static constexpr bool enable = GetSignature<decltype(&T::operator())>::enable;
    };

    /* Inspect an unannotated C++ argument in a py::Function. */
    template <typename T>
    struct Param {
        using type                          = T;
        static constexpr StaticStr name     = "";
        static constexpr bool opt           = false;
        static constexpr bool pos_only      = true;
        static constexpr bool pos           = true;
        static constexpr bool kw            = false;
        static constexpr bool kw_only       = false;
        static constexpr bool args          = false;
        static constexpr bool kwargs        = false;
    };

    /* Inspect an argument annotation in a py::Function. */
    template <typename T> requires (std::derived_from<std::decay_t<T>, impl::ArgTag>)
    struct Param<T> {
        using U = std::decay_t<T>;
        using type                          = U::type;
        static constexpr StaticStr name     = U::name;
        static constexpr bool opt           = U::is_opt;
        static constexpr bool pos_only      = U::is_pos && !U::is_kw;
        static constexpr bool pos           = U::is_pos;
        static constexpr bool kw            = U::is_kw;
        static constexpr bool kw_only       = U::is_kw && !U::is_pos;
        static constexpr bool args          = U::is_args;
        static constexpr bool kwargs        = U::is_kwargs;
    };

    /* Analyze an annotated function signature by inspecting each argument and
    extracting metadata that allows call signatures to be resolved at compile time. */
    template <typename... Args>
    struct Signature {
    private:

        template <StaticStr name, typename... Ts>
        static constexpr size_t index_helper = 0;
        template <StaticStr name, typename T, typename... Ts>
        static constexpr size_t index_helper<name, T, Ts...> =
            (Param<T>::name == name) ? 0 : 1 + index_helper<name, Ts...>;

        template <typename... Ts>
        static constexpr size_t opt_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t opt_index_helper<T, Ts...> =
            Param<T>::opt ? 0 : 1 + opt_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t opt_count_helper = I;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t opt_count_helper<I, T, Ts...> =
            opt_count_helper<I + Param<T>::opt, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t pos_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t pos_count_helper<I, T, Ts...> =
            pos_count_helper<I + Param<T>::pos, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t pos_opt_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t pos_opt_count_helper<I, T, Ts...> =
            pos_opt_count_helper<I + (Param<T>::pos && Param<T>::opt), Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_index_helper<T, Ts...> =
            Param<T>::kw ? 0 : 1 + kw_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_count_helper<I, T, Ts...> =
            kw_count_helper<I + Param<T>::kw, Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_only_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_only_index_helper<T, Ts...> =
            Param<T>::kw_only ? 0 : 1 + kw_only_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_only_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_only_count_helper<I, T, Ts...> =
            kw_only_count_helper<I + Param<T>::kw_only, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_only_opt_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_only_opt_count_helper<I, T, Ts...> =
            kw_only_opt_count_helper<I + (Param<T>::kw_only && Param<T>::opt), Ts...>;

        template <typename... Ts>
        static constexpr size_t args_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t args_index_helper<T, Ts...> =
            Param<T>::args ? 0 : 1 + args_index_helper<Ts...>;

        template <typename... Ts>
        static constexpr size_t kwargs_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kwargs_index_helper<T, Ts...> =
            Param<T>::kwargs ? 0 : 1 + kwargs_index_helper<Ts...>;

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

        /* Check whether the argument at index I is marked as optional. */
        template <size_t I> requires (I < size)
        static constexpr bool is_opt = Param<type<I>>::opt;

        /* Check whether the named argument is marked as optional. */
        template <StaticStr name> requires (index<name> < size)
        static constexpr bool is_opt_kw = Param<type<index<name>>>::opt;

    private:

        template <size_t I, typename... Ts>
        static constexpr bool validate = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate<I, T, Ts...> = [] {
            if constexpr (Param<T>::pos) {
                static_assert(
                    I == index<Param<T>::name> || Param<T>::name == "",
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
                    I < opt_index || Param<T>::opt,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Param<T>::kw) {
                static_assert(
                    I == index<Param<T>::name>,
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_only_index || Param<T>::kw_only,
                    "positional-or-keyword arguments must precede keyword-only arguments"
                );
                static_assert(
                    !(Param<T>::kw_only && has_args && I < args_index),
                    "keyword-only arguments must not precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_index,
                    "keyword arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_index || Param<T>::opt || Param<T>::kw_only,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Param<T>::args) {
                static_assert(
                    I < kwargs_index,
                    "variadic positional arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I == args_index,
                    "signature must not contain multiple variadic positional arguments"
                );

            } else if constexpr (Param<T>::kwargs) {
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
            return ((std::strcmp(Param<Args>::name, name) == 0) || ...);
        }

    };

    /// TODO: need to rework Defaults and Arguments to take fully-formed signatures as template
    /// arguments?  Or maybe Defaults can be nested within Signature?  That would be
    /// kind of interesting, especially if it allows me to easily construct the
    /// function's defaults tuple from the source arguments.

    /* An interface to a std::tuple holding default values for all of the optional
    arguments in the target signature. */
    template <typename... Target>
    struct Defaults {
    private:
        using target = Signature<Target...>;

        template <size_t I>
        struct Value  {
            using annotation = target::template type<I>;
            using type = std::remove_cvref_t<typename Param<annotation>::type>;
            static constexpr StaticStr name = Param<annotation>::name;
            static constexpr size_t index = I;
            type value;
        };

        template <size_t I, typename Tuple, typename... Ts>
        struct TupleType { using type = Tuple; };
        template <size_t I, typename... Defaults, typename T, typename... Ts>
        struct TupleType<I, std::tuple<Defaults...>, T, Ts...> {
            template <typename U>
            struct ToValue {
                using type = TupleType<I + 1, std::tuple<Defaults...>, Ts...>::type;
            };
            template <typename U> requires (Param<U>::opt)
            struct ToValue<U> {
                using type = TupleType<I + 1, std::tuple<Defaults..., Value<I>>, Ts...>::type;
            };
            using type = ToValue<T>::type;
        };

        template <size_t I, typename... Ts>
        struct find_helper { static constexpr size_t value = 0; };
        template <size_t I, typename T, typename... Ts>
        struct find_helper<I, std::tuple<T, Ts...>> { static constexpr size_t value = 
            (I == T::index) ? 0 : 1 + find_helper<I, std::tuple<Ts...>>::value;
        };

    public:

        /* A std::tuple type where each element is a Value<I> wrapper around all the
        arguments marked as ::opt in the target signature. */
        using tuple = TupleType<0, std::tuple<>, Target...>::type;

        /* The total number of (optional) arguments that the function accepts, not
        including variadic positional or keyword arguments. */
        static constexpr size_t size = std::tuple_size<tuple>::value;

        /* The number of (optional) positional-only arguments that the function
        accepts, not including variadic positional arguments. */
        static constexpr size_t pos_size = target::pos_opt_count;

        /* The number of (optional) keyword-only arguments that the function accepts,
        not including variadic keyword arguments. */
        static constexpr size_t kw_size = target::kw_only_opt_count;

        /* Given an index into the target signature, find the corresponding index in
        the `type` tuple if that index corresponds to a default value.  Otherwise,
        return the size of the defaults tuple. */
        template <size_t I>
        static constexpr size_t find = find_helper<I, tuple>::value;

        /* Find the index of the named argument, or `size` if the argument is not
        present. */
        template <StaticStr name>
        static constexpr size_t index = find<target::template index<name>>;

        /* Check if the named argument is present in the defaults tuple. */
        template <StaticStr name>
        static constexpr bool contains = index<name> != size;

    private:

        tuple values;

        template <typename... Source>
        struct Constructor {
            using source = Signature<Source...>;

            /* Recursively check whether the default values fully satisfy the target
            signature.  `I` refers to the index in the defaults tuple, while `J` refers
            to the index of the source signature given to the Function constructor. */
            template <size_t I, size_t J>
            static constexpr bool enable_recursive = true;
            template <size_t I, size_t J> requires (I < size && J < source::size)
            static constexpr bool enable_recursive<I, J> = [] {
                using Value = std::tuple_element<I, tuple>::type;
                using Arg = source::template type<J>;
                if constexpr (Param<Arg>::pos) {
                    if constexpr (!std::convertible_to<
                        typename Param<Arg>::type,
                        typename Value::type
                    >) {
                        return false;
                    }
                } else if constexpr (Param<Arg>::kw) {
                    if constexpr (
                        !target::template contains<Param<Arg>::name> ||
                        !source::template contains<Value::name>
                    ) {
                        return false;
                    } else {
                        constexpr size_t idx = index<Param<Arg>::name>;
                        using Match = std::tuple_element<idx, tuple>::type;
                        if constexpr (!std::convertible_to<
                            typename Param<Arg>::type,
                            typename Match::type
                        >) {
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
                source::valid && source::size == size  && enable_recursive<0, 0>;

            template <size_t I>
            static constexpr decltype(auto) build_recursive(Source&&... values) {
                if constexpr (I < source::kw_index) {
                    return impl::unpack_arg<I>(std::forward<Source>(values)...);
                } else {
                    using Value = std::tuple_element<I, tuple>::type;
                    return impl::unpack_arg<source::template index<Value::name>>(
                        std::forward<Source>(values)...
                    ).value;
                }
            }

            /* Invoke the constructor to build the default values tuple from the
            provided arguments, reordering them as needed to account for keywords. */
            template <size_t... Is>
            static constexpr tuple operator()(
                std::index_sequence<Is...>,
                Source&&... values
            ) {
                return {build_recursive<Is>(std::forward<Source>(values)...)...};
            }

        };

    public:

        /* Indicates whether the source arguments fully satisfy the default values in
        the target signature. */
        template <typename... Source>
        static constexpr bool enable = Constructor<Source...>::enable;

        template <typename... Source> requires (enable<Source...>)
        Defaults(Source&&... source) : values(Constructor<Source...>{}(
            std::index_sequence_for<Source...>{},
            std::forward<Source>(source)...
        )) {}

        Defaults(const Defaults& other) = default;
        Defaults(Defaults&& other) = default;

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        auto& get() {
            return std::get<find<I>>(values).value;
        };

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        const auto& get() const {
            return std::get<find<I>>(values).value;
        };

        /* Get the default value associated with the named target argument. */
        template <StaticStr name>
        auto& get() {
            return std::get<index<name>>(values).value;
        };

        /* Get the default value associated with the named target argument. */
        template <StaticStr name>
        const auto& get() const {
            return std::get<index<name>>(values).value;
        };

    };

    /* Translates arguments from the call site to the target signature as long as it is
    fully satisfied, accounting for keyword args, default values, and variadic
    parameter packs. */
    template <typename target, typename... Source>
    struct Arguments {
    private:
        using source = Signature<Source...>;

        template <size_t I>
        using TGT = target::template type<I>;
        template <size_t J>
        using SRC = source::template type<J>;

        /* Upon encountering a variadic positional pack in the target signature,
        recursively traverse the remaining source positional arguments and ensure that
        each is convertible to the target type. */
        template <size_t J, typename POS>
        static constexpr bool consume_target_args = true;
        template <size_t J, typename POS> requires (J < source::kw_index)
        static constexpr bool consume_target_args<J, POS> = [] {
            if constexpr (Param<SRC<J>>::args) {
                if constexpr (!std::convertible_to<typename Param<SRC<J>>::type, POS>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<SRC<J>, POS>) {
                    return false;
                }
            }
            return consume_target_args<J + 1, POS>;
        }();

        /* Upon encountering a variadic keyword pack in the target signature,
        recursively traverse the source keyword arguments and extract any that aren't
        present in the target signature, ensuring they are convertible to the target
        type. */
        template <size_t J, typename KW>
        static constexpr bool consume_target_kwargs = true;
        template <size_t J, typename KW> requires (J < source::size)
        static constexpr bool consume_target_kwargs<J, KW> = [] {
            if constexpr (Param<SRC<J>>::kwargs) {
                if constexpr (!std::convertible_to<typename Param<SRC<J>>::type, KW>) {
                    return false;
                }
            } else {
                if constexpr (
                    (
                        target::has_args &&
                        target::template index<Param<SRC<J>>::name> < target::args_index
                    ) || (
                        !target::template contains<Param<SRC<J>>::name> &&
                        !std::convertible_to<typename Param<SRC<J>>::type, KW>
                    )
                ) {
                    return false;
                }
            }
            return consume_target_kwargs<J + 1, KW>;
        }();

        /* Upon encountering a variadic positional pack in the source signature,
        recursively traverse the target positional arguments and ensure that the source
        type is convertible to each target type. */
        template <size_t I, typename POS>
        static constexpr bool consume_source_args = true;
        template <size_t I, typename POS> requires (I < target::size && I <= target::args_index)
        static constexpr bool consume_source_args<I, POS> = [] {
            if constexpr (Param<TGT<I>>::args) {
                if constexpr (!std::convertible_to<POS, typename Param<TGT<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (
                    !std::convertible_to<POS, typename Param<TGT<I>>::type> ||
                    source::template contains<Param<TGT<I>>::name>
                ) {
                    return false;
                }
            }
            return consume_source_args<I + 1, POS>;
        }();

        /* Upon encountering a variadic keyword pack in the source signature,
        recursively traverse the target keyword arguments and extract any that aren't
        present in the source signature, ensuring that the source type is convertible
        to each target type. */
        template <size_t I, typename KW>
        static constexpr bool consume_source_kwargs = true;
        template <size_t I, typename KW> requires (I < target::size)
        static constexpr bool consume_source_kwargs<I, KW> = [] {
            // TODO: does this work as expected?
            if constexpr (Param<TGT<I>>::kwargs) {
                if constexpr (!std::convertible_to<KW, typename Param<TGT<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<KW, typename Param<TGT<I>>::type>) {
                    return false;
                }
            }
            return consume_source_kwargs<I + 1, KW>;
        }();

        /* Recursively check whether the source arguments conform to Python calling
        conventions (i.e. no positional arguments after a keyword, no duplicate
        keywords, etc.), fully satisfy the target signature, and are convertible to the
        expected types, after accounting for parameter packs in both signatures.

        The actual algorithm is quite complex, especially since it is evaluated at
        compile time via template recursion and must validate both signatures at once.
        Here's a rough outline of how it works:

            1.  Generate two indices, I and J, which are used to traverse over the
                target and source signatures, respectively.
            2.  For each I, J, inspect the target argument at index I:
                a.  If the target argument is positional-only, check that the source
                    argument is not a keyword, otherwise check that the target argument
                    is marked as optional.
                b.  If the target argument is positional-or-keyword, check that the
                    source argument meets the criteria for (a), or that the source
                    signature contains a matching keyword argument.
                c.  If the target argument is keyword-only, check that the source's
                    positional arguments have been exhausted, and that the source
                    signature contains a matching keyword argument or the target
                    argument is marked as optional.
                d.  If the target argument is variadic positional, recur until all
                    source positional arguments have been exhausted, ensuring that
                    each is convertible to the target type.
                e.  If the target argument is variadic keyword, recur until all source
                    keyword arguments have been exhausted, ensuring that each is
                    convertible to the target type.
            3.  Then inspect the source argument at index J:
                a.  If the source argument is positional, check that it is convertible
                    to the target type.
                b.  If the source argument is keyword, check that the target signature
                    contains a matching keyword argument, and that the source argument
                    is convertible to the target type.
                c.  If the source argument is variadic positional, recur until all
                    target positional arguments have been exhausted, ensuring that each
                    is convertible from the source type.
                d.  If the source argument is variadic keyword, recur until all target
                    keyword arguments have been exhausted, ensuring that each is
                    convertible from the source type.
            4.  Advance to the next argument pair and repeat until all arguments have
                been checked.  If there are more target arguments than source arguments,
                then the remaining target arguments must be optional or variadic.  If
                there are more source arguments than target arguments, then we return
                false, as the only way to satisfy the target signature is for it to
                include variadic arguments, which would have avoided this case.

        If all of these conditions are met, then the call operator is enabled and the
        arguments can be translated by the reordering operators listed below.
        Otherwise, the call operator is disabled and the arguments are rejected in a
        way that allows LSPs to provide informative feedback to the user. */
        template <size_t I, size_t J>
        static constexpr bool enable_recursive = true;
        template <size_t I, size_t J> requires (I < target::size && J >= source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            if constexpr (Param<TGT<I>>::args || Param<TGT<I>>::opt) {
                return enable_recursive<I + 1, J>;
            } else if constexpr (Param<TGT<I>>::kwargs) {
                return consume_target_kwargs<source::kw_index, typename Param<TGT<I>>::type>;
            }
            return false;
        }();
        template <size_t I, size_t J> requires (I >= target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = false;
        template <size_t I, size_t J> requires (I < target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            // ensure target arguments are present & expand variadic parameter packs
            if constexpr (Param<TGT<I>>::pos) {
                if constexpr (
                    (J >= source::kw_index && !Param<TGT<I>>::opt) ||
                    (
                        Param<TGT<I>>::name != "" &&
                        source::template contains<Param<TGT<I>>::name>
                    )
                ) {
                    return false;
                }
            } else if constexpr (Param<TGT<I>>::kw_only) {
                if constexpr (
                    J < source::kw_index ||
                    (
                        !source::template contains<Param<TGT<I>>::name> &&
                        !Param<TGT<I>>::opt
                    )
                ) {
                    return false;
                }
            } else if constexpr (Param<TGT<I>>::kw) {
                if constexpr ((
                    J < source::kw_index &&
                    source::template contains<Param<TGT<I>>::name>
                ) || (
                    J >= source::kw_index &&
                    !source::template contains<Param<TGT<I>>::name> &&
                    !Param<TGT<I>>::opt
                )) {
                    return false;
                }
            } else if constexpr (Param<TGT<I>>::args) {
                if constexpr (!consume_target_args<J, typename Param<TGT<I>>::type>) {
                    return false;
                }
                return enable_recursive<I + 1, source::kw_index>;  // skip to source keywords
            } else if constexpr (Param<TGT<I>>::kwargs) {
                return consume_target_kwargs<
                    source::kw_index,
                    typename Param<TGT<I>>::type
                >;  // end of expression
            } else {
                return false;  // not reachable
            }

            // validate source arguments & expand unpacking operators
            if constexpr (Param<SRC<J>>::pos) {
                if constexpr (!std::convertible_to<SRC<J>, TGT<I>>) {
                    return false;
                }
            } else if constexpr (Param<SRC<J>>::kw) {
                if constexpr (target::template contains<Param<SRC<J>>::name>) {
                    using type = TGT<target::template index<Param<SRC<J>>::name>>;
                    if constexpr (!std::convertible_to<SRC<J>, type>) {
                        return false;
                    }
                } else if constexpr (!target::has_kwargs) {
                    using type = Param<TGT<target::kwargs_index>>::type;
                    if constexpr (!std::convertible_to<SRC<J>, type>) {
                        return false;
                    }
                }
            } else if constexpr (Param<SRC<J>>::args) {
                if constexpr (!consume_source_args<I, typename Param<SRC<J>>::type>) {
                    return false;
                }
                return enable_recursive<target::args_index + 1, J + 1>;  // skip to target keywords
            } else if constexpr (Param<SRC<J>>::kwargs) {
                return consume_source_kwargs<I, typename Param<SRC<J>>::type>;  // end of expression
            } else {
                return false;  // not reachable
            }

            // advance to next argument pair
            return enable_recursive<I + 1, J + 1>;
        }();

        template <size_t I, typename T>
        static constexpr void build_kwargs(
            std::unordered_map<std::string, T>& map,
            Source&&... args
        ) {
            using Arg = SRC<source::kw_index + I>;
            if constexpr (!target::template contains<Param<Arg>::name>) {
                map.emplace(
                    Param<Arg>::name,
                    impl::unpack_arg<source::kw_index + I>(
                        std::forward<Source>(args)...
                    )
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
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Param<TGT<I>>::kw && !Param<TGT<I>>::kw_only)
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Param<TGT<I>>::name>) {
                constexpr size_t idx = source::template index<Param<TGT<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Param<TGT<I>>::kw_only)
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (source::template contains<Param<TGT<I>>::name>) {
                constexpr size_t idx = source::template index<Param<TGT<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Param<TGT<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            using Pack = std::vector<typename Param<TGT<I>>::type>;
            Pack vec;
            if constexpr (I < source::kw_index) {
                constexpr size_t diff = source::kw_index - I;
                vec.reserve(diff);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(impl::unpack_arg<I + Js>(std::forward<Source>(args)...)), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
            }
            return vec;
        }

        NO_UNPACK requires (Param<TGT<I>>::kwargs)
        static constexpr auto cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Param<TGT<I>>::type>;
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
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Param<TGT<I>>::opt) {
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

        ARGS_UNPACK requires (Param<TGT<I>>::kw && !Param<TGT<I>>::kw_only)
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Param<TGT<I>>::name>) {
                constexpr size_t idx = source::template index<Param<TGT<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Param<TGT<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "parameter '" + std::string(TGT<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        ARGS_UNPACK requires (Param<TGT<I>>::kw_only)
        static constexpr std::decay_t<typename Param<TGT<I>>::type> cpp(
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        ARGS_UNPACK requires (Param<TGT<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            using Pack = std::vector<typename Param<TGT<I>>::type>;
            Pack vec;
            if constexpr (I < source::args_index) {
                constexpr size_t diff = source::args_index - I;
                vec.reserve(diff + size);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(
                        impl::unpack_arg<I + Js>(std::forward<Source>(args)...)
                    ), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
                vec.insert(vec.end(), iter, end);
            }
            return vec;
        }

        ARGS_UNPACK requires (Param<T<I>>::kwargs)
        static constexpr auto cpp(
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Param<T<I>>::kw && !Param<T<I>>::kw_only)
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
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
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Param<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Param<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Param<T<I>>::opt) {
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

        KWARGS_UNPACK requires (Param<T<I>>::kw_only)
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (source::template contains<Param<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Param<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Param<T<I>>::opt) {
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

        KWARGS_UNPACK requires (Param<T<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Param<T<I>>::kwargs)
        static constexpr auto cpp(
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Param<T<I>>::type>;
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
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
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

        ARGS_KWARGS_UNPACK requires (Param<T<I>>::kw && !Param<T<I>>::kw_only)
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
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
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Param<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Param<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter != end) {
                    return *(iter++);
                } else if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Param<T<I>>::opt) {
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

        ARGS_KWARGS_UNPACK requires (Param<T<I>>::kw_only)
        static constexpr std::decay_t<typename Param<T<I>>::type> cpp(
            const Defaults& defaults,
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

        ARGS_KWARGS_UNPACK requires (Param<T<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
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

        ARGS_KWARGS_UNPACK requires (Param<T<I>>::kwargs)
        static constexpr auto cpp(
            const Defaults& defaults,
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
        static std::decay_t<typename Param<T<I>>::type> from_python(
            const Defaults& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (I < nargs) {
                return reinterpret_borrow<Object>(array[I]);
            }
            if constexpr (Param<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required positional-only argument at index " +
                    std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Param<T<I>>::kw)
        static std::decay_t<typename Param<T<I>>::type> from_python(
            const Defaults& defaults,
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
                    } else if (std::strcmp(name, Param<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[nargs + i]);
                    }
                }
            }
            if constexpr (Param<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required argument '" + std::string(T<I>::name) +
                    "' at index " + std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Param<T<I>>::kw_only)
        static std::decay_t<typename Param<T<I>>::type> from_python(
            const Defaults& defaults,
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
                    } else if (std::strcmp(name, Param<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[i]);
                    }
                }
            }
            if constexpr (Param<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required keyword-only argument '" +
                    std::string(T<I>::name) + "'"
                );
            }
        }

        template <size_t I> requires (Param<T<I>>::args)
        static auto from_python(
            const Defaults& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::vector<typename Param<T<I>>::type> vec;
            for (size_t i = I; i < nargs; ++i) {
                vec.push_back(reinterpret_borrow<Object>(array[i]));
            }
            return vec;
        }

        template <size_t I> requires (Param<T<I>>::kwargs)
        static auto from_python(
            const Defaults& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::unordered_map<std::string, typename Param<T<I>>::type> map;
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
                array[J + 1] = release(as_object(
                    impl::unpack_arg<J>(std::forward<Source>(args)...)
                ));
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Param<S<J>>::kw)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            try {
                PyTuple_SET_ITEM(
                    kwnames,
                    J - source::kw_index,
                    Py_NewRef(impl::TemplateString<Param<S<J>>::name>::ptr)
                );
                array[J + 1] = release(as_object(
                    impl::unpack_arg<J>(std::forward<Source>(args)...)
                ));
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Param<S<J>>::args)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_args = impl::unpack_arg<J>(std::forward<Source>(args)...);
                for (const auto& value : var_args) {
                    array[curr] = release(as_object(value));
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Param<S<J>>::kwargs)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_kwargs = impl::unpack_arg<J>(std::forward<Source>(args)...);
                for (const auto& [key, value] : var_kwargs) {
                    PyObject* name = PyUnicode_FromStringAndSize(
                        key.data(),
                        key.size()
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(kwnames, curr - source::kw_index, name);
                    array[curr] = release(as_object(value));
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

    };

    /* After invoking a function with variadic positional arguments, the argument
    iterators must be exhausted, otherwise there are additional positional arguments
    that were not consumed. */
    template <typename Iter, std::sentinel_for<Iter> End>
    static void assert_var_args_are_consumed(Iter& iter, const End& end) {
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

    /// TODO: account for duplicate arguments from the source signature during
    /// kwargs check.

    /* Before invoking a function with variadic keyword arguments, those arguments need
    to be scanned to ensure each of them are recognized and do not interfere with other
    keyword arguments given in the source signature. */
    template <typename target, size_t... Is, typename Kwargs>
    static void assert_var_kwargs_are_recognized(
        std::index_sequence<Is...>,
        const Kwargs& kwargs
    ) {
        std::vector<std::string> extra;
        for (const auto& [key, value] : kwargs) {
            bool is_empty = key == "";
            bool match = (
                (key == Param<typename target::template type<Is>>::name) || ...
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

}


template <typename Return, typename... Target>
struct Interface<Function<Return(*)(Target...)>> {
    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    __declspec(property(get=_get_qualname, put=_set_qualname))
        std::optional<std::string> __qualname__;
    [[nodiscard]] std::optional<std::string> _get_qualname(this const auto& self);
    void _set_qualname(this auto& self, const std::string& qualname);

    __declspec(property(get=_get_module, put=_set_module))
        std::optional<std::string> __module__;
    [[nodiscard]] std::optional<std::string> _get_module(this const auto& self);
    void _set_module(this auto& self, const std::string& module);

    __declspec(property(get=_get_code, put=_set_code)) std::optional<Code> __code__;
    [[nodiscard]] std::optional<Code> _get_code(this const auto& self);
    void _set_code(this auto& self, const Code& code);

    __declspec(property(get=_get_globals)) std::optional<Dict<Str, Object>> __globals__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_globals(this const auto& self);

    __declspec(property(get=_get_closure)) std::optional<Tuple<Object>> __closure__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_closure(this const auto& self);

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    __declspec(property(get=_get_kwdefaults, put=_set_kwdefaults))
        std::optional<Dict<Str, Object>> __kwdefaults__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_kwdefaults(this const auto& self);
    void _set_kwdefaults(this auto& self, const Dict<Str, Object>& kwdefaults);

    __declspec(property(get=_get_type_params, put=_set_type_params))
        std::optional<Tuple<Object>> __type_params__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_type_params(this const auto& self);
    void _set_type_params(this auto& self, const Tuple<Object>& type_params);

    /// TODO: instance methods (given as pointer to member functions) support
    /// additional __self__ and __func__ properties?

};


/* A universal function wrapper that can represent either a Python function exposed to
C++, or a C++ function exposed to Python with equivalent semantics.  Supports keyword,
optional, and variadic arguments through the `py::Arg` annotation.

Notes
-----
When constructed with a C++ function, this class will create a Python object that
encapsulates the function and allows it to be called from Python.  The Python wrapper
has a unique type for each template signature, which allows Bertrand to enforce strong
type safety and provide accurate error messages if a signature mismatch is detected.
It also allows Bertrand to directly unpack the underlying function from the Python
object, bypassing the Python interpreter and demoting the call to pure C++ where
possible.  If the function accepts `py::Arg` annotations in its signature, then these
will be extracted using template metaprogramming and observed when the function is
called in either language.

When constructed with a Python function, this class will store the function directly
and allow it to be called from C++ with the same semantics as the Python interpreter.
The `inspect` module is used to extract parameter names, categories, and default
values, as well as type annotations if they are present, all of which will be checked
against the expected signature and result in errors if they do not match.  `py::Arg`
annotations can be used to provide keyword, optional, and variadic arguments according
to the templated signature, and the function will be called directly using the
vectorcall protocol, which is the most efficient way to call a Python function from
C++.  

Container unpacking via the `*` and `**` operators is also supported, although it must
be explicitly enabled for C++ containers by overriding the dereference operator (which
is done automatically for iterable Python objects), and is limited in some respects
compared to Python:

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
        at compile time, this cannot be done statically.

Examples
--------
Consider the following function:

    int subtract(int x, int y) {
        return x - y;
    }

We can directly wrap this as a `py::Function` if we want, which does not alter the
calling convention or signature in any way:

    py::Function func("subtract", "a simple example function", subtract);
    func(1, 2);  // returns -1

If this function is exported to Python, its call signature will remain unchanged,
meaning that both arguments must be supplied as positional-only arguments, and no
default values will be considered.

    >>> func(1, 2)  # ok, returns -1
    >>> func(1)  # error: missing required positional argument
    >>> func(1, y = 2)  # error: unexpected keyword argument

We can add parameter names and default values by annotating the C++ function (or a
wrapper around it) with `py::Arg` tags.  For instance:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", int> x, py::Arg<"y", int>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

Note that the annotations store their values in an explicit `value` member, which uses
aggregate initialization to extend the lifetime of temporaries.  The annotations can
thus store references with the same semantics as an ordinary function call, as if the
annotations were not present.  For instance, this:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", const int&> x, py::Arg<"y", const int&>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

is equivalent to the previous example in every way, but with the added benefit that the
`x` and `y` arguments will not be copied unnecessarily according to C++ value
semantics.

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
Python and called similarly:

    >>> func(1)
    >>> func(1, 2)
    >>> func(1, y = 2)
    >>> func(x = 1)
    >>> func(x = 1, y = 2)
    >>> func(y = 2, x = 1)

What's more, all of the logic necessary to handle these cases is resolved statically at
compile time, meaning that there is no runtime cost for using these annotations, and no
additional code is generated for the function itself.  When it is called from C++, all
we have to do is inspect the provided arguments and match them against the underlying
signature, generating a compile time index sequence that can be used to reorder the
arguments and insert default values where needed.  In fact, each of the above
invocations will be transformed into the same underlying function call, with virtually
the same performance characteristics as raw C++ (disregarding any extra indirection
caused by the `std::function` wrapper).

Additionally, since all arguments are evaluated purely at compile time, we can enforce
strong type safety guarantees on the function signature and disallow invalid calls
using template constraints.  This means that proper call syntax is automatically
enforced throughout the codebase, in a way that allows static analyzers to give proper
syntax highlighting and LSP support. */
template <typename F = Object(
    Arg<"args", const Object&>::args,
    Arg<"kwargs", const Object&>::kwargs
)> requires (impl::GetSignature<F>::enable)
struct Function : Function<typename impl::GetSignature<F>::type> {};


/// TODO: in order to properly forward `self`, this class will need to be split into
/// a bunch of different specializations, depending on how the self argument needs to
/// be handled.  That will require me to move most of the shared logic into the impl::
/// namespace.  The bound method versions might also have to implement a different
/// Python type in order to handle the forwarding semantics.

/// TODO: class methods can be indicated by a member method of Type<Wrapper>.  That
/// would allow this mechanism to scale arbitrarily.


/* Specialization for static functions without a bound `self` argument. */
template <typename Return, typename... Target>
struct Function<Return(*)(Target...)> : Object, Interface<Function<Return(*)(Target...)>> {
protected:

    using target = impl::Signature<Target...>;
    static_assert(target::valid);

public:

    /// TODO: perhaps these should be placed in the interface for each specialization.

    /* The total number of arguments that the function accepts, not including variadic
    positional or keyword arguments. */
    static constexpr size_t size = target::size - target::has_args - target::has_kwargs;

    /* The number of positional-only arguments that the function accepts, not including
    variadic positional arguments. */
    static constexpr size_t pos_size = target::pos_count;

    /* The number of keyword-only arguments that the function accepts, not including
    variadic keyword arguments. */
    static constexpr size_t kw_size = target::kw_only_count;

    /* Indicates whether an argument with the given name is present. */
    template <StaticStr name>
    static constexpr bool has_arg =
        target::template contains<name> &&
        target::template index<name> < target::kw_only_index;

    /* indicates whether a keyword argument with the given name is present. */
    template <StaticStr name>
    static constexpr bool has_kwarg =
        target::template contains<name> &&
        target::template index<name> >= target::kw_index;

    /* Indicates whether the function accepts variadic positional arguments. */
    static constexpr bool has_var_args = target::has_args;

    /* Indicates whether the function accepts variadic keyword arguments. */
    static constexpr bool has_var_kwargs = target::has_kwargs;

    using Defaults = impl::Defaults<Target...>;

protected:

    /* A unique Python type to represent functions with this signature in Python. */
    struct PyFunction {
    private:

        static const std::string& type_docstring() {
            static const std::string string =
                "Python wrapper for a C++ function of type " +
                impl::demangle(typeid(Function).name());
            return string;
        }

    public:
        impl::PyFunctionBase base;
        std::function<Return(Target...)> func;
        Defaults defaults;

        // TODO: this returns a repr for an instance of this type, but what about the
        // type itself?  The only way to account for that is to use a metaclass, which
        // I'd like to support anyways at some point.
        // -> This probably necessitates the use of a heap type instead of a static
        // type.  This also raises the minimum Python version to 3.12
        // -> Ideally, this would always print out the demangled template signature.
        // For now, it prints out the mangled type name, which is okay.
        // -> In the end, I should probably implement a generic bertrand.metaclass
        // type that is inherited by all types which are created by the binding library.
        // This can be used to implement the __repr__ method for the type itself, in
        // such a way that it always demangles the corresponding C++ type name.

        static PyObject* __repr__(PyFunction* self) {
            static const std::string demangled =
                impl::demangle(typeid(Function).name());
            std::stringstream stream;
            stream << "<" << demangled << " at " << self << ">";
            std::string s = stream.str();
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

        inline static PyTypeObject type = {
            .ob_base = PyVarObject_HEAD_INIT(nullptr, 0)
            .tp_name = typeid(Function).name(),
            .tp_basicsize = sizeof(PyFunction),
            .tp_dealloc = (destructor) &__dealloc__,
            .tp_repr = (reprfunc) &__repr__,
            .tp_str = (reprfunc) &__repr__,
            .tp_flags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_METHOD_DESCRIPTOR |
                Py_TPFLAGS_HAVE_VECTORCALL,
            .tp_doc = PyDoc_STR(type_docstring().c_str()),
            .tp_methods = nullptr,  // TODO
            .tp_getset = nullptr,  // TODO
        };

    };

public:
    struct __python__ : def<__python__, Function>, PyObject {
        std::string name;
        std::string docstring;
        std::function<Return(Target...)> func;
        Defaults defaults;
        vectorcallfunc vectorcall;

        __python__(
            std::string&& name,
            std::string&& docstring,
            std::function<Return(Target...)>&& func,
            Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            vectorcall(&__vectorcall__)
        {}

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings) {
            /// TODO: manually initialize a heap type with a correct vectorcall offset.
            /// This has to be an instance of the metaclass, which may require a
            /// forward reference here.
        }

        /// TODO: have to do some checking to see if there's a self argument I need to
        /// forward to.  Perhaps Function can be templated to accept member functions?

        static PyObject* __vectorcall__(
            __python__* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            try {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyFunction* self,
                    PyObject* const* args,
                    Py_ssize_t nargsf,
                    PyObject* kwnames
                ) {
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    if constexpr (!target::has_args) {
                        constexpr size_t expected = target::size - target::kw_only_count;
                        if (nargs > expected) {
                            throw TypeError(
                                "expected at most " + std::to_string(expected) +
                                " positional arguments, but received " +
                                std::to_string(nargs)
                            );
                        }
                    }
                    size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
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
                    if constexpr (std::is_void_v<Return>) {
                        self->func(
                            {Arguments<Target...>::template from_python<Is>(
                                self->defaults,
                                args,
                                nargs,
                                kwnames,
                                kwcount
                            )}...
                        );
                        Py_RETURN_NONE;
                    } else {
                        return release(as_object(
                            self->func(
                                {Arguments<Target...>::template from_python<Is>(
                                    self->defaults,
                                    args,
                                    nargs,
                                    kwnames,
                                    kwcount
                                )}...
                            )
                        ));
                    }
                }(
                    std::make_index_sequence<target::size>{},
                    self,
                    args,
                    nargsf,
                    kwnames
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(__python__* self) {
            static const std::string demangled =
                impl::demangle(typeid(Function).name());
            std::string s = "<" + demangled + " at " +
                std::to_string(reinterpret_cast<size_t>(self)) + ">";
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

    };

    using ReturnType = Return;
    using ArgTypes = std::tuple<typename Inspect<Target>::type...>;
    using Annotations = std::tuple<Target...>;

    /* Instantiate a new function type with the same arguments but a different return
    type. */
    template <typename R>
    using with_return = Function<R(Target...)>;

    /* Instantiate a new function type with the same return type but different
    argument annotations. */
    template <typename... Ts>
    using with_args = Function<Return(Ts...)>;

    /* Template constraint that evaluates to true if this function can be called with
    the templated argument types. */
    template <typename... Source>
    static constexpr bool invocable = Arguments<Source...>::enable;

    // TODO: constructor should fail if the function type is a subclass of my root
    // function type, but not a subclass of this specific function type.  This
    // indicates a type mismatch in the function signature, which is a violation of
    // static type safety.  I can then print a helpful error message with the demangled
    // function types which can show their differences.
    // -> This can be implemented in the actual call operator itself, but it would be
    // better to implement it on the constructor side.  Perhaps including it in the
    // isinstance()/issubclass() checks would be sufficient, since those are used
    // during implicit conversion anyways.

    Function(PyObject* p, borrowed_t t) : Base(p, t) {}
    Function(PyObject* p, stolen_t t) : Base(p, t) {}

    template <typename... Args> requires (implicit_ctor<Function>::template enable<Args...>)
    Function(Args&&... args) : Base(
        implicit_ctor<Function>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Function>::template enable<Args...>)
    explicit Function(Args&&... args) : Base(
        explicit_ctor<Function>{},
        std::forward<Args>(args)...
    ) {}

    /// TODO: invoke() should be unified for both Python functions and C++ functions.
    /// the python overload will take a PyObject* as the first argument, followed by
    /// Python-style argument annotations for the call itself.  The C++ overload will
    /// take a Defaults tuple followed by a function that is invocable with the target
    /// signature.  There will also have to be two additional overloads for when
    /// invoke() is called on a Python object.  If the object is implemented in C++,
    /// unwrap it and forward to invoke() on the C++ object.  If it is implemented in
    /// Python, get its ptr() and forward to invoke() on the Python object.

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

            // NOTE: we MUST use aggregate initialization of argument proxies in order
            // to extend the lifetime of temporaries for the duration of the function
            // call.  This is the only guaranteed way of avoiding UB in this context.

            // if there are no parameter packs, then we simply reorder the arguments
            // and call the function directly
            if constexpr (!source::has_args && !source::has_kwargs) {
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        std::forward<Source>(args)...
                    )}...);
                } else {
                    return func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        std::forward<Source>(args)...
                    )}...);
                }

            // variadic positional arguments are passed as an iterator range, which
            // must be exhausted after the function call completes
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = impl::unpack_arg<source::args_index>(
                    std::forward<Source>(args)...
                );
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                } else {
                    decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                    return result;
                }

            // variadic keyword arguments are passed as a dictionary, which must be
            // validated up front to ensure all keys are recognized
            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                    std::forward<Source>(args)...
                );
                if constexpr (!target::has_kwargs) {
                    impl::assert_var_kwargs_are_recognized<target>(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                } else {
                    return func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                }

            // interpose the two if there are both positional and keyword argument packs
            } else {
                auto var_kwargs = impl::unpack_arg<source::kwargs_index>(
                    std::forward<Source>(args)...
                );
                if constexpr (!target::has_kwargs) {
                    impl::assert_var_kwargs_are_recognized<target>(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                auto var_args = impl::unpack_arg<source::args_index>(
                    std::forward<Source>(args)...
                );
                auto iter = var_args.begin();
                auto end = var_args.end();
                if constexpr (std::is_void_v<Return>) {
                    func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
                    }
                } else {
                    decltype(auto) result = func({Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )}...);
                    if constexpr (!target::has_args) {
                        impl::assert_var_args_are_consumed(iter, end);
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

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <typename R = Object, typename... Source> requires (invocable<Source...>)
    static Return invoke_py(PyObject* func, Source&&... args) {
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );

        // bypass the Python interpreter if the function is an instance of the coupled
        // function type, which guarantees that the template signatures exactly match
        if (PyType_IsSubtype(Py_TYPE(func), &PyFunction::type)) {
            PyFunction* func = reinterpret_cast<PyFunction*>(ptr(func));
            if constexpr (std::is_void_v<Return>) {
                invoke_cpp(
                    func->defaults,
                    func->func,
                    std::forward<Source>(args)...
                );
            } else {
                return invoke_cpp(
                    func->defaults,
                    func->func,
                    std::forward<Source>(args)...
                );
            }
        }

        // fall back to an optimized Python call
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            PyObject* result;

            // if there are no arguments, we can use the no-args protocol
            if constexpr (source::size == 0) {
                result = PyObject_CallNoArgs(func);

            // if there are no variadic arguments, we can stack allocate the argument
            // array with a fixed size
            } else if constexpr (!source::has_args && !source::has_kwargs) {
                // if there is only one argument, we can use the one-arg protocol
                if constexpr (source::size == 1) {
                    if constexpr (source::has_kw) {
                        result = PyObject_CallOneArg(
                            func,
                            ptr(as_object(
                                impl::unpack_arg<0>(std::forward<Source>(args)...).value
                            ))
                        );
                    } else {
                        result = PyObject_CallOneArg(
                            func,
                            ptr(as_object(
                                impl::unpack_arg<0>(std::forward<Source>(args)...)
                            ))
                        );
                    }

                // if there is more than one argument, we construct a vectorcall
                // argument array
                } else {
                    // PY_VECTORCALL_ARGUMENTS_OFFSET requires an extra element
                    PyObject* array[source::size + 1];
                    array[0] = nullptr;  // first element is reserved for 'self'
                    PyObject* kwnames;
                    if constexpr (source::has_kw) {
                        kwnames = PyTuple_New(source::kw_count);
                    } else {
                        kwnames = nullptr;
                    }
                    (
                        Arguments<Source...>::template to_python<Is>(
                            kwnames,
                            array,  // 1-indexed
                            std::forward<Source>(args)...
                        ),
                        ...
                    );
                    Py_ssize_t npos = source::size - source::kw_count;
                    result = PyObject_Vectorcall(
                        func,
                        array + 1,  // skip the first element
                        npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    for (size_t i = 1; i <= source::size; ++i) {
                        Py_XDECREF(array[i]);  // release all argument references
                    }
                }

            // otherwise, we have to heap-allocate the array with a variable size
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args =
                    impl::unpack_arg<source::args_index>(std::forward<Source>(args)...);
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
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            // The following specializations handle the cross product of
            // positional/keyword parameter packs which differ only in initialization
            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs =
                    impl::unpack_arg<source::kwargs_index>(std::forward<Source>(args)...);
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
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else {
                auto var_args =
                    impl::unpack_arg<source::args_index>(std::forward<Source>(args)...);
                auto var_kwargs =
                    impl::unpack_arg<source::kwargs_index>(std::forward<Source>(args)...);
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
                    func,
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;
            }

            // A null return value indicates an error
            if (result == nullptr) {
                Exception::from_python();
            }

            // Python void functions return a reference to None, which we must release
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

    /// TODO: operator() should be a specialization of __call__

    /* Call the function with the given arguments.  If the wrapped function is of the
    coupled Python type, then this will be translated into a raw C++ call, bypassing
    Python entirely. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        if constexpr (std::is_void_v<Return>) {
            invoke_py(m_ptr, std::forward<Source>(args)...);  // Don't reference m_ptr directly, and turn this into a control struct
        } else {
            return invoke_py(m_ptr, std::forward<Source>(args)...);
        }
    }

    // get_default<I>() returns a reference to the default value of the I-th argument
    // get_default<name>() returns a reference to the default value of the named argument

    // TODO:
    // .defaults -> MappingProxy<Str, Object>
    // .annotations -> MappingProxy<Str, Type<Object>>
    // .posonly -> Tuple<Str>
    // .kwonly -> Tuple<Str>

    /* Get the name of the wrapped function. */
    __declspec(property(get=_get_name)) std::string name;
    [[nodiscard]] std::string _get_name(this const auto& self) {
        // TODO: get the base type and check against it.
        if (true) {
            Type<Function> func_type;
            if (PyType_IsSubtype(
                Py_TYPE(ptr(self)),
                reinterpret_cast<PyTypeObject*>(ptr(func_type))
            )) {
                PyFunction* func = reinterpret_cast<PyFunction*>(ptr(self));
                return func->base.name;
            }
            // TODO: print out a detailed error message with both signatures
            throw TypeError("signature mismatch");
        }

        PyObject* result = PyObject_GetAttrString(ptr(self), "__name__");
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

    // /* Get a read-only dictionary mapping argument names to their default values. */
    // __declspec(property(get=_get_defaults)) MappingProxy<Dict<Str, Object>> defaults;
    // [[nodiscard]] MappingProxy<Dict<Str, Object>> _get_defaults() const {
    //     // TODO: check for the PyFunction type and extract the defaults directly
    //     if (true) {
    //         Type<Function> func_type;
    //         if (PyType_IsSubtype(
    //             Py_TYPE(ptr(*this)),
    //             reinterpret_cast<PyTypeObject*>(ptr(func_type))
    //         )) {
    //             // TODO: generate a dictionary using a fold expression over the
    //             // defaults, then convert to a MappingProxy.
    //             // Or maybe return a std::unordered_map
    //             throw NotImplementedError();
    //         }
    //         // TODO: print out a detailed error message with both signatures
    //         throw TypeError("signature mismatch");
    //     }

    //     // check for positional defaults
    //     PyObject* pos_defaults = PyFunction_GetDefaults(ptr(*this));
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
    //         if (PyFunction_SetAnnotations(self(), ptr(result))) {
    //             Exception::from_python();
    //         }
    //     }
    // }

    /* Set the default value for one or more arguments. */
    template <typename... Values> requires (sizeof...(Values) > 0)  // and all are valid
    void defaults(Values&&... values);

    /* Get a read-only mapping of argument names to their type annotations. */
    [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const;

    /* Set the type annotation for one or more arguments. */
    template <typename... Annotations> requires (sizeof...(Annotations) > 0)  // and all are valid
    void annotations(Annotations&&... annotations);

};


/* Specialization for mutable instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...)> :
    Object, Interface<Function<Return(Self::*)(Target...)>>
{

};


/* Specialization for const instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) const> :
    Object, Interface<Function<Return(Self::*)(Target...) const>>
{

};


/* Specialization for volatile instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) volatile> :
    Object, Interface<Function<Return(Self::*)(Target...) volatile>>
{

};


/* Specialization for const volatile instance methods. */
template <typename Return, typename Self, typename... Target>
struct Function<Return(Self::*)(Target...) const volatile> :
    Object, Interface<Function<Return(Self::*)(Target...) const volatile>>
{

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


template <typename R, typename... A>
struct __as_object__<R(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<R(*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...)> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile> : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<std::function<R(A...)>> : Returns<Function<R(A...)>> {};


template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if (impl::cpp_like<T>) {
            return issubclass<T, Function<R(A...)>>();

        } else if constexpr (issubclass<T, Function<R(A...)>>()) {
            return ptr(obj) != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            return ptr(obj) != nullptr && (
                PyFunction_Check(ptr(obj)) ||
                PyMethod_Check(ptr(obj)) ||
                PyCFunction_Check(ptr(obj))
            );
        } else {
            return false;
        }
    }
};


// TODO: if default specialization is given, type checks should be fully generic, right?
// issubclass<T, Function<>>() should check impl::is_callable_any<T>;

template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>> : Returns<bool> {
    static constexpr bool operator()() {
        return std::is_invocable_r_v<R, T, A...>;
    }
    static constexpr bool operator()(const T&) {
        // TODO: this is going to have to be radically rethought.
        // Maybe I just forward to an issubclass() check against the type object?
        // In fact, this could maybe be standard operating procedure for all types.
        // 
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(ptr(Type<T>())),
            reinterpret_cast<PyTypeObject*>(ptr(Type<Function<R(A...)>>()))
        );
    }
};


template <typename Return, typename... Target, typename Func, typename... Values>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            "",
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    std::convertible_to<std::string> Doc,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Doc, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Doc&& doc, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            std::forward<Doc>(doc),
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


namespace impl {

    /* A convenience function that calls a named method of a Python object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <StaticStr Name, typename Self, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_method(Self&& self, Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        auto meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(self),
            TemplateString<Name>::ptr
        ));
        if (ptr(meth) == nullptr) {
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
    template <typename Self, StaticStr Name, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        auto meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(Self::type),
            TemplateString<Name>::ptr
        ));
        if (ptr(meth) == nullptr) {
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

}


/////////////////////////
////    OVERLOADS    ////
/////////////////////////


/* An Python-compatible overload set that dispatches to a collection of functions using
an efficient trie-based data structure. */
struct Overload : Object {
private:
    using Base = Object;
    friend class Type<OverloadSet>;

    /* Argument maps are topologically sorted such that parent classes are always
    checked after subclasses. */
    struct Compare {
        bool operator()(PyObject* lhs, PyObject* rhs) const {
            int result = PyObject_IsSubclass(lhs, rhs);
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        }
    };

    /* Each node in the trie has 2 maps and a terminal function if this is the last
    argument in the list.  The first map is a topological map of positional arguments
    to the next node in the trie.  The second is a map of keyword names to another
    topological map with the same behavior. */
    struct Node {
        std::map<PyObject*, Node*, Compare> positional;
        std::unordered_map<std::string, std::map<PyObject*, Node*, Compare>> keyword;
        PyObject* func = nullptr;
    };

    /* The Python representation of an Overload object. */
    struct PyOverload {
        PyObject_HEAD
        Node root;  // describes the zeroth arg (return type) of the overload set
        std::unordered_set<PyObject*> funcs;  // all functions in the overload set

        static PyObject* overload(PyObject* self, PyObject* func) {
            PyErr_SetString(PyExc_NotImplementedError, nullptr);
        }

        inline static PyMethodDef methods[] = {
            {
                "overload",
                overload,
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyTypeObject* type = {

        };

    };

public:

    OverloadSet(PyObject* p, borrowed_t t) : Base(p, t) {}
    OverloadSet(PyObject* p, stolen_t t) : Base(p, t) {}

    template <typename... Args> requires (implicit_ctor<OverloadSet>::template enable<Args...>)
    OverloadSet(Args&&... args) : Base(
        implicit_ctor<OverloadSet>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<OverloadSet>::template enable<Args...>)
    explicit OverloadSet(Args&&... args) : Base(
        explicit_ctor<OverloadSet>{},
        std::forward<Args>(args)...
    ) {}

    /* Add an overload from C++. */
    template <typename Return, typename... Target>
    void overload(const Function<Return(Target...)>& func) {

    }

    /* Attach the overload set to a type as a descriptor. */
    template <StaticStr Name, typename T>
    void attach(Type<T>& type) {

    }

    // TODO: index into an overload set to resolve a particular function

};


template <>
struct Type<OverloadSet> : Object {
    using __python__ = OverloadSet::PyOverload;

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename Return, typename... Target>
    static void overload(OverloadSet& self, const Function<Return(Target...)>& func) {
        self.overload(func);
    }

    template <StaticStr Name, typename T>
    static void attach(OverloadSet& self, Type<T>& type) {
        self.attach<Name>(type);
    }

};


template <>
struct __init__<Type<OverloadSet>> : Returns<Type<OverloadSet>> {
    static Type<OverloadSet> operator()() {
        return reinterpret_borrow<Type<OverloadSet>>(
            reinterpret_cast<PyObject*>(Type<OverloadSet>::__python__::type)
        );
    }
};


}  // namespace py


#endif
