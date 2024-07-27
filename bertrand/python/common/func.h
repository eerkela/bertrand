#ifndef BERTRAND_PYTHON_COMMON_FUNC_H
#define BERTRAND_PYTHON_COMMON_FUNC_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"


namespace py {


// TODO: descriptors might also allow for multiple dispatch if I manage it correctly.
// -> The descriptor would store an internal map of types to Python/C++ functions, and
// could be appended to from either side.  This would replace pybind11's overloading
// mechanism, and would store signatures in topographical order.  When the descriptor
// is called, it would test each signature in order, and call the first one that
// fully matches
// -> pybind11's overloading mechanism can't be used anyways, since I need to decouple
// from it entirely.

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




/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function. */
template <StaticStr Name, typename T>
class Arg : public impl::ArgTag {

    template <bool positional, bool keyword>
    struct Optional : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = positional;
        static constexpr bool is_keyword = keyword;
        static constexpr bool is_optional = true;
        static constexpr bool is_variadic = false;

        T value;
    };

    template <bool optional>
    struct Positional : public impl::ArgTag {
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;
    };

    template <bool optional>
    struct Keyword : public impl::ArgTag {
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;
    };

    struct Args : public impl::ArgTag {
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
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::vector<type> value;

        Args() = default;
        Args(const std::vector<type>& val) : value(val) {}
        Args(std::vector<type>&& val) : value(std::move(val)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& val) {
            value.reserve(val.size());
            for (const auto& item : val) {
                value.push_back(item);
            }
        }
        Args(const Args& other) : value(other.value) {}
        Args(Args&& other) : value(std::move(other.value)) {}

        [[nodiscard]] auto begin() const { return value.begin(); }
        [[nodiscard]] auto cbegin() const { return value.cbegin(); }
        [[nodiscard]] auto end() const { return value.end(); }
        [[nodiscard]] auto cend() const { return value.cend(); }
        [[nodiscard]] auto rbegin() const { return value.rbegin(); }
        [[nodiscard]] auto crbegin() const { return value.crbegin(); }
        [[nodiscard]] auto rend() const { return value.rend(); }
        [[nodiscard]] auto crend() const { return value.crend(); }
        [[nodiscard]] constexpr auto size() const { return value.size(); }
        [[nodiscard]] constexpr auto empty() const { return value.empty(); }
        [[nodiscard]] constexpr auto data() const { return value.data(); }
        [[nodiscard]] constexpr decltype(auto) front() const { return value.front(); }
        [[nodiscard]] constexpr decltype(auto) back() const { return value.back(); }
        [[nodiscard]] constexpr decltype(auto) operator[](size_t index) const {
            return value.at(index);
        }
    };

    struct Kwargs : public impl::ArgTag {
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
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::unordered_map<std::string, T> value;

        Kwargs() = default;
        Kwargs(const std::unordered_map<std::string, type>& val) : value(val) {}
        Kwargs(std::unordered_map<std::string, type>&& val) : value(std::move(val)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& val) {
            value.reserve(val.size());
            for (const auto& [k, v] : val) {
                value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : value(other.value) {}
        Kwargs(Kwargs&& other) : value(std::move(other.value)) {}

        [[nodiscard]] auto begin() const { return value.begin(); }
        [[nodiscard]] auto cbegin() const { return value.cbegin(); }
        [[nodiscard]] auto end() const { return value.end(); }
        [[nodiscard]] auto cend() const { return value.cend(); }
        [[nodiscard]] constexpr auto size() const { return value.size(); }
        [[nodiscard]] constexpr bool empty() const { return value.empty(); }
        [[nodiscard]] constexpr bool contains(const std::string& key) const {
            return value.contains(key);
        }
        [[nodiscard]] constexpr auto count(const std::string& key) const {
            return value.count(key);
        }
        [[nodiscard]] decltype(auto) find(const std::string& key) const {
            return value.find(key);
        }
        [[nodiscard]] decltype(auto) operator[](const std::string& key) const {
            return value.at(key);
        }
    };

public:
    static_assert(Name != "", "Argument name cannot be an empty string.");

    using type = T;
    using pos = Positional<false>;
    using kw = Keyword<false>;
    using opt = Optional<true, true>;
    using args = Args;
    using kwargs = Kwargs;
    static constexpr StaticStr name = Name;
    static constexpr bool is_positional = true;
    static constexpr bool is_keyword = true;
    static constexpr bool is_optional = false;
    static constexpr bool is_variadic = false;

    T value;
};


namespace impl {

    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr auto operator=(T&& value) const {
            return Arg<name, T>{std::forward<T>(value)};
        }
    };

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


    /// TODO: The approach explored by PyFunctionBase could be a contender for
    /// automated template bindings, especially if I decouple all the Python __ready__
    /// calls to the point where they can be added to the module init function every
    /// time an exported template is instantiated anywhere in the code.  That would
    /// directly allow the use of C++ templates in Python.

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

        static void __dealloc__(PyFunctionBase* self) {
            self->name.~basic_string();
            self->docstring.~basic_string();
            type.tp_free(reinterpret_cast<PyObject*>(self));
        }

        static PyObject* __repr__(PyFunctionBase* self) {
            std::string s =  "<py::Function [template]>";
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


/* A compile-time factory for binding keyword arguments with Python syntax.  A
constexpr instance of this class can be used to provide even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::UnboundArg<name> arg {};


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
and `->` operators, or by accessing their `.value` member directly, which comprises
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
template <typename F = Object(
    Arg<"args", const Object&>::args,
    Arg<"kwargs", const Object&>::kwargs
)>
class Function : public Function<typename impl::GetSignature<F>::type> {};


template <typename Return, typename... Target>
class Function<Return(Target...)> : public Object, public impl::FunctionTag {
    using Base = Object;
    using Self = Function;

protected:

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

        /* Check whether the argument at index I is marked as optional. */
        template <size_t I> requires (I < size)
        static constexpr bool is_opt = Inspect<type<I>>::opt;

        /* Check whether the named argument is marked as optional. */
        template <StaticStr name> requires (index<name> < size)
        static constexpr bool is_opt_kw = Inspect<type<index<name>>>::opt;

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

        template <typename source, size_t... Is, typename Kwargs>
        static void assert_var_kwargs_are_recognized(
            std::index_sequence<Is...>,
            const Kwargs& kwargs
        ) {
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

    };

    using target = Signature<Target...>;
    static_assert(target::valid);

public:

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

    /* An interface to a std::tuple that contains owning references to default values
    for all of the optional arguments in the target signature. */
    class Defaults {

        template <size_t I>
        struct Value  {
            using annotation = target::template type<I>;
            using type = std::remove_cvref_t<typename Inspect<annotation>::type>;
            static constexpr StaticStr name = Inspect<annotation>::name;
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
            template <typename U> requires (Inspect<U>::opt)
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
                if constexpr (Inspect<Arg>::pos) {
                    if constexpr (!std::convertible_to<
                        typename Inspect<Arg>::type,
                        typename Value::type
                    >) {
                        return false;
                    }
                } else if constexpr (Inspect<Arg>::kw) {
                    if constexpr (
                        !target::template contains<Inspect<Arg>::name> ||
                        !source::template contains<Value::name>
                    ) {
                        return false;
                    } else {
                        constexpr size_t idx = index<Inspect<Arg>::name>;
                        using Match = std::tuple_element<idx, tuple>::type;
                        if constexpr (!std::convertible_to<
                            typename Inspect<Arg>::type,
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

protected:

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
                    impl::unpack_arg<source::kw_index + I>(std::forward<Source>(args)...)
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
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
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
                    (vec.push_back(impl::unpack_arg<I + Js>(std::forward<Source>(args)...)), ...);
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
            const Defaults& defaults,
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
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return impl::unpack_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
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
            const Defaults& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        ARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const Defaults& defaults,
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

        ARGS_UNPACK requires (Inspect<T<I>>::kwargs)
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
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
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
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
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
            const Defaults& defaults,
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
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
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
            const Defaults& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const Defaults& defaults,
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

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
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
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return impl::unpack_arg<idx>(std::forward<Source>(args)...);
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

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::args)
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

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
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
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const Defaults& defaults,
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
            const Defaults& defaults,
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
            const Defaults& defaults,
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
                array[J + 1] = release(as_object(
                    impl::unpack_arg<J>(std::forward<Source>(args)...)
                ));
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
                array[J + 1] = release(as_object(
                    impl::unpack_arg<J>(std::forward<Source>(args)...)
                ));
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

        template <size_t J> requires (Inspect<S<J>>::kwargs)
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

    /* A unique Python type to represent functions with this signature in Python. */
    class PyFunction {

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

        // TODO: I need to manually call __ready__ for each binding in the module
        // initialization function rather than __create__ so that I can properly
        // instantiate types for use in Python.  In the AST parser, this equates to
        // searching the AST for all instantiations of an exported template, and
        // emitting a __ready__() call for each one so that they are accessible from
        // Python.  This is better than readying the type the first time it is
        // constructed, since some templates may be instantiated without being
        // constructed.

        // TODO: also, the py::Type class should be moved into common/ and absorb
        // some of this responsibility.  For instance, it can offer a wrapper around
        // the __ready__() call that allows it to be called as part of the final
        // initialization, without exposing the PyTypeObject* directly to the user.

        static void __ready__() {
            static bool initialized = false;
            if (!initialized) {
                type.tp_base = &impl::PyFunctionBase::type;
                if (PyType_Ready(&type) < 0) {
                    Exception::from_python();
                }

                // TODO: ideally, this:
                // py::Tuple<py::Object> key{py::Type<Return>(), py::Type<Target>()...};
                // impl::PyFunctionBase::instances[key] = py::Type<Function>();

                initialized = true;
            }
        }

        static PyObject* __create__(
            std::string&& name,
            std::string&& docstring,
            std::function<Return(Target...)>&& func,
            Defaults&& defaults
        ) {
            PyFunction* self = PyObject_New(PyFunction, &type);
            if (self == nullptr) {
                return nullptr;
            }
            try {
                self->base.vectorcall = (vectorcallfunc) &__call__;
                new (&self->base.name) std::string(std::move(name));
                new (&self->base.docstring) std::string(std::move(docstring));
                new (&self->func) std::function(std::move(func));
                new (&self->defaults) Defaults(std::move(defaults));
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Py_DECREF(self);
                throw;
            }
        }

        static void __dealloc__(PyFunction* self) {
            self->func.~function<Return(Target...)>();
            self->defaults.~Defaults();
            type.tp_free(reinterpret_cast<PyObject*>(self));
        }

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            Py_ssize_t nargs,
            PyObject* kwnames
        ) {
            try {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyFunction* self,
                    PyObject* const* args,
                    Py_ssize_t nargs,
                    PyObject* kwnames
                ) {
                    size_t true_nargs = PyVectorcall_NARGS(nargs);
                    if constexpr (!target::has_args) {
                        constexpr size_t expected = target::size - target::kw_only_count;
                        if (true_nargs > expected) {
                            throw TypeError(
                                "expected at most " + std::to_string(expected) +
                                " positional arguments, but received " +
                                std::to_string(true_nargs)
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
                                true_nargs,
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
                                    true_nargs,
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
                    nargs,
                    kwnames
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

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
    using ReturnType = Return;
    using ArgTypes = std::tuple<typename Inspect<Target>::type...>;
    using Annotations = std::tuple<Target...>;

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

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // TODO: isinstance(), issubclass() should work via PyType_IsSubtype() since I
    // control the type object itself.

    // TODO: constructor should fail if the function type is a subclass of my root
    // function type, but not a subclass of this specific function type.  This
    // indicates a type mismatch in the function signature, which is a violation of
    // static type safety.  I can then print a helpful error message with the demangled
    // function types which can show their differences.
    // -> This does not occur for raw Python functions, which are always considered to
    // be compatible.  Perhaps in the future, I can analyze the type hints to determine
    // compatibility.

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

    // TODO: these can now be replaced with __init__ specializations?

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
        Base(PyFunction::__create__(
            "",
            "",
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        ), stolen_t{})
    {}

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
        Base(PyFunction::__create__(
            std::move(name),
            "",
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        ), stolen_t{})
    {}

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
        Base(PyFunction::__create__(
            std::move(name),
            std::move(doc),
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        ), stolen_t{})
    {}

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

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
                        target::assert_var_args_are_consumed(iter, end);
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
                        target::assert_var_args_are_consumed(iter, end);
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
                    target::assert_var_kwargs_are_recognized(
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
                    target::assert_var_kwargs_are_recognized(
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
                        target::assert_var_args_are_consumed(iter, end);
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
                        target::assert_var_args_are_consumed(iter, end);
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
    static Return invoke_py(Handle func, Source&&... args) {
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );

        // bypass the Python interpreter if the function is an instance of the coupled
        // function type, which guarantees that the template signatures exactly match
        if (PyType_IsSubtype(Py_TYPE(ptr(func)), &PyFunction::type)) {
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
            Handle func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            PyObject* result;

            // if there are no arguments, we can use the no-args protocol
            if constexpr (source::size == 0) {
                result = PyObject_CallNoArgs(ptr(func));

            // if there are no variadic arguments, we can stack allocate the argument
            // array with a fixed size
            } else if constexpr (!source::has_args && !source::has_kwargs) {
                // if there is only one argument, we can use the one-arg protocol
                if constexpr (source::size == 1) {
                    if constexpr (source::has_kw) {
                        result = PyObject_CallOneArg(
                            ptr(func),
                            ptr(as_object(
                                impl::unpack_arg<0>(std::forward<Source>(args)...).value
                            ))
                        );
                    } else {
                        result = PyObject_CallOneArg(
                            ptr(func),
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
                        ptr(func),
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
                    ptr(func),
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
                    ptr(func),
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
                    ptr(func),
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

    /* Call the function with the given arguments.  If the wrapped function is of the
    coupled Python type, then this will be translated into a raw C++ call, bypassing
    Python entirely. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        if constexpr (std::is_void_v<Return>) {
            invoke_py(m_ptr, std::forward<Source>(args)...);
        } else {
            return invoke_py(m_ptr, std::forward<Source>(args)...);
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
    //         if (PyFunction_SetAnnotations(self(), ptr(result))) {
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
    //     PyObject* descriptor = PyDescr_NewMethod(ptr(type), contents->method_def);
    //     if (descriptor == nullptr) {
    //         Exception::from_python();
    //     }
    //     int rc = PyObject_SetAttrString(ptr(type), contents->name.data(), descriptor);
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


// TODO: if default specialization is given, type checks should be fully generic, right?
// issubclass<T, Function<>>() should check impl::is_callable_any<T>;

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


/* Construct a py::Function from a valid C++ function with the templated signature.
Use CTAD to deduce the signature if not explicitly provided.  If the signature
contains default value annotations, they must be specified here. */
template <typename Return, typename... Target, typename Func, typename... Values>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Func&& func, Values&&... defaults) {
        // PyObject* result = py::Type<type>::__python__::__create__(  // TODO: eventually this should work
        //     "",
        //     "",
        //     std::function(std::forward<Func>(func)),
        //     typename type::Defaults(std::forward<Values>(defaults)...)
        // );
        PyObject* result = type::PyFunction::__create__(
            "",
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        );
        if (result == nullptr) {
            throw Exception{};
        }
        return reinterpret_steal<type>(result);
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
        auto meth = reinterpret_steal<Object>(
            PyObject_GetAttr(ptr(self), TemplateString<name>::ptr)
        );
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
    template <typename Self, StaticStr name, typename... Args>
        requires (invocable<std::decay_t<Self>, name, Args...>)
    inline decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, name>::type;
        auto meth = reinterpret_steal<Object>(
            PyObject_GetAttr(ptr(Self::type), TemplateString<name>::ptr)
        );
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


}  // namespace py


#endif
