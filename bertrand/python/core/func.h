#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "access.h"
#include "iter.h"


/// NOTE: Beware all ye who enter here, for this is the point of no return!
/// Functions require incredibly detailed compile-time and runtime logic over
/// the cross product of possible C++ and Python paradigms, including (but not
/// limited to):
///
///     Static vs dynamic typing
///     Compile-time vs runtime introspection + validation
///     Variadic + keyword arguments
///     Partial function application
///     Function chaining
///     Function overloading
///     Inline caching
///     Asynchronous execution (NYI)
///     Descriptor protocol
///     Structural typing
///     Performance + memory optimizations
///     Extensability
///
/// All of this serves as bedrock for the rest of Bertrand's core features, and
/// must be defined very early in the dependency chain, before any conveniences
/// that would simplify the logic.  It therefore requires heavy use of the
/// (unsafe) CPython API, which is both highly verbose and error-prone, as well
/// as extreme amounts of template metaprogramming.  If any of this scares you
/// (as it should), then turn back now while you still can!


namespace bertrand {


/* Introspect an annotated C++ function signature to extract compile-time type
information about its parameters and allow matching functions to be called safely from
both languages with a consistent syntax.  Also defines supporting data structures to
allow for dynamic function overloading and partial function application. */
template <typename T>
struct signature;


/* Call operator for all `py::Object` types. */
template <typename Self, typename... Args>
    requires (
        __call__<Self, Args...>::enable &&
        std::convertible_to<typename __call__<Self, Args...>::type, Object> && (
            std::is_invocable_r_v<
                typename __call__<Self, Args...>::type,
                __call__<Self, Args...>,
                Self,
                Args...
            > || (
                !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                meta::has_cpp<Self> &&
                std::is_invocable_r_v<
                    typename __call__<Self, Args...>::type,
                    meta::cpp_type<Self>,
                    Args...
                >
            ) || (
                !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __call__<Self, Args...>::type, Object> &&
                __getattr__<Self, "__call__">::enable &&
                meta::inherits<typename __getattr__<Self, "__call__">::type, impl::FunctionTag>
            )
        )
    )
decltype(auto) Object::operator()(this Self&& self, Args&&... args) {
    if constexpr (std::is_invocable_v<__call__<Self, Args...>, Self, Args...>) {
        return __call__<Self, Args...>{}(
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );

    } else if constexpr (meta::has_cpp<Self>) {
        return from_python(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    } else {
        return getattr<"__call__">(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    }
}


/* The dereference operator is used to emulate Python container unpacking when calling
a Python-style function from C++.

A single unpacking operator passes the contents of an iterable container as positional
arguments to a function.  Unlike Python, only one such operator is allowed per call,
and it must be the last positional argument in the parameter list.  This allows the
compiler to ensure that the container's value type is minimally convertible to each of
the remaining positional arguments ahead of time, even though the number of arguments
cannot be determined until runtime.  Thus, if any arguments are missing or extras are
provided, the call will raise an exception similar to Python, rather than failing
statically at compile time.  This can be avoided by using standard positional and
keyword arguments instead, which can be fully verified at compile time, or by including
variadic positional arguments in the function signature, which will consume any
remaining arguments according to Python semantics.

A second unpacking operator promotes the arguments into keywords, and can only be used
if the container is mapping-like, meaning it possess both `::key_type` and
`::mapped_type` aliases, and that indexing it with an instance of the key type returns
a value of the mapped type.  The actual unpacking is robust, and will attempt to use
iterators over the container to produce key-value pairs, either directly through
`begin()` and `end()` or by calling the `.items()` method if present, followed by
zipping `.keys()` and `.values()` if both exist, and finally by iterating over the keys
and indexing into the container.  Similar to the positional unpacking operator, only
one of these may be present as the last keyword argument in the parameter list, and a
compile-time check is made to ensure that the mapped type is convertible to any missing
keyword arguments that are not explicitly provided at the call site.

In both cases, the extra runtime complexity results in a small performance degradation
over a typical function call, which is minimized as much as possible. */
template <meta::inherits<Object> Self> requires (meta::iterable<Self>)
[[nodiscard]] auto operator*(Self&& self) {
    return arg_pack<Self>{std::forward<Self>(self)};
}


namespace impl {

    /// TODO: determine whether I can eliminate the get_parameter_name() function.
    /// It is only strictly needed for the `bertrand.Arg[]` indexing operator, which
    /// may actually be covered by JIT compilation + C++ template constraints on the
    /// Python side.  If so, then there should be no situation where a parameter name
    /// can be invalid.

    /* Validate a C++ string that represents an argument name, throwing an error if it
    does not conform to Python naming conventions. */
    inline std::string_view get_parameter_name(std::string_view str) {
        std::string_view sub = str.substr(
            str.starts_with("*") +
            str.starts_with("**")
        );
        if (sub.empty()) {
            throw TypeError("argument name cannot be empty");
        } else if (std::isdigit(sub.front())) {
            throw TypeError(
                "argument name cannot start with a number: '" +
                std::string(sub) + "'"
            );
        }
        for (const char c : sub) {
            if (std::isalnum(c) || c == '_') {
                continue;
            }
            throw TypeError(
                "argument name must only contain alphanumerics and underscores: '" +
                std::string(sub) + "'"
            );
        }
        return str;
    }

    /* Validate a Python string that represents an argument name, throwing an error if
    it does not conform to Python naming conventions, and otherwise returning the name
    as a C++ string_view. */
    inline std::string_view get_parameter_name(PyObject* str) {
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(str, &len);
        if (data == nullptr) {
            Exception::from_python();
        }
        return get_parameter_name({data, static_cast<size_t>(len)});
    }

    template <typename Hash = FNV1a>
    inline size_t parameter_hash(
        const char* name,
        PyObject* value,
        impl::ArgKind kind
    ) noexcept {
        return impl::hash_combine(
            Hash{}(name),
            PyType_Check(value) ?
                reinterpret_cast<size_t>(value) :
                reinterpret_cast<size_t>(Py_TYPE(value)),
            static_cast<size_t>(kind)
        );
    }

    template <typename F>
    concept normalized_signature =
        signature<F>::enable &&
        std::same_as<std::remove_cvref_t<F>, typename signature<F>::type>;

    struct SignatureTag : BertrandTag {};
    struct SignaturePartialTag : BertrandTag {};
    struct SignatureDefaultsTag : BertrandTag {};
    struct SignatureBindTag : BertrandTag {};
    struct SignatureVectorcallTag : BertrandTag {};
    struct SignatureOverloadsTag : BertrandTag {};
    struct DefTag {};

    template <typename R>
    struct SignatureBase : SignatureTag {
        using Return = R;
    };

    /* A standardized helper type that represents the arguments used to initiate a
    search over an overload trie.  This must be a separate type in order to allow keys
    from one specialization of `Signature` to be used to search for overloads on a
    different specialization of `Signature`.  Otherwise, they would all refer to
    separate types, and would not be able to communicate. */
    struct OverloadKey {
        struct Argument;

    private:
        using Vec = std::vector<Argument>;

    public:
        struct Argument {
            std::string name;
            Object value;
            impl::ArgKind kind;
        };

        Vec vec;
        size_t hash = 0;
        bool has_hash = false;

        Vec& operator*() noexcept { return vec; }
        const Vec& operator*() const noexcept { return vec; }
        Vec* operator->() noexcept { return &vec; }
        const Vec* operator->() const noexcept { return &vec; }
    };

}


/// TODO: inspect.signature() intentionally leaves out partial arguments, so it might
/// not be a bad idea to do the same here.  The problem is that I need the full
/// signature to instantiate templates on both sides of the language divide, including
/// partials, so there has to be some way to satisfy both requirements.  Perhaps what
/// I can do is encode the full signature into __partial__, and then synthesize them
/// in some way here?  So `__signature__` would yield the signature without any
/// partials, but if a `__partial__` attribute is present, then it would merge them
/// together when constructing the final signature.  This should allow me to represent
/// both raw Python objects and my custom partials at the same time, although the logic
/// gets really squirrely really fast.


/* Inspect a Python function object and extract its signature so that it can be
analyzed from C++.

This class works just like the `inspect.signature()` function in Python, with extra
logic for normalizing type hints and handling partial functions.  It also houses a
static callback table that can be extended from C++ to handle custom type annotations.
Partial arguments are inferred by searching for a `__partial__` attribute on the
function object, which should be a tuple of (name, value) pairs in the same order as
the function's parameters, or `None` if no partials are present.  Alternatively, if a
`__self__` attribute is present, then it will be used to bind the first parameter of
the function as if it were an instance method.

Instances of this type are stored as keys within a function's overload trie, in order
to uniquely identify all possible signatures, and check for conflicts between them.  It
is somewhat expensive to construct, and is only necessary when dealing with
dynamically-typed Python functions, whose signatures cannot be known ahead of time.
For C++ functions where the signature is encoded at compile time, the `py::signature`
class should be used instead, which is much more detailed and eliminates runtime
overhead. */
struct inspect {
    using Required = bitset<MAX_ARGS>;

    struct Param;
    struct Callback;

private:

    static Object import_bertrand() {
        PyObject* bertrand = PyImport_Import(
            ptr(impl::template_string<"bertrand">())
        );
        if (bertrand == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(bertrand);
    }

    static Object import_inspect() {
        PyObject* inspect = PyImport_Import(
            ptr(impl::template_string<"inspect">())
        );
        if (inspect == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(inspect);
    }

    static Object import_typing() {
        PyObject* typing = PyImport_Import(
            ptr(impl::template_string<"typing">())
        );
        if (typing == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(typing);
    }

    static Object import_types() {
        PyObject* types = PyImport_Import(
            ptr(impl::template_string<"types">())
        );
        if (types == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(types);
    }

    Object get_name() {
        Object str = impl::template_string<"__name__">();
        if (PyObject_HasAttr(ptr(m_func), ptr(str))) {
            PyObject* name = PyObject_GetAttr(ptr(m_func), ptr(str));
            if (name == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(name);
        }
        return impl::template_string<"">();
    }

    struct init_imports {
        Object inspect = import_inspect();
        Object typing = import_typing();
        Object Parameter = getattr<"Parameter">(inspect);
        Object empty = getattr<"empty">(Parameter);
        Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
        Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
        Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
        Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);
        Object VAR_KEYWORD = getattr<"VAR_KEYWORD">(Parameter);
    };

    struct init_partials {
        Object partials;
        Object self;
        size_t idx;
        size_t size;

        init_partials(Object& m_func) :
            partials(getattr<"__partial__">(m_func, None)),
            self(getattr<"__self__">(
                m_func,
                reinterpret_steal<Object>(nullptr)
            )),
            idx(0),
            size(0)
        {
            if (!partials.is(None)) {
                if (!PyTuple_Check(ptr(partials))) {
                    throw TypeError(
                        "__partial__ attribute must be a tuple of (name, "
                        "value) pairs"
                    );
                }
                size = PyTuple_GET_SIZE(ptr(partials));

            } else if (!self.is(nullptr)) {
                /// NOTE: the `__self__` parameter would ordinarily not show up in the
                /// inspect.signature() call, so it needs to be removed and added back in
                /// manually.
                m_func = getattr<"__func__">(self, m_func);
            }
        }

        static bool is_positional(PyObject* pair) {
            if (
                !PyTuple_Check(pair) ||
                PyTuple_GET_SIZE(pair) != 2 ||
                !PyUnicode_Check(PyTuple_GET_ITEM(pair, 0))
            ) {
                throw TypeError(
                    "__partial__ attribute must be a tuple of (name, value) pairs"
                );
            }
            int rc = PyObject_Not(PyTuple_GET_ITEM(pair, 0));
            if (rc < 0) {
                Exception::from_python();
            }
            return rc;
        }
    };

    struct init_signature {
        Object signature;
        Object parameters;
        Py_ssize_t size;
        Object hints;

        init_signature(init_imports& ctx, Object& m_func) :
            signature(getattr<"signature">(ctx.inspect)(m_func)),
            parameters(getattr<"values">(getattr<"parameters">(signature))()),
            size([](const Object& parameters) {
                Py_ssize_t result = PyObject_Length(ptr(parameters));
                if (result < 0) {
                    Exception::from_python();
                } else if (result > Required::size()) {
                    throw ValueError(
                        "bertrand functions are limited to " +
                        std::to_string(Required::size()) +
                        " parameters (received: " +  std::to_string(result) + ")"
                    );
                }
                return result;
            }(this->parameters)),
            hints(getattr<"get_type_hints">(ctx.typing)(
                m_func,
                arg<"include_extras"> = reinterpret_borrow<Object>(Py_True)
            ))
        {}
    };

    struct init_parameter {
        Object unicode;
        std::string_view name;
        Object annotation;
        Object default_value;
        Object partial;
        impl::ArgKind kind;

        init_parameter(
            init_imports& ctx,
            init_partials& parts,
            init_signature& sig,
            size_t idx,
            Object& param
        ) :
            unicode(getattr<"name">(param)),
            name([](PyObject* unicode) {
                Py_ssize_t size;
                const char* name = PyUnicode_AsUTF8AndSize(unicode, &size);
                if (name == nullptr) {
                    Exception::from_python();
                }
                return std::string_view(name, size);
            }(ptr(unicode))),
            annotation([](init_imports& ctx, init_signature& sig, PyObject* unicode) {
                Object result = reinterpret_steal<Object>(PyDict_GetItem(
                    ptr(sig.hints),
                    unicode
                ));
                if (result.is(nullptr)) {
                    result = ctx.empty;
                }
                return parse(result);
            }(ctx, sig, ptr(unicode))),
            default_value(getattr<"default">(param)),
            partial(reinterpret_steal<Object>(nullptr)),
            kind([&] {
                Object py_kind = getattr<"kind">(param);
                if (py_kind.is(ctx.POSITIONAL_ONLY)) {
                    partial = partial_posonly(ctx, parts, idx);
                    return default_value.is(ctx.empty) ?
                        impl::ArgKind::POS :
                        impl::ArgKind::OPT | impl::ArgKind::POS;
                }
                if (py_kind.is(ctx.POSITIONAL_OR_KEYWORD)) {
                    partial = partial_pos_or_kw(
                        ctx,
                        parts,
                        idx,
                        ptr(unicode)
                    );
                    return default_value.is(ctx.empty) ?
                        impl::ArgKind::POS | impl::ArgKind::KW :
                        impl::ArgKind::OPT | impl::ArgKind::POS | impl::ArgKind::KW;
                }
                if (py_kind.is(ctx.KEYWORD_ONLY)) {
                    partial = partial_kwonly(
                        ctx,
                        parts,
                        ptr(unicode)
                    );
                    return default_value.is(ctx.empty) ?
                        impl::ArgKind::KW :
                        impl::ArgKind::OPT | impl::ArgKind::KW;
                }
                if (py_kind.is(ctx.VAR_POSITIONAL)) {
                    partial = partial_args(ctx, parts);
                    return impl::ArgKind::VAR | impl::ArgKind::POS;
                }
                if (py_kind.is(ctx.VAR_KEYWORD)) {
                    partial = partial_kwargs(ctx, parts);
                    return impl::ArgKind::VAR | impl::ArgKind::KW;
                }
                throw TypeError("unrecognized parameter kind: " + repr(kind));
            }())
        {}

        static Object partial_posonly(
            init_imports& ctx,
            init_partials& parts,
            size_t idx
        ) {
            if (!parts.partials.is(None) && parts.idx < parts.size) {
                PyObject* pair = PyTuple_GET_ITEM(ptr(parts.partials), parts.idx);
                if (parts.is_positional(pair)) {
                    ++parts.idx;
                    return reinterpret_borrow<Object>(pair);
                }

            } else if (idx == 0 && !parts.self.is(nullptr)) {
                Object result = reinterpret_steal<Object>(PyTuple_Pack(
                    2,
                    ptr(impl::template_string<"">()),
                    ptr(parts.self)
                ));
                if (result.is(nullptr)) {
                    Exception::from_python();
                }
                return result;
            }

            return reinterpret_steal<Object>(nullptr);
        }

        static Object partial_pos_or_kw(
            init_imports& ctx,
            init_partials& parts,
            size_t idx,
            PyObject* unicode
        ) {
            if (!parts.partials.is(None) && parts.idx < parts.size) {
                PyObject* pair = PyTuple_GET_ITEM(ptr(parts.partials), parts.idx);
                if (parts.is_positional(pair)) {
                    ++parts.idx;
                    return reinterpret_borrow<Object>(pair);
                }
                int rc = PyObject_RichCompareBool(
                    PyTuple_GET_ITEM(pair, 0),
                    unicode,
                    Py_EQ
                );
                if (rc < 0) {
                    Exception::from_python();
                }
                if (rc) {
                    ++parts.idx;
                    return reinterpret_borrow<Object>(pair);
                }

            } else if (idx == 0 && !parts.self.is(nullptr)) {
                Object result = reinterpret_steal<Object>(PyTuple_Pack(
                    2,
                    ptr(impl::template_string<"">()),
                    ptr(parts.self)
                ));
                if (result.is(nullptr)) {
                    Exception::from_python();
                }
                return result;
            }

            return reinterpret_steal<Object>(nullptr);
        }

        static Object partial_kwonly(
            init_imports& ctx,
            init_partials& parts,
            PyObject* unicode
        ) {
            if (!parts.partials.is(None) && parts.idx < parts.size) {
                PyObject* pair = PyTuple_GET_ITEM(ptr(parts.partials), parts.idx);

                if (!parts.is_positional(pair)) {
                    int rc = PyObject_RichCompareBool(
                        PyTuple_GET_ITEM(pair, 0),
                        unicode,
                        Py_EQ
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    }
                    if (rc) {
                        ++parts.idx;
                        return reinterpret_borrow<Object>(pair);
                    }
                }
            }

            return reinterpret_steal<Object>(nullptr);
        }

        static Object partial_args(
            init_imports& ctx,
            init_partials& parts
        ) {
            if (!parts.partials.is(None) && parts.idx < parts.size) {
                Object out = reinterpret_steal<Object>(nullptr);

                while (parts.idx < parts.size) {
                    PyObject* pair = PyTuple_GET_ITEM(ptr(parts.partials), parts.idx);
                    if (parts.is_positional(pair)) {
                        if (out.is(nullptr)) {
                            out = reinterpret_steal<Object>(PyList_New(1));
                            if (out.is(nullptr)) {
                                Exception::from_python();
                            }
                            PyList_SET_ITEM(ptr(out), 0, Py_NewRef(pair));
                        } else {
                            if (PyList_Append(ptr(out), pair)) {
                                Exception::from_python();
                            }
                        }
                        ++parts.idx;
                    } else {
                        break;  // stop at first keyword arg
                    }
                }

                if (!out.is(nullptr)) {
                    return reinterpret_steal<Object>(PyList_AsTuple(ptr(out)));
                }
            }

            return reinterpret_steal<Object>(nullptr);
        }

        static Object partial_kwargs(
            init_imports& ctx,
            init_partials& parts
        ) {
            if (!parts.partials.is(None) && parts.idx < parts.size) {
                Object out = reinterpret_steal<Object>(nullptr);

                while (parts.idx < parts.size) {
                    PyObject* pair = PyTuple_GET_ITEM(ptr(parts.partials), parts.idx);
                    if (!parts.is_positional(pair)) {
                        if (out.is(nullptr)) {
                            out = reinterpret_steal<Object>(PyList_New(1));
                            if (out.is(nullptr)) {
                                Exception::from_python();
                            }
                            PyList_SET_ITEM(ptr(out), 0, Py_NewRef(pair));
                        } else {
                            if (PyList_Append(ptr(out), pair)) {
                                Exception::from_python();
                            }
                        }
                        ++parts.idx;
                    }
                }

                if (!out.is(nullptr)) {
                    return reinterpret_steal<Object>(PyList_AsTuple(ptr(out)));
                }
            }

            return reinterpret_steal<Object>(nullptr);
        }
    };

    Object initialize() {
        init_imports ctx;
        init_partials parts(m_func);
        init_signature sig(ctx, m_func);

        // allocate new parameters tuple + parameter array + name map
        Object new_params = reinterpret_steal<Object>(PyTuple_New(sig.size));
        if (new_params.is(nullptr)) {
            Exception::from_python();
        }
        m_parameters.reserve(sig.size);
        m_names.reserve(sig.size);

        // parse each parameter
        size_t idx = 0;
        for (Object param : sig.parameters) {
            init_parameter parsed(ctx, parts, sig, idx, param);
            if (
                m_first_keyword == std::numeric_limits<size_t>::max() &&
                (parsed.kind.kw() || parsed.kind.kwargs())
            ) {
                m_first_keyword = idx;
            }

            // insert parsed parameter + update name map, hash, and required bitmask
            m_parameters.emplace_back(
                std::move(parsed.name),
                std::move(parsed.annotation),
                std::move(parsed.default_value),
                std::move(parsed.partial),
                idx,
                parsed.kind
            );
            m_names.emplace(&m_parameters.back());
            m_hash = impl::hash_combine(m_hash, m_parameters.back().hash());
            m_required <<= 1;
            m_required |= !(parsed.kind.opt() | parsed.kind.variadic());

            // insert into reconstructed parameters tuple
            PyTuple_SET_ITEM(
                ptr(new_params),
                idx++,
                release(getattr<"replace">(param)(
                    arg<"annotation"> = parsed.annotation
                ))
            );
        }
        if (parts.idx != parts.size) {
            throw TypeError(
                "invalid partial arguments provided for function: " +
                repr(parts.partials)
            );
        }
        if (m_first_keyword == std::numeric_limits<size_t>::max()) {
            m_first_keyword = m_parameters.size();
        }

        // normalize return annotation
        Object new_return = reinterpret_steal<Object>(PyDict_GetItem(
            ptr(sig.hints),
            ptr(impl::template_string<"return">())
        ));
        if (new_return.is(nullptr)) {
            new_return = ctx.empty;
        }
        m_return_annotation = parse(new_return);
        m_hash = impl::hash_combine(
            m_hash,
            PyType_Check(ptr(m_return_annotation)) ?
                reinterpret_cast<size_t>(ptr(m_return_annotation)) :
                reinterpret_cast<size_t>(Py_TYPE(ptr(m_return_annotation)))
        );

        // replace the original parameters with the newly-normalized ones
        return getattr<"replace">(sig.signature)(
            arg<"return_annotation"> = m_return_annotation,
            arg<"parameters"> = new_params
        );
    }

    struct Hash {
        using is_transparent = void;
        static size_t operator()(const Param* param) noexcept {
            return std::hash<std::string_view>{}(param->name);
        }
        static size_t operator()(std::string_view name) noexcept {
            return std::hash<std::string_view>{}(name);
        }
    };

    struct Equal {
        using is_transparent = void;
        static bool operator()(const Param* lhs, const Param* rhs) noexcept {
            return lhs->name == rhs->name;
        }
        static bool operator()(const Param* lhs, std::string_view rhs) noexcept {
            return lhs->name == rhs;
        }
        static bool operator()(std::string_view lhs, const Param* rhs) noexcept {
            return lhs == rhs->name;
        }
    };

    Object m_func;
    Object m_name = get_name();
    size_t m_hash = 0;
    Required m_required = 0;
    size_t m_first_keyword = std::numeric_limits<size_t>::max();
    std::vector<Param> m_parameters;
    std::unordered_set<const Param*, Hash, Equal> m_names;
    Object m_return_annotation = reinterpret_steal<Object>(nullptr);
    Object m_signature = initialize();
    mutable Object m_key = reinterpret_steal<Object>(nullptr);

public:
    explicit inspect(const Object& func) : m_func(func) {}
    explicit inspect(Object&& func) : m_func(std::move(func)) {}

    explicit inspect(const Object& func, std::string_view name) :
        m_func(func),
        m_name([](std::string_view name) {
            PyObject* str = PyUnicode_FromStringAndSize(name.data(), name.size());
            if (str == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(str);
        }(name))
    {}

    explicit inspect(Object&& func, std::string_view name) :
        m_func(std::move(func)),
        m_name([](std::string_view name) {
            PyObject* str = PyUnicode_FromStringAndSize(name.data(), name.size());
            if (str == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(str);
        }(name))
    {}

    inspect(const inspect& other) :
        m_func(other.m_func),
        m_name(other.m_name),
        m_hash(other.m_hash),
        m_required(other.m_required),
        m_first_keyword(other.m_first_keyword),
        m_parameters(other.m_parameters),
        m_return_annotation(other.m_return_annotation),
        m_signature(other.m_signature),
        m_key(other.m_key)
    {
        m_names.reserve(other.m_names.size());
        for (const Param& param : m_parameters) {
            m_names.emplace(&param);
        }
    }

    inspect& operator=(const inspect& other) {
        if (this != &other) {
            m_func = other.m_func;
            m_name = other.m_name;
            m_hash = other.m_hash;
            m_required = other.m_required;
            m_first_keyword = other.m_first_keyword;
            m_parameters = other.m_parameters;
            m_names.clear();
            m_names.reserve(other.m_names.size());
            for (const Param& param : m_parameters) {
                m_names.emplace(&param);
            }
            m_return_annotation = other.m_return_annotation;
            m_signature = other.m_signature;
            m_key = other.m_key;
        }
        return *this;
    }

    inspect(inspect&& other) noexcept = default;
    inspect& operator=(inspect&& other) = default;

    /* Get a reference to the function being inspected. */
    [[nodiscard]] const Object& function() const noexcept {
        return m_func;
    }

    /* Get the name of the function by introspecting its `__name__` attribute, if it
    has one or by using an explicit name that was provided to the constructor.
    Otherwise, returns an empty string. */
    [[nodiscard]] std::string_view name() const {
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(
            ptr(m_name),
            &size
        );
        if (data == nullptr) {
            Exception::from_python();
        }
        return {data, static_cast<size_t>(size)};
    }

    /* Estimate the total memory consumption of the signature in bytes.  Note that this
    does not include any memory held by Python, including for the `inspect.Signature`
    instance that backs this object. */
    [[nodiscard]] size_t memory_usage() const noexcept {
        size_t total = sizeof(inspect);
        total += m_parameters.size() * sizeof(Param);
        total += m_names.size() * sizeof(const Param*);
        return total;
    }

    /* Get a reference to the normalized `inspect.Signature` instance that was obtained
    from the function.  Note that any inline type annotations will be passed through
    `typing.get_type_hints(include_extras=True)`, and then parsed according to the
    `parse()` helper within this class.  This means that stringized annotations (e.g.
    `from future import __annotations__`), forward references, and future PEP formats
    will be resolved and passed through the callback map to obtain proper bertrand
    types, which can be enforced at runtime. */
    [[nodiscard]] const Object& signature() const noexcept {
        return m_signature;
    }

    /* Get a reference to the normalized parameter array. */
    [[nodiscard]] const std::vector<Param>& parameters() const noexcept {
        return m_parameters;
    }

    /* Get a reference to the normalized return annotation, which is parsed using the
    same callbacks as any other parameter annotation. */
    [[nodiscard]] const Object& return_annotation() const noexcept {
        return m_return_annotation;
    }

    /* Return a unique hash associated with this signature, under which a matching
    overload will be registered.  This combines the name, annotation, and kind
    (positional, keyword, optional, variadic, etc.) of each parameter, as well as
    the return type in order to quickly identify unique overloads. */
    [[nodiscard]] size_t hash() const noexcept {
        return m_hash;
    }

    /* Return a bitmask encoding the positions of all of the required arguments within
    the signature, for easy comparison during overload resolution. */
    [[nodiscard]] const Required& required() const noexcept {
        return m_required;
    }

    /* Get the index of the first keyword argument in the signature, or the size of the
    signature if there are no keyword arguments.  This excludes positional-only and
    variadic positional arguments, but includes positional-or-keyword, keyword-only,
    and variadic keyword arguments. */
    [[nodiscard]] size_t first_keyword() const noexcept {
        return m_first_keyword;
    }

    /* A lightweight representation of a single parameter in the signature,
    analogous to an `inspect.Parameter` instance in Python. */
    struct Param {
        std::string_view name;
        Object type;
        Object default_value;
        Object partial;
        size_t index;
        impl::ArgKind kind;

        [[nodiscard]] bool posonly() const noexcept { return kind.posonly(); }
        [[nodiscard]] bool pos() const noexcept { return kind.pos(); }
        [[nodiscard]] bool args() const noexcept { return kind.args(); }
        [[nodiscard]] bool kwonly() const noexcept { return kind.kwonly(); }
        [[nodiscard]] bool kw() const noexcept { return kind.kw(); }
        [[nodiscard]] bool kwargs() const noexcept { return kind.kwargs(); }
        [[nodiscard]] bool opt() const noexcept { return kind.opt(); }
        [[nodiscard]] bool bound() const noexcept { return !partial.is(nullptr); }
        [[nodiscard]] bool variadic() const noexcept { return kind.variadic(); }

        [[nodiscard]] size_t hash() const noexcept {
            return impl::parameter_hash(
                name.data(),
                ptr(type),
                kind
            );
        }
    };

    /* Get a pointer to the parameter at index i.  Returns a null pointer if the index
    is out of range. */
    [[nodiscard]] const Param* operator[](size_t i) const {
        return i < m_parameters.size() ? &m_parameters[i] : nullptr;
    }

    /* Get a pointer to the named parameter.  Returns a null pointer if the parameter
    is not present. */
    [[nodiscard]] const Param* operator[](std::string_view name) const {
        auto it = m_names.find(name);
        return it != m_names.end() ? *it : nullptr;
    }

    /* Get a pointer to the parameter at index i.  Raises an IndexError if the index
    is out of range. */
    [[nodiscard]] const Param& get(size_t i) const {
        if (i < m_parameters.size()) {
            return m_parameters[i];
        } else {
            throw IndexError("parameter index out of range: " + std::to_string(i));
        }
    }

    /* Look up a specific parameter by name.  Returns a null pointer if the named
    parameter is not present. */
    [[nodiscard]] const Param& get(std::string_view name) const {
        auto it = m_names.find(name);
        if (it != m_names.end()) {
            return **it;
        } else {
            throw KeyError("parameter not found: " + std::string(name));
        }
    }

    /* Check whether the given index is within range of the signature's parameter
    list. */
    [[nodiscard]] bool contains(size_t i) const {
        return i < m_parameters.size();
    }

    /* Check whether a given parameter name is present in the signature. */
    [[nodiscard]] bool contains(std::string_view name) const {
        return m_names.contains(name);
    }

    [[nodiscard]] auto size() const { return m_parameters.size(); }
    [[nodiscard]] auto empty() const { return m_parameters.empty(); }
    [[nodiscard]] auto begin() const { return m_parameters.begin(); }
    [[nodiscard]] auto cbegin() const { return m_parameters.cbegin(); }
    [[nodiscard]] auto rbegin() const { return m_parameters.rbegin(); }
    [[nodiscard]] auto crbegin() const { return m_parameters.crbegin(); }
    [[nodiscard]] auto end() const { return m_parameters.end(); }
    [[nodiscard]] auto cend() const { return m_parameters.cend(); }
    [[nodiscard]] auto rend() const { return m_parameters.rend(); }
    [[nodiscard]] auto crend() const { return m_parameters.crend(); }

    /* Parse a Python-style type hint by linearly searching the callback map,
    converting the hint into a uniform object that can be used as the target of an
    `isinstance()` or `issubclass()` check.  The search stops at the first callback
    that returns true, recurring if necessary for instances of `typing.Annotated`,
    union types, etc.  If no callbacks match the hint, then it is returned as-is
    and assumed to implement the required operators. */
    [[nodiscard]] static Object parse(Object hint) {
        std::vector<Object> keys;
        parse(hint, keys);

        auto it = keys.begin();
        auto end = keys.end();
        while (it != end) {
            bool duplicate = false;
            auto it2 = it;
            while (++it2 != end) {
                if (*it2 == *it) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                it = keys.erase(it);
            } else {
                ++it;
            }
        }

        if (keys.empty()) {
            return reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(Py_TYPE(Py_None))
            );
        } else if (keys.size() == 1) {
            return std::move(keys.back());
        } else {
            Object bertrand = import_bertrand();
            Object key = reinterpret_steal<Object>(
                PyTuple_New(keys.size())
            );
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            size_t i = 0;
            for (Object& type : keys) {
                PyTuple_SET_ITEM(ptr(key), i++, release(type));
            }
            Object specialization = reinterpret_steal<Object>(PyObject_GetItem(
                ptr(getattr<"Union">(bertrand)),
                ptr(key)
            ));
            if (specialization.is(nullptr)) {
                Exception::from_python();
            }
            return specialization;
        }
    }

    /* Parse a Python-style type hint in-place, inserting the constituent types
    into the output vector.  This is intended to be called from within the callback
    mechanism itself, in order to trigger recursion for composite hints, such as
    aliases, unions, etc. */
    static void parse(Object hint, std::vector<Object>& out) {
        for (const Callback& cb : callbacks) {
            if (cb(hint, out)) {
                return;
            }
        }
        Object typing = import_typing();
        Object origin = getattr<"get_origin">(typing)(hint);
        if (origin.is(getattr<"Annotated">(typing))) {
            parse(reinterpret_borrow<Object>(PyTuple_GET_ITEM(
                ptr(getattr<"get_args">(typing)(hint)),
                0
            )), out);
            return;
        }
        out.emplace_back(std::move(hint));
    }

    /* A callback function to use when parsing inline type hints within a Python
    function declaration. */
    struct Callback {
        using Func = std::function<bool(Object, std::vector<Object>&)>;
        std::string id;
        Func func;
        bool operator()(const Object& hint, std::vector<Object>& out) const {
            return func(hint, out);
        }
    };

    /* An extendable series of callback functions that are used to parse
    Python-style type hints, normalizing them into a format that can be used during
    overload resolution and type safety checks.

    Each callback is tested in order and expected to return true if it can handle
    the hint, in which case the search terminates and the final state of the `out`
    vector will be converted into a normalized hint.  If the vector is empty, then
    the normalized hint will be `NoneType`, indicating a void function.  If the
    vector contains only a single unique item, then it will be returned directly.
    Otherwise, the vector will be converted into a `bertrand.Union` type, which is
    parametrized by the unique types in the vector, in the same order that they
    were added.

    Note that `typing.get_type_hints(include_extras=True)` is used to extract the
    type hints from the function signature, meaning that stringized annotations and
    forward references will be resolved before any callbacks are invoked.  The
    `include_extras` flag is used to ensure that `typing.Annotated` hints are
    preserved, so that they can be interpreted by the callback map if necessary.
    The default behavior in this case is to simply extract the underlying type and
    recur if no callbacks match the annotated type.  Custom callbacks can be registered
    to interpret these annotations if needed.
    
    If no callbacks match a particular hint, then the default behavior is to simply add
    it anyways, assuming that it is a valid type.  Any other behavior must be added by
    appending a callback for that case.  A final callback that always returns true
    would completely replace the default behavior, for instance. */
    inline static std::deque<Callback> callbacks {
        /// NOTE: Callbacks are linearly searched, so more common constructs should
        /// be generally placed at the front of the list for performance reasons.
        {
            "inspect.Parameter.empty",
            [](Object hint, std::vector<Object>& out) -> bool {
                if (hint.is(getattr<"empty">(
                    getattr<"Parameter">(import_inspect())
                ))) {
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                    ));
                    return true;
                }
                return false;
            }
        },
        {
            /// TODO: handling GenericAlias types is going to be fairly complicated, 
            /// and will require interactions with the global type map, and thus a
            /// forward declaration here.
            "types.GenericAlias",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object types = import_types();
                int rc = PyObject_IsInstance(
                    ptr(hint),
                    ptr(getattr<"GenericAlias">(types))
                );
                if (rc < 0) {
                    Exception::from_python();
                } else if (rc) {
                    Object typing = import_typing();
                    Object origin = getattr<"get_origin">(typing)(hint);
                    /// TODO: search in type map or fall back to Object
                    Object args = getattr<"get_args">(typing)(hint);
                    /// TODO: parametrize the bertrand type with the same args.  If
                    /// this causes a template error, then fall back to its default
                    /// specialization (i.e. list[Object]).
                    throw NotImplementedError(
                        "generic type subscription is not yet implemented"
                    );
                    return true;
                }
                return false;
            }
        },
        {
            "types.UnionType",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object types = import_types();
                int rc = PyObject_IsInstance(
                    ptr(hint),
                    ptr(getattr<"UnionType">(types))
                );
                if (rc < 0) {
                    Exception::from_python();
                } else if (rc) {
                    Object args = getattr<"get_args">(types)(hint);
                    Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                    for (Py_ssize_t i = 0; i < len; ++i) {
                        parse(reinterpret_borrow<Object>(
                            PyTuple_GET_ITEM(ptr(args), i)
                        ), out);
                    }
                    return true;
                }
                return false;
            }
        },
        {
            /// NOTE: when `typing.get_origin()` is called on a `typing.Optional`,
            /// it returns `typing.Union`, meaning that this handler will also
            /// implicitly cover `Optional` annotations for free.
            "typing.Union",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                Object origin = getattr<"get_origin">(typing)(hint);
                if (origin.is(nullptr)) {
                    Exception::from_python();
                } else if (origin.is(getattr<"Union">(typing))) {
                    Object args = getattr<"get_args">(typing)(hint);
                    Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                    for (Py_ssize_t i = 0; i < len; ++i) {
                        parse(reinterpret_borrow<Object>(
                            PyTuple_GET_ITEM(ptr(args), i)
                        ), out);
                    }
                    return true;
                }
                return false;
            }
        },
        {
            "typing.Any",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                Object origin = getattr<"get_origin">(typing)(hint);
                if (origin.is(nullptr)) {
                    Exception::from_python();
                } else if (origin.is(getattr<"Any">(typing))) {
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                    ));
                    return true;
                }
                return false;
            }
        },
        {
            "typing.TypeAliasType",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                int rc = PyObject_IsInstance(
                    ptr(hint),
                    ptr(getattr<"TypeAliasType">(typing))
                );
                if (rc < 0) {
                    Exception::from_python();
                } else if (rc) {
                    parse(getattr<"__value__">(hint), out);
                    return true;
                }
                return false;
            }
        },
        {
            "typing.Literal",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                Object origin = getattr<"get_origin">(typing)(hint);
                if (origin.is(nullptr)) {
                    Exception::from_python();
                } else if (origin.is(getattr<"Literal">(typing))) {
                    Object args = getattr<"get_args">(typing)(hint);
                    if (args.is(nullptr)) {
                        Exception::from_python();
                    }
                    Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                    for (Py_ssize_t i = 0; i < len; ++i) {
                        out.emplace_back(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(Py_TYPE(
                                PyTuple_GET_ITEM(ptr(args), i)
                            ))
                        ));
                    }
                    return true;
                }
                return false;
            }
        },
        {
            "typing.LiteralString",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                if (hint.is(getattr<"LiteralString">(typing))) {
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyUnicode_Type)
                    ));
                    return true;
                }
                return false;
            }
        },
        {
            "typing.AnyStr",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                if (hint.is(getattr<"AnyStr">(typing))) {
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyUnicode_Type)
                    ));
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBytes_Type)
                    ));
                    return true;
                }
                return false;
            }
        },
        {
            "typing.NoReturn",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                if (
                    hint.is(getattr<"NoReturn">(typing)) ||
                    hint.is(getattr<"Never">(typing))
                ) {
                    /// NOTE: this handler models NoReturn/Never by not pushing a
                    /// type to the `out` set, giving an empty return type.
                    return true;
                }
                return false;
            }
        },
        {
            "typing.TypeGuard",
            [](Object hint, std::vector<Object>& out) -> bool {
                Object typing = import_typing();
                Object origin = getattr<"get_origin">(typing)(hint);
                if (origin.is(nullptr)) {
                    Exception::from_python();
                } else if (origin.is(getattr<"TypeGuard">(typing))) {
                    out.emplace_back(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBool_Type)
                    ));
                    return true;
                }
                return false;
            }
        }
    };

    /* Convert the signature into a string representation for debugging purposes.  The
    provided `prefix` will be prepended to each output line, and if `max_width` is
    provided, then the algorithm will attempt to wrap the output to that width, with
    each parameter indented on a separate line.  If a single parameter exceeds the
    maximum width, then it will be wrapped onto multiple lines with an additional level
    of indentation for the extra lines.  Note that the maximum width is not a hard
    limit; individual components can exceed it, but never on the same line as another
    component. */
    [[nodiscard]] std::string to_string(
        bool keep_defaults = true,
        const std::string& prefix = "",
        size_t max_width = std::numeric_limits<size_t>::max(),
        size_t indent = 4
    ) const {
        std::vector<std::string> components;
        components.reserve(m_parameters.size() * 3 + 2);
        components.emplace_back(std::string(name()));

        size_t args_idx = std::numeric_limits<size_t>::max();
        size_t kwargs_idx = std::numeric_limits<size_t>::max();
        size_t last_posonly = std::numeric_limits<size_t>::max();
        size_t first_kwonly = std::numeric_limits<size_t>::max();
        for (size_t i = 0, end = m_parameters.size(); i < end; ++i) {
            const Param& param = m_parameters[i];
            if (param.args()) {
                args_idx = i;
                components.emplace_back("*" + std::string(param.name));
            } else if (param.kwargs()) {
                kwargs_idx = i;
                components.emplace_back("**" + std::string(param.name));
            } else {
                if (param.posonly()) {
                    last_posonly = i;
                } else if (param.kwonly() && first_kwonly > end) {
                    first_kwonly = i;
                }
                components.emplace_back(std::string(param.name));
            }
            components.emplace_back(demangle(PyType_Check(ptr(param.type)) ?
                reinterpret_cast<PyTypeObject*>(ptr(param.type))->tp_name :
                Py_TYPE(ptr(param.type))->tp_name
            ));
            if (param.opt()) {
                if (keep_defaults) {
                    components.emplace_back(repr(param.default_value));
                } else {
                    components.emplace_back("...");
                }
            } else {
                components.emplace_back("");
            }
        }

        if (m_return_annotation.is(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)))) {
            components.emplace_back("None");
        } else {
            components.emplace_back(demangle(
                PyType_Check(ptr(m_return_annotation)) ?
                    reinterpret_cast<PyTypeObject*>(ptr(m_return_annotation))->tp_name :
                    Py_TYPE(ptr(m_return_annotation))->tp_name
            ));
        }

        /// NOTE: a signature containing multiple "*" parameters is malformed in
        /// Python - all parameters after "*args" are keyword-only by design.
        if (args_idx < m_parameters.size()) {
            first_kwonly = std::numeric_limits<size_t>::max();
        }
        return impl::format_signature(
            prefix,
            max_width,
            indent,
            components,
            last_posonly,
            first_kwonly
        );
    }

    /* Convert the inspected signature into a template key that can be used to
    specialize the `bertrand.Function` type on the Python side.  This is used to
    implement C++-style CTAD in Python, in which an arbitrary Python function can be
    inspected and converted to a proper `bertrand.Function` type with a matching
    signature, possibly involving JIT compilation if the template type does not already
    exist. */
    [[nodiscard]] const Object& template_key() const {
        if (!m_key.is(nullptr)) {
            return m_key;
        }

        Object bertrand = import_bertrand();
        Object Arg = getattr<"Arg">(bertrand);
        Object partials = getattr<"__partial__">(m_func, None);
        Object self = getattr<"__self__">(m_func, None);
        Object result = reinterpret_steal<Object>(
            PyTuple_New(m_parameters.size() + 1)
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        // first element describes the return type
        size_t idx = 0;
        Object returns = return_annotation();
        if (returns.is(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)))) {
            returns = None;
        }
        PyTuple_SET_ITEM(ptr(result), idx++, release(returns));

        // remaining are parameters, expressed as specializations of `bertrand.Arg`
        for (const Param& param : m_parameters) {
            // append "*"/"**" to name if variadic
            PyObject* str;
            if (param.args()) {
                std::string name = "*" + std::string(param.name);
                str = PyUnicode_FromStringAndSize(
                    name.data(),
                    name.size()
                );
            } else if (param.kwargs()) {
                std::string name = "**" + std::string(param.name);
                str = PyUnicode_FromStringAndSize(
                    name.data(),
                    name.size()
                );
            } else {
                str = PyUnicode_FromStringAndSize(
                    param.name.data(),
                    param.name.size()
                );
            }
            if (str == nullptr) {
                Exception::from_python();
            }

            // parametrize Arg with name and type
            Object key = reinterpret_steal<Object>(PyTuple_Pack(
                2,
                str,
                ptr(param.type)
            ));
            Py_DECREF(str);
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            Object specialization = reinterpret_steal<Object>(PyObject_GetItem(
                ptr(Arg),
                ptr(key)
            ));
            if (specialization.is(nullptr)) {
                Exception::from_python();
            }

            // apply positional/keyword/optional flags
            if (param.posonly()) {
                if (param.opt()) {
                    specialization = getattr<"opt">(
                        getattr<"pos">(specialization)
                    );
                } else {
                    specialization = getattr<"pos">(specialization);
                }
            } else if (param.pos()) {
                if (param.opt()) {
                    specialization = getattr<"opt">(specialization);
                }
            } else if (param.kw()) {
                if (param.opt()) {
                    specialization = getattr<"opt">(
                        getattr<"kw">(specialization)
                    );
                } else {
                    specialization = getattr<"kw">(specialization);
                }
            } else if (!(param.args() || param.kwargs())) {
                throw TypeError(
                    "invalid parameter kind: " +
                    std::to_string(param.kind)
                );
            }

            // append bound partial type(s) if present
            if (param.bound()) {
                if (param.args()) {
                    size_t size = PyTuple_GET_SIZE(ptr(param.partial));
                    Object out = reinterpret_steal<Object>(PyTuple_New(size));
                    if (out.is(nullptr)) {
                        Exception::from_python();
                    }
                    for (size_t i = 0; i < size; ++i) {
                        PyObject* pair = PyTuple_GET_ITEM(ptr(param.partial), i);
                        PyObject* type = reinterpret_cast<PyObject*>(Py_TYPE(
                            PyTuple_GET_ITEM(pair, 1)
                        ));
                        PyTuple_SET_ITEM(ptr(out), i, Py_NewRef(type));
                    }
                    specialization = reinterpret_steal<Object>(PyObject_GetItem(
                        ptr(getattr<"bind">(specialization)),
                        ptr(out)
                    ));
                } else if (param.kwargs()) {
                    size_t size = PyTuple_GET_SIZE(ptr(param.partial));
                    Object out = reinterpret_steal<Object>(PyTuple_New(size));
                    if (out.is(nullptr)) {
                        Exception::from_python();
                    }
                    for (size_t i = 0; i < size; ++i) {
                        PyObject* pair = PyTuple_GET_ITEM(ptr(param.partial), i);
                        PyObject* type = reinterpret_cast<PyObject*>(Py_TYPE(
                            PyTuple_GET_ITEM(pair, 1)
                        ));
                        Object template_params = reinterpret_steal<Object>(
                            PyTuple_Pack(2, PyTuple_GET_ITEM(pair, 0), type)
                        );
                        if (template_params.is(nullptr)) {
                            Exception::from_python();
                        }
                        Object kw = reinterpret_steal<Object>(PyObject_GetItem(
                            ptr(Arg),
                            ptr(template_params)
                        ));
                        if (kw.is(nullptr)) {
                            Exception::from_python();
                        }
                        PyTuple_SET_ITEM(ptr(out), i, release(kw));
                    }
                    specialization = reinterpret_steal<Object>(PyObject_GetItem(
                        ptr(getattr<"bind">(specialization)),
                        ptr(out)
                    ));
                } else {
                    PyObject* name = PyTuple_GET_ITEM(ptr(param.partial), 0);
                    PyObject* value = PyTuple_GET_ITEM(ptr(param.partial), 1);
                    PyObject* type = reinterpret_cast<PyObject*>(Py_TYPE(value));
                    int rc = PyObject_Not(name);
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        specialization = reinterpret_steal<Object>(PyObject_GetItem(
                            ptr(getattr<"bind">(specialization)),
                            type
                        ));
                    } else {
                        Object template_params = reinterpret_steal<Object>(
                            PyTuple_Pack(2, name, type)
                        );
                        if (template_params.is(nullptr)) {
                            Exception::from_python();
                        }
                        Object kw = reinterpret_steal<Object>(PyObject_GetItem(
                            ptr(Arg),
                            ptr(template_params)
                        ));
                        if (kw.is(nullptr)) {
                            Exception::from_python();
                        }
                        specialization = reinterpret_steal<Object>(PyObject_GetItem(
                            ptr(getattr<"bind">(specialization)),
                            ptr(kw)
                        ));
                    }
                    if (specialization.is(nullptr)) {
                        Exception::from_python();
                    }
                }
            }

            // insert result into key tuple
            PyTuple_SET_ITEM(ptr(result), idx++, release(specialization));
        }

        // cache and return the completed key
        m_key = result;
        return m_key;
    }
};


/* Checks whether two signatures are compatible with each other, meaning that
one can be registered as a viable overload of the other.  Also allows
topological ordering of signatures, where `<` checks will sort signatures from
most restrictive to least restrictive.

Two signatures are considered compatible if every parameter in the lesser
signature has an equivalent in the greater signature with the same name and
kind, and the lesser annotation is a subtype of the greater.  Arguments must
also be given in the same order, and if any in the lesser signature have
default values, then the same must also be true in the greater (though the
default values may differ between them).  Note that the reverse is not always
true, meaning that if the greater signature defines a parameter as having a
default value, then it may be required in the lesser signature without
violating compatibility.

Variadic arguments in both signatures are special cases.  First, if the lesser
signature accepts variadic arguments of either kind, then the greater
signature must as well, and the lesser annotation must be a subtype of the
greater annotation.  Similar to default values, the reverse does not always
hold, meaning that if the greater signature has variadic arguments, then the
lesser signature can accept any number of additional arguments of the same kind
with arbitrary names (including other variadic arguments), as long as the
annotations are compatible with the greater argument's annotation.

Equality occurs when two signatures are compatible in both directions, meaning
that their parameter lists are identical.  Strict `<` or `>` ordering occurs
when the subtype relationships hold, but the parameter lists are not
identical. */
[[nodiscard]] bool operator<(const inspect& lhs, const inspect& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };
    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    if (!issubclass(
        ptr(lhs.return_annotation()),
        ptr(rhs.return_annotation())
    )) {
        return false;
    }
    bool equal = isequal(
        ptr(lhs.return_annotation()),
        ptr(rhs.return_annotation())
    );

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        if (r->args()) {
            while (l != l_end && l->pos()) {
                equal = false;
                if (!issubclass(ptr(l->type), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(r->type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(l->type), ptr(r->type));
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                equal = false;
                if (!issubclass(ptr(l->type), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(r->type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(l->type), ptr(r->type));
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type),
                ptr(r->type)
            )) {
                return false;
            }
            if (equal) {
                equal = isequal(ptr(l->type), ptr(r->type));
            }
            ++l;
        }
        ++r;
    }
    return !equal;
}


[[nodiscard]] bool operator<=(const inspect& lhs, const inspect& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    if (!issubclass(
        ptr(lhs.return_annotation()),
        ptr(rhs.return_annotation())
    )) {
        return false;
    }

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        if (r->args()) {
            while (l != l_end && l->pos()) {
                if (!issubclass(ptr(l->type), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(r->type)
                )) {
                    return false;
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                if (!issubclass(ptr(l->type), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(r->type)
                )) {
                    return false;
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type),
                ptr(r->type)
            )) {
                return false;
            }
            ++l;
        }
        ++r;
    }
    return true;
}


[[nodiscard]] bool operator==(const inspect& lhs, const inspect& rhs) {
    if (lhs.size() != rhs.size() || lhs.hash() != rhs.hash()) {
        return false;
    }

    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    if (!isequal(
        ptr(lhs.return_annotation()),
        ptr(rhs.return_annotation())
    )) {
        return false;
    }

    auto l = lhs.begin();
    auto r = rhs.begin();
    auto end = rhs.end();
    while (r != end) {
        if (l->name != r->name || l->kind != r->kind || !isequal(
            ptr(l->type),
            ptr(r->type)
        )) {
            return false;
        }
        ++l;
        ++r;
    }
    return true;
}


[[nodiscard]] bool operator!=(const inspect& lhs, const inspect& rhs) {
    return !(lhs == rhs);
}


[[nodiscard]] bool operator>=(const inspect& lhs, const inspect& rhs) {
    return rhs <= lhs;
}


[[nodiscard]] bool operator>(const inspect& lhs, const inspect& rhs) {
    return rhs < lhs;
}


}  // namespace bertrand


namespace std {
    template <py::impl::is<py::inspect> T>
    struct hash<T> {
        [[nodiscard]] static size_t operator()(const py::inspect& inspect) {
            return inspect.hash();
        }
    };
}


namespace bertrand {


namespace impl {

    struct signature_vectorcall_tag {};

    /* A single entry in a callback table, storing the argument name (which may be
    empty), a bitmask specifying its kind (positional-only, optional, variadic, etc.),
    a one-hot encoded bitmask specifying its position within the enclosing parameter
    list, and a set of function pointers that can be used to validate the argument at
    runtime.  Such callbacks are typically returned by the index operator and
    associated accessors. */
    struct PyParam {
        std::string_view name;
        ArgKind kind;
        size_t index;
        py::Object(*type)();
        bool(*isinstance)(const py::Object&);
        bool(*issubclass)(const py::Object&);

        template <size_t I, typename... Args>
        static constexpr PyParam create() {
            using T = unpack_type<I, Args...>;
            return {
                .name = std::string_view(ArgTraits<T>::name),
                .kind = ArgTraits<T>::kind,
                .index = I,
                .type = []() -> py::Object {
                    using U = ArgTraits<T>::type;
                    if constexpr (py::impl::has_python<U>) {
                        return py::Type<std::remove_cvref_t<py::impl::python_type<U>>>();
                    } else {
                        throw py::TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                },
                .isinstance = [](const py::Object& value) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (py::impl::has_python<U>) {
                        using V = std::remove_cvref_t<py::impl::python_type<U>>;
                        return py::isinstance<V>(value);
                    } else {
                        throw py::TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                },
                .issubclass = [](const py::Object& type) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (py::impl::has_python<U>) {
                        using V = std::remove_cvref_t<py::impl::python_type<U>>;
                        return py::issubclass<V>(type);
                    } else {
                        throw py::TypeError(
                            "C++ type has no Python equivalent: " + type_name<U>
                        );
                    }
                },
            };
        }

        [[nodiscard]] constexpr bool posonly() const noexcept { return kind.posonly(); }
        [[nodiscard]] constexpr bool pos() const noexcept { return kind.pos(); }
        [[nodiscard]] constexpr bool args() const noexcept { return kind.args(); }
        [[nodiscard]] constexpr bool kw() const noexcept { return kind.kw(); }
        [[nodiscard]] constexpr bool kwonly() const noexcept { return kind.kwonly(); }
        [[nodiscard]] constexpr bool kwargs() const noexcept { return kind.kwargs(); }
        [[nodiscard]] constexpr bool opt() const noexcept { return kind.opt(); }
        [[nodiscard]] constexpr bool variadic() const noexcept { return kind.variadic(); }

        [[nodiscard]] size_t hash() const noexcept {
            py::Object type = this->type();
            return hash_combine(
                fnv1a(
                    name.data(),
                    fnv1a_seed,
                    fnv1a_prime
                ),
                PyType_Check(ptr(type)) ?
                    reinterpret_cast<size_t>(ptr(type)) :
                    reinterpret_cast<size_t>(Py_TYPE(ptr(type))),
                static_cast<size_t>(kind)
            );
        }
    };

    /* Backs a C++ function whose return and argument types are all convertible to
    Python.  This extends the pure C++ equivalent to support Python's vectorcall
    protocol, allowing a matching function to be called from Python and/or be converted
    to a pure-Python equivalent, which can be returned to the interpreter. */
    template <typename Param, typename Return, typename... Args>
    struct CppToPySignature : CppSignature<Param, Return, Args...> {
    private:
        using base = CppSignature<Param, Return, Args...>;

    public:
        using Defaults = base::Defaults;

        static constexpr bool is_convertible_to_python = true;
        static constexpr bool is_python =
            py::impl::python<Return> && (
                py::impl::python<typename ArgTraits<Args>::type> && ...
            );

        using as_python = signature<py::impl::python_type<Return>(
            typename ArgTraits<Args>::template with_type<py::impl::python_type<Args>>...
        )>;

        /* A tuple holding a partial value for every bound argument in the enclosing
        parameter list.  One of these must be provided whenever a C++ function is
        invoked, and constructing one requires that the initializers match a
        sub-signature consisting only of the bound args as positional-only and
        keyword-only parameters for clarity.  The result may be empty if there are no
        bound arguments in the enclosing signature, in which case the constructor will
        be optimized out. */
        struct Partial : base::Partial {
        private:
            template <size_t I, size_t K>
            struct to_overload_key {
                static constexpr size_t size(size_t result) noexcept { return result; }
                template <typename P>
                static void operator()(P&&, py::impl::OverloadKey&) {}
            };
            template <size_t I, size_t K>
                requires (
                    I < base::size() &&
                    (K < base::Partial::size() && base::Partial::template rfind<K> == I)
                )
            struct to_overload_key<I, K> {
                static constexpr size_t size(size_t result) noexcept {
                    using T = base::template at<I>;
                    return to_overload_key<I + !ArgTraits<T>::variadic(), K + 1>::size(
                        result + 1
                    );
                }

                template <typename P>
                static void operator()(P&& partial, py::impl::OverloadKey& key) {
                    using T = base::template at<I>;
                    key->emplace_back(
                        std::string(base::Partial::template name<K>),
                        py::to_python(std::forward<P>(partial).template get<K>()),
                        ArgTraits<T>::kind
                    );
                    to_overload_key<I + !ArgTraits<T>::variadic(), K + 1>{}(
                        std::forward<P>(partial),
                        key
                    );
                }
            };
            template <size_t I, size_t K>
                requires (
                    I < base::size() &&
                    !(K < base::Partial::size() && base::Partial::template rfind<K> == I)
                )
            struct to_overload_key<I, K> {
                static constexpr size_t size(size_t result) noexcept {
                    return to_overload_key<I + 1, K>::size(result + 1);
                }

                template <typename P>
                static void operator()(P&& partial, py::impl::OverloadKey& key) {
                    using T = base::template at<I>;
                    key->emplace_back(
                        std::string(ArgTraits<T>::name),
                        py::reinterpret_steal<py::Object>(nullptr),
                        ArgTraits<T>::kind
                    );
                    to_overload_key<I + 1, K>{}(std::forward<P>(partial), key);
                }
            };

        public:
            using base::Partial::Partial;

            /* Convert a partial tuple into a standardized key type that can be used to
            initiate a search of an overload trie. */
            [[nodiscard]] py::impl::OverloadKey overload_key() const {
                py::impl::OverloadKey key;
                key->reserve(to_overload_key<0, 0>::size(0));
                to_overload_key<0, 0>{}(*this, key);
                return key;
            }
            [[nodiscard]] py::impl::OverloadKey overload_key() && {
                py::impl::OverloadKey key;
                key->reserve(to_overload_key<0, 0>::size(0));
                to_overload_key<0, 0>{}(std::move(*this), key);
                return key;
            }
        };

        /* Instance-level constructor for a `::Partial` tuple. */
        template <typename... A>
            requires (
                !(arg_pack<A> || ...) &&
                !(kwarg_pack<A> || ...) &&
                Partial::template Bind<A...>::proper_argument_order &&
                Partial::template Bind<A...>::no_qualified_arg_annotations &&
                Partial::template Bind<A...>::no_duplicate_args &&
                Partial::template Bind<A...>::no_conflicting_values &&
                Partial::template Bind<A...>::no_extra_positional_args &&
                Partial::template Bind<A...>::no_extra_keyword_args &&
                Partial::template Bind<A...>::satisfies_required_args &&
                Partial::template Bind<A...>::can_convert
            )
        [[nodiscard]] static constexpr Partial partial(A&&... args) {
            return Partial(std::forward<A>(args)...);
        }

        /* Adopt or produce a Python vectorcall array for the enclosing signature,
        allowing a matching C++ function to be invoked from Python, or a Python
        function to be invoked from C++.  This same format can also be used to search a
        function's overload trie and immediately invoke a match if one exists.  Such an
        array always has the following layout:

                                        ( kwnames tuple )
                    -------------------------------------
                    | x | p | p | p |...| k | k | k |...|
                    -------------------------------------
                        ^             ^
                        |             nargsf ends here
                        *args starts here

        Where 'x' is an optional first element that can be temporarily written to in
        order to efficiently forward the `self` argument for bound methods, etc.  The
        presence of this argument is determined by the `PY_VECTORCALL_ARGUMENTS_OFFSET`
        flag, which is encoded in `nargsf`.  You can check for its presence by bitwise
        AND-ing against `nargsf`, and the true number of positional arguments must be
        extracted using `PyVectorcall_NARGS(nargsf)` to account for this.  By default, any vectorcall array
        that is produced by Bertrand will include this offset in order to speed up
        downstream code, but vectorcall arrays passed in from Python might not use it.  If
        `PY_VECTORCALL_ARGUMENTS_OFFSET` is set and 'x' is written to, then it must always
        be reset to its original value before the function returns to allow for nested
        forwarding/scoping using the same argument list.

        Type safety is handled by either converting C++ arguments to Python via
        `py::to_python()` or converting Python arguments to C++ by interpreting them as
        dynamic `py::Object` types and implicitly converting to the expected C++ type.
        This means that any changes made to the `__isinstance__` or `__cast__` control
        structs will be reflected here as well, and the conversion will always be as
        efficient as possible thanks to compile-time specialization.

        In some cases, an additional conversion may be required to handle Python types that
        lack sufficient type information for the check, such as standard Python containers
        that can contain any type.  In those cases, the type check may be applied
        elementwise to the contents of the container, which can be expensive if the
        container is large.  This can be avoided by using Bertrand types as inputs to the
        function, in which case the check is always O(1) in time, due to the 1:1
        equivalence between Bertrand wrappers and their C++ counterparts. */
        struct Vectorcall : signature_vectorcall_tag {
        private:
            static std::string_view arg_name(PyObject* name) {
                Py_ssize_t len;
                const char* str = PyUnicode_AsUTF8AndSize(name, &len);
                if (str == nullptr) {
                    Exception::from_python();
                }
                return {str, static_cast<size_t>(len)};
            }

            static size_t arg_hash(
                const char* name,
                PyObject* value,
                ArgKind kind
            ) noexcept {
                return py::impl::parameter_hash(name, value, kind);
            }

            /* Python flags are joined into `nargsf` and occupy only the highest bits,
            leaving the lowest bits available for our use. */
            enum class Flags : size_t {
                NORMALIZED      = 0b1,
                ALL             = 0b1,  // masks all extra flags
            };

            /* The kwnames tuple must be converted into a temporary map that can be
            destructively searched during the call algorithm, analogous to KwargPack,
            but lighter weight, since it doesn't need to own the keyword names.  If any
            arguments remain by the time the underlying function is called, then they
            are considered extras. */
            struct Kwargs {
                struct Value {
                    std::string_view name;
                    PyObject* unicode;
                    PyObject* value;

                    struct Hash {
                        using is_transparent = void;
                        static size_t operator()(const Value& value) {
                            return std::hash<std::string_view>{}(value.name);
                        }
                        static size_t operator()(std::string_view name) {
                            return std::hash<std::string_view>{}(name);
                        }
                    };

                    struct Equal {
                        using is_transparent = void;
                        static bool operator()(const Value& lhs, const Value& rhs) {
                            return lhs.name == rhs.name;
                        }
                        static bool operator()(const Value& lhs, std::string_view rhs) {
                            return lhs.name == rhs;
                        }
                        static bool operator()(std::string_view lhs, const Value& rhs) {
                            return lhs == rhs.name;
                        }
                    };
                };

                using Set = std::unordered_set<
                    Value,
                    typename Value::Hash,
                    typename Value::Equal
                >;
                Set set;

                Kwargs(
                    PyObject* const* array,
                    size_t nargs,
                    PyObject* kwnames,
                    size_t kwcount
                ) :
                    set([](
                        PyObject* const* array,
                        size_t nargs,
                        PyObject* kwnames,
                        size_t kwcount
                    ) {
                        Set set;
                        set.reserve(kwcount);
                        for (size_t i = 0; i < kwcount; ++i) {
                            PyObject* kwname = PyTuple_GET_ITEM(kwnames, i);
                            set.emplace(
                                arg_name(kwname),
                                kwname,
                                array[nargs + i]
                            );
                        }
                        return set;
                    }(array, nargs, kwnames, kwcount))
                {}

                void validate() {
                    if constexpr (!signature::has_kwargs) {
                        if (!set.empty()) {
                            auto it = set.begin();
                            auto end = set.end();
                            std::string message =
                                "unexpected keyword arguments: ['" + std::string(it->name);
                            while (++it != end) {
                                message += "', '" + std::string(it->name);
                            }
                            message += "']";
                            throw TypeError(message);
                        }
                    }
                }

                auto size() const { return set.size(); }
                template <typename T>
                auto extract(T&& key) { return set.extract(std::forward<T>(key)); }
                auto begin() { return set.begin(); }
                auto end() { return set.end(); }
            };

            template <size_t I, size_t J, size_t K>
            struct merge {
            private:
                using T = base::template at<I>;
                static constexpr static_str name = ArgTraits<T>::name;

                template <size_t K2>
                static constexpr bool use_partial = false;
                template <size_t K2> requires (K2 < Partial::size())
                static constexpr bool use_partial<K2> = Partial::template rfind<K2> == I;

                template <size_t K2>
                static constexpr size_t consecutive = 0;
                template <size_t K2> requires (K2 < Partial::size())
                static constexpr size_t consecutive<K2> = 
                    Partial::template rfind<K2> == I ? consecutive<K2 + 1> + 1 : 0;

                template <typename... A>
                static constexpr size_t pos_range = 0;
                template <typename A, typename... As>
                static constexpr size_t pos_range<A, As...> =
                    ArgTraits<A>::pos() ? pos_range<As...> + 1 : 0;

                template <typename... A>
                static constexpr void assert_no_kwargs_conflict(A&&... args) {
                    if constexpr (!name.empty() && impl::kwargs_idx<A...> < sizeof...(A)) {
                        auto&& pack = unpack_arg<impl::kwargs_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        if (auto node = pack.extract(name)) {
                            throw py::TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }
                    }
                }

                static void forward_partial(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    constexpr static_str partial_name = Partial::template name<K>;

                    PyObject* value = py::release(py::to_python(
                        std::forward<P>(parts).template get<K>()
                    ));
                    array[idx++] = value;

                    if constexpr (ArgTraits<T>::posonly() || ArgTraits<T>::args()) {
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );

                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (
                            partial_name.empty() ||
                            J < std::min(impl::kw_idx<A...>, impl::kwargs_idx<A...>)
                        ) {
                            hash = hash_combine(
                                hash,
                                arg_hash("", value, ArgKind::POS)
                            );
                        } else {
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx++,
                                py::release(py::impl::template_string<partial_name>())
                            );
                            hash = hash_combine(
                                hash,
                                arg_hash(partial_name, value, ArgKind::KW)
                            );
                        }

                    } else if constexpr (ArgTraits<T>::kw() || ArgTraits<T>::kwargs()) {
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            py::release(py::impl::template_string<partial_name>())
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash(partial_name, value, ArgKind::KW)
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }

                    return merge<I + !ArgTraits<T>::variadic(), J, K + 1>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                }

                static void forward_positional(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    assert_no_kwargs_conflict(std::forward<decltype(args)>(args)...);
                    PyObject* value = py::release(py::to_python(
                        unpack_arg<J>(std::forward<decltype(args)>(args)...)
                    ));
                    array[idx++] = value;
                    hash = hash_combine(
                        hash,
                        arg_hash("", value, ArgKind::POS)
                    );
                    merge<I + 1, J + 1, K>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(args)>(args)...
                    );
                }

                template <size_t... Prev, size_t... Next, size_t... Rest>
                static void forward_keyword(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    std::index_sequence<Rest...>,
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    constexpr size_t kw = impl::args_idx<name, decltype(args)...>;
                    PyObject* value = py::release(py::to_python(
                        unpack_arg<kw>(std::forward<decltype(args)>(args)...)
                    ));
                    array[idx++] = value;
                    PyTuple_SET_ITEM(
                        kwnames,
                        kw_idx++,
                        py::release(py::impl::template_string<name>())
                    );
                    hash = hash_combine(
                        hash,
                        arg_hash(name, value, ArgKind::KW)
                    );
                    merge<I + 1, J + 1, K>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<decltype(parts)>(parts),
                        unpack_arg<Prev>(std::forward<decltype(args)>(args))...,
                        unpack_arg<kw>(std::forward<decltype(args)>(args)...),
                        unpack_arg<J + Next>(std::forward<decltype(args)>(args))...,
                        unpack_arg<kw + 1 + Rest>(std::forward<decltype(args)>(args))...
                    );
                }

                static void forward_from_pos_pack(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    PyObject* value = py::release(py::to_python(pack.value()));
                    array[idx++] = value;
                    hash = hash_combine(
                        hash,
                        arg_hash("", value, ArgKind::POS)
                    );
                    merge<I + 1, J, K>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(args)>(args)...
                    );
                }

                static void forward_from_kw_pack(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    PyObject* value = py::release(py::to_python(std::move(node.mapped())));
                    array[idx++] = value;
                    PyTuple_SET_ITEM(
                        kwnames,
                        kw_idx++,
                        py::release(py::impl::template_string<name>())
                    );
                    hash = hash_combine(
                        hash,
                        arg_hash(name, value, ArgKind::KW)
                    );
                    merge<I + 1, J, K>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<decltype(parts)>(parts),
                        std::forward<decltype(args)>(args)...
                    );
                }

                template <size_t J2, typename P, typename... A>
                static void forward_pos_pack(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    A&&... args
                ) {
                    if constexpr (J2 < pos_range<A...>) {
                        PyObject* value = py::release(py::to_python(
                            unpack_arg<J2>(std::forward<A>(args)...)
                        ));
                        array[idx++] = value;
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                        forward_pos_pack<J2 + 1>(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J2 < sizeof...(A) && J2 == impl::args_idx<A...>) {
                        auto&& pack = unpack_arg<J2>(std::forward<A>(args)...);
                        while (pack.has_value()) {
                            PyObject* value = py::release(py::to_python(
                                pack.value()
                            ));
                            array[idx++] = value;
                            hash = hash_combine(
                                hash,
                                arg_hash("", value, ArgKind::POS)
                            );
                        }
                        drop_empty_pack(
                            std::make_index_sequence<J2>{},
                            std::make_index_sequence<sizeof...(A) - (J2 + 1)>{},
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    } else {
                        merge<I + 1, J2, K>::create(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    }
                }

                template <size_t J2, typename P, typename... A>
                static void forward_kw_pack(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    A&&... args
                ) {
                    if constexpr (J2 < impl::kwargs_idx<A...>) {
                        using U = unpack_type<J2, A...>;
                        constexpr static_str name = ArgTraits<U>::name;
                        PyObject* value = py::release(py::to_python(
                            unpack_arg<J2>(std::forward<A>(args)...)
                        ));
                        array[idx++] = value;
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            py::release(py::impl::template_string<name>())
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                        return forward_kw_pack<J2 + 1>(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    } else if constexpr (J2 < sizeof...(A) && J2 == impl::kwargs_idx<A...>) {
                        auto&& pack = unpack_arg<J2>(std::forward<A>(args)...);
                        auto it = pack.begin();
                        auto end = pack.end();
                        while (it != end) {
                            // postfix++ required to increment before invalidating
                            auto node = pack.extract(it++);
                            PyObject* value = py::release(py::to_python(
                                std::move(node.mapped())
                            ));
                            array[idx++] = value;
                            PyObject* name = PyUnicode_FromStringAndSize(
                                node.key().data(),
                                node.key().size()
                            );
                            if (name == nullptr) {
                                py::Exception::from_python();
                            }
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx++,
                                name  // steals reference
                            );
                            hash = hash_combine(
                                hash,
                                arg_hash(
                                    node.key().data(),
                                    value,
                                    ArgKind::KW
                                )
                            );
                        }
                        drop_empty_pack(
                            std::make_index_sequence<J2>{},
                            std::make_index_sequence<sizeof...(A) - (J2 + 1)>{},
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    } else {
                        merge<I + 1, J2, K>::create(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            std::forward<A>(args)...
                        );
                    }
                }

                template <size_t... Prev, size_t... Next>
                static void drop_empty_pack(
                    std::index_sequence<Prev...>,
                    std::index_sequence<Next...>,
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    auto&& parts,
                    auto&&... args
                ) {
                    merge<I, J, K>::create(
                        array,
                        idx,
                        kwnames,
                        kw_idx,
                        hash,
                        std::forward<decltype(parts)>(parts),
                        unpack_arg<Prev>(
                            std::forward<decltype(args)>(args)
                        )...,
                        unpack_arg<J + 1 + Next>(
                            std::forward<decltype(args)>(args)
                        )...
                    );
                }

                template <typename P>
                static void normalize_partial(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    constexpr static_str partial_name = Partial::template name<K>;

                    PyObject* value = py::release(py::to_python(
                        std::forward<P>(parts).template get<K>()
                    ));
                    array[idx++] = value;

                    if constexpr (ArgTraits<T>::posonly() || ArgTraits<T>::args()) {
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );

                    } else if constexpr (ArgTraits<T>::pos()) {
                        if (partial_name.empty() || old_idx < old_nargs) {
                            hash = hash_combine(
                                hash,
                                arg_hash("", value, ArgKind::POS)
                            );
                        } else {
                            PyTuple_SET_ITEM(
                                kwnames,
                                kw_idx++,
                                py::release(py::impl::template_string<partial_name>())
                            );
                            hash = hash_combine(
                                hash,
                                arg_hash(name, value, ArgKind::KW)
                            );
                        }

                    } else if constexpr (ArgTraits<T>::kw() || ArgTraits<T>::kwargs()) {
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            py::release(py::impl::template_string<partial_name>())
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash(name, value, ArgKind::KW)
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                    }
                }

                static void normalize_posonly(
                    PyObject** array,
                    size_t& idx,
                    size_t& hash,
                    PyObject* const* old,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    if (old_idx < old_nargs) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(old[old_idx++])
                        ));
                        array[idx++] = value;
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                    } else {
                        if constexpr (!ArgTraits<T>::opt()) {
                            if constexpr (name.empty()) {
                                throw py::TypeError(
                                    "no match for positional-only parameter at "
                                    "index " + static_str<>::from_int<I>
                                );
                            } else {
                                throw py::TypeError(
                                    "no match for positional-only parameter '" +
                                    name + "' at index " + static_str<>::from_int<I>
                                );
                            }
                        }
                    }
                }

                static void normalize_pos_or_kw(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    PyObject* const* old,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    if (old_idx < old_nargs) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(old[old_idx++])
                        ));
                        array[idx++] = value;
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                    } else {
                        if constexpr (!ArgTraits<T>::opt()) {
                            throw py::TypeError(
                                "no match for parameter '" + name + "' at index " +
                                static_str<>::from_int<I>
                            );
                        }
                    }
                }

                static void normalize_pos_or_kw(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    Kwargs& old_kwargs,
                    PyObject* const* old,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    if (old_idx < old_nargs) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(old[old_idx++])
                        ));
                        array[idx++] = value;
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                    } else if (auto node = old_kwargs.extract(std::string_view(name))) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(node.value().value)
                        ));
                        array[idx++] = value;
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            Py_NewRef(node.value().unicode)
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash(
                                node.value().name.data(),
                                value,
                                ArgKind::KW
                            )
                        );
                    } else {
                        if constexpr (!ArgTraits<T>::opt()) {
                            throw py::TypeError(
                                "no match for parameter '" + name + "' at index " +
                                static_str<>::from_int<I>
                            );
                        }
                    }
                }

                static void normalize_kwonly(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash
                ) {
                    if constexpr (!ArgTraits<T>::opt()) {
                        throw py::TypeError(
                            "no match for keyword-only parameter '" + ArgTraits<T>::name +
                            "' at index " + static_str<>::from_int<I>
                        );
                    }
                }

                static void normalize_kwonly(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    Kwargs& old_kwargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    if (auto node = old_kwargs.extract(std::string_view(name))) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(node.value().value)
                        ));
                        array[idx++] = value;
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            Py_NewRef(node.value().unicode)
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash(
                                node.value().name.data(),
                                value,
                                ArgKind::KW
                            )
                        );
                    } else {
                        if constexpr (!ArgTraits<T>::opt()) {
                            throw py::TypeError(
                                "no match for keyword-only parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }
                    }
                }

                static void normalize_args(
                    PyObject** array,
                    size_t& idx,
                    size_t& hash,
                    PyObject* const* old,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    while (old_idx < old_nargs) {
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(old[old_idx++])
                        ));
                        array[idx++] = value;
                        hash = hash_combine(
                            hash,
                            arg_hash("", value, ArgKind::POS)
                        );
                    }
                }

                static void normalize_kwargs(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    Kwargs& old_kwargs
                ) {
                    using type = py::impl::python_type<typename ArgTraits<T>::type>;
                    auto it = old_kwargs.begin();
                    auto end = old_kwargs.end();
                    while (it != end) {
                        // postfix ++ required to increment before invalidation
                        auto node = old_kwargs.extract(it++);
                        PyObject* value = py::release(implicit_cast<type>(
                            py::reinterpret_borrow<py::Object>(node.value().value)
                        ));
                        array[idx++] = value;
                        PyTuple_SET_ITEM(
                            kwnames,
                            kw_idx++,
                            Py_NewRef(node.value().unicode)
                        );
                        hash = hash_combine(
                            hash,
                            arg_hash(
                                node.value().name.data(),
                                value,
                                ArgKind::KW
                            )
                        );
                    }
                }

            public:
                /* Given a C++ argument list meant to initialize a vectorcall array,
                determine the total number of keywords that will be added to represent
                partial arguments.  Note that this at most the raw number of partial
                keywords, since bound positional-or-keyword arguments may be demoted to
                positional if another positional argument follows later in the argument
                list.  The result is used to allocate a precise length of kwnames for
                the vectorcall array. */
                template <typename... A>
                static constexpr size_t n_partial_kw() {
                    constexpr size_t cutoff =
                        std::min(impl::kw_idx<A...>, impl::kwargs_idx<A...>);
                    if constexpr (use_partial<K>) {
                        constexpr size_t next = merge<
                            I + 1,
                            J,
                            K + consecutive<K>
                        >::template n_partial_kw<A...>();
                        if constexpr (ArgTraits<T>::kwonly() || ArgTraits<T>::kwargs()) {
                            return next + consecutive<K>;
                        } else if constexpr (ArgTraits<T>::kw()) {
                            return next + (J >= cutoff);
                        } else {
                            return next;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return merge<
                            I + 1,
                            cutoff,
                            K
                        >::template n_partial_kw<A...>();
                    } else if constexpr (impl::has_args<A...> && J == impl::args_idx<A...>) {
                        return merge<
                            std::min(base::kwonly_idx, base::kwargs_idx),
                            J + 1,
                            K
                        >::template n_partial_kw<A...>();
                    } else {
                        return merge<
                            I + 1,
                            J + 1,
                            K
                        >::template n_partial_kw<A...>();
                    }
                }

                /* Given an index and total positional count for a denormalized
                vectorcall array, determine the total number of keywords that will be
                added during normalization to represent partial arguments.  Note that
                this at most the raw number of partial keywords, since bound
                positional-or-keyword arguments may be demoted to positional if another
                positional argument follows later in the argument list.  The result is
                used to allocate a precise length of kwnames for the vectorcall
                array. */
                static constexpr size_t n_partial_kw(size_t idx, size_t nargs) {
                    if constexpr (use_partial<K>) {
                        size_t next = merge<
                            I + 1,
                            J,
                            K + consecutive<K>
                        >::n_partial_kw(idx, nargs);
                        if constexpr (ArgTraits<T>::kwonly() || ArgTraits<T>::kwargs()) {
                            return next + consecutive<K>;
                        } else if constexpr (ArgTraits<T>::kw()) {
                            return next + (idx >= nargs);
                        } else {
                            return next;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return merge<I + 1, J, K>::n_partial_kw(nargs, nargs);
                    } else {
                        return merge<I + 1, J, K>::n_partial_kw(++idx, nargs);
                    }
                }

                /* Populate a vectorcall array with C++ arguments.  This is called on
                either a stack-allocated array with a precomputed size or a
                heap-allocated array if there are packs in the argument list, whose
                size cannot be known ahead of time. */
                template <inherits<Partial> P, typename... A>
                static void create(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    A&&... args
                ) {
                    if constexpr (ArgTraits<T>::posonly()) {
                        if constexpr (use_partial<K>) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_partial(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (J < pos_range<A...>) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_positional(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (J < sizeof...(A) && J == impl::args_idx<A...>) {
                            auto&& pack = unpack_arg<J>(std::forward<A>(args)...);
                            if (pack.has_value()) {
                                assert_no_kwargs_conflict(std::forward<A>(args)...);
                                forward_from_pos_pack(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                drop_empty_pack(
                                    std::make_index_sequence<J>{},
                                    std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        } else if constexpr (ArgTraits<T>::opt()) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            merge<I + 1, J, K>::create(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (name.empty()) {
                            throw py::TypeError(
                                "no match for positional-only parameter at "
                                "index " + static_str<>::from_int<I>
                            );
                        } else {
                            throw py::TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + static_str<>::from_int<I>
                            );
                        }

                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (use_partial<K>) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_partial(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (J < pos_range<A...>) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_positional(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (J < sizeof...(A) && J == impl::args_idx<A...>) {
                            auto&& pack = impl::unpack_arg<J>(
                                std::forward<decltype(args)>(args)...
                            );
                            if (pack.has_value()) {
                                assert_no_kwargs_conflict(std::forward<A>(args)...);
                                forward_from_pos_pack(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                drop_empty_pack(
                                    std::make_index_sequence<J>{},
                                    std::make_index_sequence<sizeof...(A) - (J + 1)>{},
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            }
                        } else if constexpr (impl::args_idx<name, A...> < sizeof...(A)) {
                            constexpr size_t kw = impl::args_idx<name, A...>;
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_keyword(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<kw - J>{},
                                std::make_index_sequence<sizeof...(A) - (kw + 1)>{},
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (impl::kwargs_idx<A...> < sizeof...(A)) {
                            auto&& pack = unpack_arg<impl::kwargs_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            if (auto node = pack.extract(name)) {
                                forward_from_kw_pack(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                if constexpr (ArgTraits<T>::opt()) {
                                    merge<I + 1, J, K>::create(
                                        array,
                                        idx,
                                        kwnames,
                                        kw_idx,
                                        hash,
                                        std::forward<P>(parts),
                                        std::forward<A>(args)...
                                    );
                                } else {
                                    throw py::TypeError(
                                        "no match for parameter '" + name + "' at index " +
                                        static_str<>::from_int<I>
                                    );
                                }
                            }
                        } else if constexpr (ArgTraits<T>::opt()) {
                            merge<I + 1, J, K>::create(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else {
                            throw py::TypeError(
                                "no match for parameter '" + name + "' at index " +
                                static_str<>::from_int<I>
                            );
                        }

                    } else if constexpr (ArgTraits<T>::kw()) {
                        if constexpr (use_partial<K>) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_partial(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (impl::arg_idx<name, A...> < sizeof...(A)) {
                            assert_no_kwargs_conflict(std::forward<A>(args)...);
                            forward_keyword(
                                std::make_index_sequence<J>{},
                                std::make_index_sequence<kw - J>{},
                                std::make_index_sequence<sizeof...(A) - (kw + 1)>{},
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                            auto&& pack = unpack_arg<impl::kwargs_idx<A...>>(
                                std::forward<A>(args)...
                            );
                            if (auto node = pack.extract(name)) {
                                forward_from_kw_pack(
                                    array,
                                    idx,
                                    kwnames,
                                    kw_idx,
                                    hash,
                                    std::forward<P>(parts),
                                    std::forward<A>(args)...
                                );
                            } else {
                                if constexpr (ArgTraits<T>::opt()) {
                                    merge<I + 1, J, K>::create(
                                        array,
                                        idx,
                                        kwnames,
                                        kw_idx,
                                        hash,
                                        std::forward<P>(parts),
                                        std::forward<A>(args)...
                                    );
                                } else {
                                    throw py::TypeError(
                                        "no match for parameter '" + name + "' at index " +
                                        static_str<>::from_int<I>
                                    );
                                }
                            }
                        } else if constexpr (ArgTraits<T>::opt()) {
                            return merge<I + 1, J, K>::create(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else {
                            throw py::TypeError(
                                "no match for keyword-only parameter '" + name +
                                "' at index " + static_str<>::from_int<I>
                            );
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        if constexpr (use_partial<K>) {
                            forward_partial(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else {
                            forward_pos_pack(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        if constexpr (use_partial<K>) {
                            forward_partial(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        } else {
                            forward_kw_pack(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<P>(parts),
                                std::forward<A>(args)...
                            );
                        }
                    } else {
                        static_assert(false, "invalid argument kind");
                    }
                }

                template <inherits<Partial> P>
                static void normalize(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    PyObject* const* old,
                    size_t& old_idx,
                    size_t& old_nargs
                ) {
                    if constexpr (use_partial<K>) {
                        normalize_partial(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old_idx,
                            old_nargs
                        );
                        return merge<I + !ArgTraits<T>::variadic(), J, K + 1>::normalize(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old,
                            old_idx,
                            old_nargs
                        );
                    } else {
                        if constexpr (ArgTraits<T>::posonly()) {
                            normalize_posonly(
                                array,
                                idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs
                            );
                        } else if constexpr (ArgTraits<T>::pos()) {
                            normalize_pos_or_kw(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs
                            );
                        } else if constexpr (ArgTraits<T>::kw()) {
                            normalize_kwonly(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash
                            );
                        } else if constexpr (ArgTraits<T>::args()) {
                            normalize_args(
                                array,
                                idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs
                            );
                        } else if constexpr (ArgTraits<T>::kwargs()) {
                            // no keywords to match
                        } else {
                            static_assert(false, "invalid argument kind");
                            std::unreachable();
                        }
                        merge<I + 1, J, K>::normalize(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old,
                            old_idx,
                            old_nargs
                        );
                    }
                }

                template <inherits<Partial> P>
                static void normalize(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    PyObject* const* old,
                    size_t old_idx,
                    size_t old_nargs,
                    Kwargs& old_kwargs
                ) {
                    if constexpr (use_partial<K>) {
                        if constexpr (!ArgTraits<T>::variadic() && !name.empty()) {
                            if (auto node = old_kwargs.extract(std::string_view(name))) {
                                throw py::TypeError(
                                    "received multiple values for argument '" + name + "'"
                                );
                            }
                        }
                        normalize_partial(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old_idx,
                            old_nargs
                        );
                        return merge<I + !ArgTraits<T>::variadic(), J, K + 1>::normalize(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old,
                            old_idx,
                            old_nargs,
                            old_kwargs
                        );
                    } else {
                        if constexpr (ArgTraits<T>::posonly()) {
                            normalize_posonly(
                                array,
                                idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs
                            );
                        } else if constexpr (ArgTraits<T>::pos()) {
                            normalize_pos_or_kw(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs,
                                old_kwargs
                            );
                        } else if constexpr (ArgTraits<T>::kw()) {
                            normalize_kwonly(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                old_kwargs
                            );
                        } else if constexpr (ArgTraits<T>::args()) {
                            normalize_args(
                                array,
                                idx,
                                hash,
                                old,
                                old_idx,
                                old_nargs
                            );
                        } else if constexpr (ArgTraits<T>::kwargs()) {
                            normalize_kwargs(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                old_kwargs
                            );
                        } else {
                            static_assert(false, "invalid argument kind");
                            std::unreachable();
                        }
                        merge<I + 1, J, K>::normalize(
                            array,
                            idx,
                            kwnames,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            old,
                            old_idx,
                            old_nargs,
                            old_kwargs
                        );
                    }
                }


            };
            template <size_t J, size_t K>
            struct merge<base::size(), J, K> {
            private:

                static void validate_positional(
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs
                ) {
                    if constexpr (!base::has_args) {
                        if (idx < nargs) {
                            std::string message =
                                "unexpected positional arguments: [" +
                                py::repr(py::reinterpret_borrow<Object>(array[idx]));
                            while (++idx < nargs) {
                                message += ", " + py::repr(
                                    py::reinterpret_borrow<Object>(array[idx])
                                );
                            }
                            message += "]";
                            throw py::TypeError(message);
                        }
                    }
                }

            public:
                template <typename... A>
                static constexpr size_t n_partial_kw() { return 0; }
                static constexpr size_t n_partial_kw(size_t idx, size_t nargs) {
                    return 0;
                }

                template <inherits<Partial> P, typename... A>
                static void create(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    A&&... args
                ) {
                    if constexpr (args_idx<A...> < sizeof...(A)) {
                        auto&& pack = unpack_arg<pos_pack_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        pack.validate();
                    }
                    if constexpr (kw_pack_idx<A...> < sizeof...(A)) {
                        auto&& pack = unpack_arg<kw_pack_idx<A...>>(
                            std::forward<A>(args)...
                        );
                        pack.validate();
                    }
                }

                template <inherits<Partial> P>
                static void normalize(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    PyObject* const* old,
                    size_t old_idx,
                    size_t old_nargs
                ) {
                    validate_positional(old, old_idx, old_nargs);
                }

                template <inherits<Partial> P>
                static void normalize(
                    PyObject** array,
                    size_t& idx,
                    PyObject* kwnames,
                    size_t& kw_idx,
                    size_t& hash,
                    P&& parts,
                    Kwargs& old_kwargs,
                    PyObject* const* old,
                    size_t old_idx,
                    size_t old_nargs
                ) {
                    validate_positional(old, old_idx, old_nargs);
                    old_kwargs.validate();
                }


                template <
                    inherits<Partial> P,
                    inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (base::template invocable<F>)
                static std::invoke_result_t<F, Args...> operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    A&&... args
                ) {
                    validate_positional(array, idx, nargs);
                    return std::forward<F>(func)(std::forward<A>(args)...);
                }

                template <
                    inherits<Partial> P,
                    inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (base::template invocable<F>)
                static std::invoke_result_t<F, Args...> operator()(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    Kwargs& kwargs,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    A&&... args
                ) {
                    validate_positional(array, idx, nargs);
                    kwargs.validate();
                    return std::forward<F>(func)(std::forward<A>(args)...);
                }
            };


            /* Invoking a C++ function from Python involves translating a vectorcall
            array and kwnames tuple into a valid C++ parameter list that exactly
            matches the enclosing signature.  This is implemented as a 3-way merge
            between partial arguments, converted vectorcall arguments, and default
            values, in that order of precedence.  The merge is controlled entirely via
            index sequences and fold expressions, which can be inlined directly into
            the final call. */
            template <size_t I, size_t J, size_t K>
            struct call {  // terminal case

            };
            template <size_t I, size_t J, size_t K>
                requires (
                    I < base::size() &&
                    (K < Partial::size() && Partial::template rfind<K> == I)
                )
            struct call<I, J, K> {  // insert partial argument(s)




                template <
                    impl::inherits<Partial> P,
                    impl::inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (signature::invocable<F>)
                static std::invoke_result_t<F, Args...> invoke(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    A&&... args
                ) {
                    using T = signature::at<I>;
                    if constexpr (ArgTraits<T>::args()) {
                        return call<I + 1, J, K + consecutive<K>>::invoke(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_positional(
                                std::forward<P>(parts),
                                array,
                                idx,
                                nargs
                            ))
                        );
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return call<I + 1, J, K + consecutive<K>>::invoke(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_keywords(
                                std::forward<P>(parts)
                            ))
                        );
                    } else {
                        return call<I + 1, J, K + 1>::invoke(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            std::forward<A>(args)...,
                            to_arg<I>(std::forward<P>(parts).template get<K>())
                        );
                    }
                }

                template <
                    impl::inherits<Partial> P,
                    impl::inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (signature::invocable<F>)
                static std::invoke_result_t<F, Args...> invoke_with_keywords(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    Kwargs& kwargs,
                    A&&... args
                ) {
                    using T = signature::at<I>;
                    if constexpr (ArgTraits<T>::args()) {
                        return call<I + 1, J, K + consecutive<K>>::invoke_with_keywords(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            kwargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_positional(
                                std::forward<P>(parts),
                                array,
                                idx,
                                nargs
                            ))
                        );
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return call<I + 1, J, K + consecutive<K>>::invoke_with_keywords(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            kwargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_keywords(
                                std::forward<P>(parts),
                                kwargs
                            ))
                        );
                    } else {
                        return call<I + 1, J, K + 1>::invoke_with_keywords(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            kwargs,
                            std::forward<A>(args)...,
                            to_arg<I>(std::forward<P>(parts).template get<K>())
                        );
                    }
                }

            private:

                template <typename P, typename... A>
                static auto variadic_positional(
                    P&& parts,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs
                ) {
                    using T = signature::at<I>;

                    // allocate variadic positional array
                    using vec = std::vector<typename ArgTraits<T>::type>;
                    vec out;
                    size_t diff = nargs > idx ? nargs - idx : 0;
                    out.reserve(consecutive<K> + diff);

                    // consume partial args
                    []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        vec& out,
                        auto&& parts
                    ) {
                        (out.emplace_back(std::forward<decltype(
                            parts
                        )>(parts).template get<K + Ks>()), ...);
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        out,
                        std::forward<P>(parts)
                    );

                    // consume vectorcall args
                    for (size_t i = idx; idx < nargs; ++i) {
                        out.emplace_back(
                            reinterpret_borrow<Object>(array[i])
                        );
                    }
                    return out;
                }

                template <typename P>
                static auto variadic_keywords(P&& parts) {
                    using T = signature::at<I>;

                    // allocate variadic keyword map
                    using map = std::unordered_map<
                        std::string,
                        typename ArgTraits<T>::type
                    >;
                    map out;
                    out.reserve(consecutive<K>);

                    // consume partial kwargs
                    []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        map& out,
                        auto&& parts
                    ) {
                        (out.emplace(
                            Partial::template name<K + Ks>,
                            std::forward<decltype(parts)>(
                                parts
                            ).template get<K + Ks>()
                        ), ...);
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        out,
                        std::forward<P>(parts)
                    );

                    return out;
                }

                template <typename P>
                static auto variadic_keywords(P&& parts, Kwargs& kwargs) {
                    using T = signature::at<I>;

                    // allocate variadic keyword map
                    using map = std::unordered_map<
                        std::string,
                        typename ArgTraits<T>::type
                    >;
                    map out;
                    out.reserve(consecutive<K> + kwargs.size());

                    // consume partial kwargs
                    []<size_t... Ks>(
                        std::index_sequence<Ks...>,
                        map& out,
                        auto&& parts
                    ) {
                        (out.emplace(
                            Partial::template name<K + Ks>,
                            std::forward<decltype(parts)>(
                                parts
                            ).template get<K + Ks>()
                        ), ...);
                    }(
                        std::make_index_sequence<consecutive<K>>{},
                        out,
                        std::forward<P>(parts)
                    );

                    // consume vectorcall kwargs
                    for (auto& keyword : kwargs) {
                        out.emplace(
                            keyword.name,
                            reinterpret_borrow<Object>(keyword.value)
                        );
                    }
                    return out;
                }
            };
            template <size_t I, size_t J, size_t K>
                requires (
                    I < base::size() &&
                    !(K < Partial::size() && Partial::template rfind<K> == I)
                )
            struct call<I, J, K> {  // insert Python argument(s) or default value

                template <
                    impl::inherits<Partial> P,
                    impl::inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (signature::invocable<F>)
                static std::invoke_result_t<F, Args...> invoke(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    A&&... args
                ) {
                    using T = signature::at<I>;
                    constexpr static_str name = ArgTraits<T>::name;

                    // positional-only
                    if constexpr (ArgTraits<T>::posonly()) {
                        if (idx < nargs) {
                            return call<I + 1, J, K>::invoke(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                ++idx,
                                nargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    array[idx - 1]
                                ))
                            );
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        if constexpr (name.empty()) {
                            throw TypeError(
                                "no match for positional-only parameter at "
                                "index " + static_str<>::from_int<I>
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + static_str<>::from_int<I>
                            );
                        }

                    // positional-or-keyword
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if (idx < nargs) {
                            return call<I + 1, J, K>::invoke(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                ++idx,
                                nargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    array[idx - 1]
                                ))
                            );
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            static_str<>::from_int<I>
                        );

                    // keyword-only
                    } else if constexpr (ArgTraits<T>::kw()) {
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            static_str<>::from_int<I>
                        );

                    // variadic positional
                    } else if constexpr (ArgTraits<T>::args()) {
                        return call<I + 1, J, K>::invoke(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_positional(
                                std::forward<P>(parts),
                                array,
                                idx,
                                nargs
                            ))
                        );

                    // variadic keyword
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return call<I + 1, J, K>::invoke(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            std::forward<A>(args)...,
                            to_arg<I>(std::unordered_map<
                                std::string,
                                typename ArgTraits<T>::type
                            >{})
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }
                }

                template <
                    impl::inherits<Partial> P,
                    impl::inherits<Defaults> D,
                    typename F,
                    typename... A
                >
                    requires (signature::invocable<F>)
                static std::invoke_result_t<F, Args...> invoke_with_keywords(
                    P&& parts,
                    D&& defaults,
                    F&& func,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs,
                    Kwargs& kwargs,
                    A&&... args
                ) {
                    using T = signature::at<I>;
                    constexpr static_str name = ArgTraits<T>::name;

                    // positional-only
                    if constexpr (ArgTraits<T>::posonly()) {
                        if (idx < nargs) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                ++idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    array[idx - 1]
                                ))
                            );
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        if constexpr (name.empty()) {
                            throw TypeError(
                                "no match for positional-only parameter at "
                                "index " + static_str<>::from_int<I>
                            );
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter '" +
                                name + "' at index " + static_str<>::from_int<I>
                            );
                        }

                    // positional-or-keyword
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if (idx < nargs) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                ++idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    array[idx - 1]
                                ))
                            );
                        }
                        auto node = kwargs.extract(std::string_view(name));
                        if (node) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    node.value().value
                                ))
                            );
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            static_str<>::from_int<I>
                        );

                    // keyword-only
                    } else if constexpr (ArgTraits<T>::kw()) {
                        auto node = kwargs.extract(std::string_view(name));
                        if (node) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(reinterpret_borrow<Object>(
                                    node.value().value
                                ))
                            );
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return call<I + 1, J, K>::invoke_with_keywords(
                                std::forward<P>(parts),
                                std::forward<D>(defaults),
                                std::forward<F>(func),
                                array,
                                idx,
                                nargs,
                                kwargs,
                                std::forward<A>(args)...,
                                to_arg<I>(
                                    std::forward<D>(defaults).template get<I>()
                                )
                            );
                        }
                        throw TypeError(
                            "no match for parameter '" + name + "' at index " +
                            static_str<>::from_int<I>
                        );

                    // variadic positional
                    } else if constexpr (ArgTraits<T>::args()) {
                        return call<I + 1, J, K>::invoke_with_keywords(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            kwargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_positional(
                                std::forward<P>(parts),
                                array,
                                idx,
                                nargs
                            ))
                        );

                    // variadic keyword
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return call<I + 1, J, K>::invoke_with_keywords(
                            std::forward<P>(parts),
                            std::forward<D>(defaults),
                            std::forward<F>(func),
                            array,
                            idx,
                            nargs,
                            kwargs,
                            std::forward<A>(args)...,
                            to_arg<I>(variadic_keywords(
                                std::forward<P>(parts),
                                kwargs
                            ))
                        );

                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }
                }

            private:

                template <typename P, typename... A>
                static auto variadic_positional(
                    P&& parts,
                    PyObject* const* array,
                    size_t idx,
                    size_t nargs
                ) {
                    using T = signature::at<I>;

                    // allocate variadic positional array
                    using vec = std::vector<typename ArgTraits<T>::type>;
                    vec out;
                    out.reserve(idx < nargs ? nargs - idx : 0);

                    // consume vectorcall args
                    for (size_t i = idx; idx < nargs; ++i) {
                        out.emplace_back(
                            reinterpret_borrow<Object>(array[i])
                        );
                    }
                    return out;
                }

                template <typename P>
                static auto variadic_keywords(P&& parts, Kwargs& kwargs) {
                    using T = signature::at<I>;

                    // allocate variadic keyword map
                    using map = std::unordered_map<std::string, typename ArgTraits<T>::type>;
                    map out;
                    out.reserve(kwargs.size());

                    // consume vectorcall kwargs
                    auto it = kwargs.begin();
                    auto end = kwargs.end();
                    while (it != end) {
                        // postfix ++ required to increment before invalidation
                        auto node = kwargs.extract(it++);
                        out.emplace_back(
                            node.value().name,
                            reinterpret_borrow<Object>(node.value().value)
                        );
                    }
                    return out;
                }
            };

        public:
            size_t kwcount;
            size_t nargs;
            size_t flags;
            py::Object kwnames;
            PyObject** storage;
            PyObject* const* array;
            size_t hash;

            /* Directly initialize the vectorcall array.  This constructor is mostly for
            internal use, in order to speed up nested calls with modified signatures. */
            Vectorcall(
                size_t kwcount,
                size_t nargs,
                size_t flags,
                const py::Object& kwnames,
                PyObject** storage,
                PyObject* const* array,
                size_t hash
            ) :
                kwcount(kwcount),
                nargs(nargs),
                flags(flags),
                kwnames(kwnames),
                storage(storage),
                array(array),
                hash(hash)
            {}

            /* Adopt a denormalized vectorcall array directly from Python, without any
            extra work.  Such an array can be directly called along with partial arguments
            to avoid extra allocations during the call procedure. */
            Vectorcall(PyObject* const* args, size_t nargsf, PyObject* kwnames) :
                nargs(PyVectorcall_NARGS(nargsf)),
                kwcount(kwnames ? PyTuple_GET_SIZE(kwnames) : 0),
                flags(nargsf - nargs),
                kwnames(py::reinterpret_borrow<py::Object>(kwnames)),
                storage(nullptr),
                array(args - offset()),
                hash(0)
            {}

            /* Build a normalized vectorcall array from C++.  Note that this will always
            involve a heap allocation for the array itself, since the possible presence of
            parameter packs in the source arguments means its size cannot be known at
            compile time. */
            template <inherits<Partial> P, typename... A>
                requires (
                    Bind<A...>::proper_argument_order &&
                    Bind<A...>::no_qualified_arg_annotations &&
                    Bind<A...>::no_duplicate_args &&
                    Bind<A...>::no_extra_positional_args &&
                    Bind<A...>::no_extra_keyword_args &&
                    Bind<A...>::no_conflicting_values &&
                    Bind<A...>::can_convert &&
                    Bind<A...>::satisfies_required_args
                )
            Vectorcall(P&& parts, A&&... args) :
                kwcount([](auto&&... args) {
                    using Source = signature<Vectorcall(A...)>;
                    constexpr size_t base =
                        call<0, 0, 0>::template n_partial_kw<A...>() + Source::n_kw;
                    if constexpr (Source::has_kwargs) {
                        return base + impl::unpack_arg<Source::kwargs_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size();
                    }
                    return base;
                }(std::forward<A>(args)...)),
                nargs([](auto&&... args) {
                    using Source = signature<Return(A...)>;
                    constexpr size_t base =
                        (Partial::n - call<0, 0, 0>::template n_partial_kw<A...>()) +
                        Source::n_pos;
                    if constexpr (Source::has_args) {
                        return base + impl::unpack_arg<Source::args_idx>(
                            std::forward<decltype(args)>(args)...
                        ).size();
                    }
                    return base;
                }(std::forward<A>(args)...)),
                flags(PY_VECTORCALL_ARGUMENTS_OFFSET | Flags::NORMALIZED),
                kwnames([](size_t kwcount) {
                    if (kwcount) {
                        PyObject* kwnames = PyTuple_New(kwcount);
                        if (kwnames == nullptr) {
                            Exception::from_python();
                        }
                        return reinterpret_steal<Object>(kwnames);
                    } else {
                        return reinterpret_steal<Object>(nullptr);
                    }
                }(kwcount)),
                storage(new PyObject*[nargs + kwcount + 1]),
                array(storage),
                hash(0)
            {
                size_t idx = 0;
                size_t kw_idx = 0;
                try {
                    storage[0] = nullptr;
                    invoke_with_packs(
                        [](
                            PyObject** array,
                            size_t& idx,
                            PyObject* kwnames,
                            size_t& kw_idx,
                            size_t& hash,
                            auto&& parts,
                            auto&&... args
                        ) {
                            call<0, 0, 0>::create(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(args)>(args)...
                            );
                        },
                        storage + 1,
                        idx,
                        ptr(kwnames),
                        kw_idx,
                        hash,
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                } catch (...) {
                    for (size_t i = 1; i <= idx; ++i) {
                        Py_DECREF(storage[i]);
                    }
                    delete[] storage;
                    throw;
                }
            }

            /* Build a normalized vectorcall array from C++.  This is identical to the
            generalized C++ constructor except that the underlying array is stack-allocated
            and managed in an external scope, instead of using the heap.  The incoming
            arguments must not include any parameter packs, so that the size of the array
            can be verified at compile time.  Using this constructor where possible avoids
            an extra heap allocation, thereby improving performance.  The user must ensure
            that the lifetime of the array exceeds that of the Vectorcall arguments, and
            any existing contents stored in the array will be overwritten. */
            template <impl::inherits<Partial> P, typename... A>
                requires (
                    !(impl::arg_pack<A> || ...) &&
                    !(impl::kwarg_pack<A> || ...) &&
                    signature::args_are_convertible_to_python &&
                    signature::return_is_convertible_to_python &&
                    Bind<A...>::proper_argument_order &&
                    Bind<A...>::no_qualified_arg_annotations &&
                    Bind<A...>::no_duplicate_args &&
                    Bind<A...>::no_extra_positional_args &&
                    Bind<A...>::no_extra_keyword_args &&
                    Bind<A...>::no_conflicting_values &&
                    Bind<A...>::can_convert &&
                    Bind<A...>::satisfies_required_args
                )
            Vectorcall(
                std::array<PyObject*, Partial::n + sizeof...(A) + 1>& out,
                P&& parts,
                A&&... args
            ) :
                kwcount(
                    call<0, 0, 0>::template n_partial_kw<A...>() +
                    signature<Vectorcall(A...)>::n_kw
                ),
                nargs(Partial::n + sizeof...(A) - kwcount),
                flags(PY_VECTORCALL_ARGUMENTS_OFFSET | Flags::NORMALIZED),
                kwnames([](size_t kwcount) {
                    if (kwcount) {
                        PyObject* kwnames = PyTuple_New(kwcount);
                        if (kwnames == nullptr) {
                            Exception::from_python();
                        }
                        return reinterpret_steal<Object>(kwnames);
                    } else {
                        return reinterpret_steal<Object>(nullptr);
                    }
                }(kwcount)),
                storage(nullptr),
                array(out.data()),
                hash(0)
            {
                size_t idx = 0;
                size_t kw_idx = 0;
                try {
                    out[0] = nullptr;
                    invoke_with_packs(
                        [](
                            PyObject** array,
                            size_t& idx,
                            PyObject* kwnames,
                            size_t& kw_idx,
                            size_t& hash,
                            auto&& parts,
                            auto&&... args
                        ){
                            call<0, 0, 0>::create(
                                array,
                                idx,
                                kwnames,
                                kw_idx,
                                hash,
                                std::forward<decltype(parts)>(parts),
                                std::forward<decltype(args)>(args)...
                            );
                        },
                        out.data() + 1,
                        idx,
                        ptr(kwnames),
                        kw_idx,
                        hash,
                        std::forward<P>(parts),
                        std::forward<A>(args)...
                    );
                } catch (...) {
                    for (size_t i = 1; i <= idx; ++i) {
                        Py_DECREF(out[i]);
                    }
                    throw;
                }
            }

            Vectorcall(const Vectorcall& other) :
                kwcount(other.kwcount),
                nargs(other.nargs),
                flags(other.flags),
                kwnames(other.kwnames),
                storage(nullptr),
                array(other.array),
                hash(other.hash)
            {
                if (other.storage) {
                    size_t size = other.nargs + other.kwcount;
                    storage = new PyObject*[size + 1];
                    storage[0] = nullptr;
                    for (size_t i = 1; i <= size; ++i) {
                        storage[i] = Py_NewRef(other.storage[i]);
                    }
                    array = storage;
                }
            }

            Vectorcall(Vectorcall&& other) :
                kwcount(other.kwcount),
                nargs(other.nargs),
                flags(other.flags),
                kwnames(other.kwnames),
                storage(other.storage),
                array(other.array),
                hash(other.hash)
            {
                other.kwcount = 0;
                other.nargs = 0;
                other.flags = 0;
                other.kwnames = nullptr;
                other.storage = nullptr;
                other.array = nullptr;
                other.hash = 0;
            }

            Vectorcall& operator=(const Vectorcall& other) {
                if (this != &other) {
                    if (storage) {
                        for (size_t i = 1, n = nargs + kwcount; i <= n; ++i) {
                            Py_DECREF(storage[i]);
                        }
                        delete[] storage;
                        storage = nullptr;
                    }
                    kwcount = other.kwcount;
                    nargs = other.nargs;
                    flags = other.flags;
                    kwnames = other.kwnames;
                    if (other.storage) {
                        size_t size = other.nargs + other.kwcount;
                        storage = new PyObject*[size + 1];
                        storage[0] = nullptr;
                        for (size_t i = 1; i <= size; ++i) {
                            storage[i] = Py_NewRef(other.storage[i]);
                        }
                        array = storage;
                    } else {
                        array = other.array;
                    }
                    hash = other.hash;
                }
                return *this;
            }

            Vectorcall& operator=(Vectorcall&& other) {
                if (this != &other) {
                    if (storage) {
                        for (size_t i = 1, n = nargs + kwcount; i <= n; ++i) {
                            Py_DECREF(storage[i]);
                        }
                        delete[] storage;
                        storage = nullptr;
                    }
                    kwcount = other.kwcount;
                    nargs = other.nargs;
                    flags = other.flags;
                    kwnames = other.kwnames;
                    storage = other.storage;
                    array = other.array;
                    hash = other.hash;
                    other.kwcount = 0;
                    other.nargs = 0;
                    other.flags = 0;
                    other.kwnames = reinterpret_steal<Object>(nullptr);
                    other.storage = nullptr;
                    other.array = nullptr;
                    other.hash = 0;
                }
                return *this;
            }

            ~Vectorcall() noexcept {
                if (storage) {
                    for (size_t i = 1, n = nargs + kwcount; i <= n; ++i) {
                        Py_DECREF(storage[i]);
                    }
                    delete[] storage;
                }
            }

            /* Recombine the positional argument count with the encoded Python flags,
            leaving out any extra bertrand flags. */
            [[nodiscard]] size_t nargsf() const noexcept {
                return nargs + (flags & ~Flags::ALL);
            }

            /* Indicates whether the `PY_VECTORCALL_ARGUMENTS_OFFSET` flag is set, meaning
            that an extra first element is prepended to the array, which can contain a
            forwarded `self` argument to make downstream calls more efficient. */
            [[nodiscard]] bool offset() const noexcept {
                return flags & PY_VECTORCALL_ARGUMENTS_OFFSET;
            }

            /* A lightweight, trivially-destructible representation of a single parameter
            in the vectorcall array, as returned by the index operator. */
            struct Param {
                std::string_view name;
                PyObject* const value;
                impl::ArgKind kind;

                constexpr bool posonly() const noexcept { return kind.posonly(); }
                constexpr bool pos() const noexcept { return kind.pos(); }
                constexpr bool args() const noexcept { return kind.args(); }
                constexpr bool kwonly() const noexcept { return kind.kwonly(); }
                constexpr bool kw() const noexcept { return kind.kw(); }
                constexpr bool kwargs() const noexcept { return kind.kwargs(); }
                constexpr bool opt() const noexcept { return kind.opt(); }
                constexpr bool variadic() const noexcept { return kind.variadic(); }

                /* Compute a hash of this parameter's name, type, and kind, using the given
                FNV-1a hash seed and prime. */
                size_t hash() const noexcept {
                    return arg_hash(name.data(), value, kind);
                }
            };

            /* Index into the vectorcall array, returning an individual argument as a
            lightweight Param struct.  An IndexError will be thrown if the index is out of
            range for the vectorcall array. */
            [[nodiscard]] Param operator[](size_t i) const {
                if (i < nargs) {
                    return {
                        .name = std::string_view{"", 0},
                        .value = array[i + offset()],
                        .kind = impl::ArgKind::POS
                    };
                }
                if (i < size()) {
                    return {
                        .name = arg_name(PyTuple_GET_ITEM(ptr(kwnames), i - nargs)),
                        .value = array[i + offset()],
                        .kind = impl::ArgKind::KW
                    };
                }
                throw IndexError(std::to_string(i));
            }

            /* A random access iterator over the parameters within the vectorcall array. */
            struct Iterator {
            private:

                const Vectorcall* self;
                int idx;
                mutable int cache_idx = std::numeric_limits<int>::min();
                mutable Param cache;

            public:
                using iterator_category = std::random_access_iterator_tag;
                using difference_type = int;
                using value_type = Param;
                using pointer = const Param*;
                using reference = Param;  // valid for read-only iterators

                Iterator(const Vectorcall* self, int idx) : self(self), idx(idx) {}

                reference operator*() const {
                    return (*self)[idx];
                }

                pointer operator->() const {
                    if (idx != cache_idx) {
                        cache = (*self)[idx];
                    }
                    return &cache;
                }

                reference operator[](difference_type n) const {
                    return (*self)[idx + n];
                }

                Iterator& operator++() {
                    ++idx;
                    return *this;
                }

                Iterator operator++(int) {
                    Iterator copy = *this;
                    ++*this;
                    return copy;
                }

                Iterator& operator+=(difference_type n) {
                    idx += n;
                    return *this;
                }

                friend Iterator operator+(const Iterator& lhs, difference_type rhs) {
                    return {lhs.self, lhs.idx + rhs};
                }

                friend Iterator operator+(difference_type lhs, const Iterator& rhs) {
                    return {rhs.self, lhs + rhs.idx};
                }

                Iterator& operator--() {
                    --idx;
                    return *this;
                }

                Iterator operator--(int) {
                    Iterator copy = *this;
                    --*this;
                    return copy;
                }

                Iterator& operator-=(difference_type n) {
                    idx -= n;
                    return *this;
                }

                friend Iterator operator-(const Iterator& lhs, difference_type rhs) {
                    return {lhs.self, lhs.idx - rhs};
                }

                friend Iterator operator-(difference_type lhs, const Iterator& rhs) {
                    return {rhs.self, lhs - rhs.idx};
                }

                friend difference_type operator-(const Iterator& lhs, const Iterator& rhs) {
                    return lhs.idx - rhs.idx;
                }

                friend auto operator<=>(const Iterator& lhs, const Iterator& rhs) {
                    return lhs.idx <=> rhs.idx;
                }
            };
            using ReverseIterator = std::reverse_iterator<Iterator>;

            [[nodiscard]] size_t size() const noexcept { return nargs + kwcount; }
            [[nodiscard]] bool empty() const noexcept { return size() == 0; }
            [[nodiscard]] Iterator begin() const { return {this, 0}; }
            [[nodiscard]] Iterator cbegin() const { return {this, 0}; }
            [[nodiscard]] Iterator end() const { return {this, size()}; }
            [[nodiscard]] Iterator cend() const { return {this, size()}; }
            [[nodiscard]] ReverseIterator rbegin() const { return {end()}; }
            [[nodiscard]] ReverseIterator crbegin() const { return {cend()}; }
            [[nodiscard]] ReverseIterator rend() const { return {begin()}; }
            [[nodiscard]] ReverseIterator crend() const { return {cbegin()}; }

            /* Check whether the Python arguments have been normalized, and therefore
            include any partial arguments, with all arguments having proper bertrand
            types. */
            [[nodiscard]] bool normalized() const noexcept {
                return flags & Flags::NORMALIZED;
            }

            /// TODO: it's not enough for normalize() to convert to the types in the
            /// enclosing signature, it has to actually convert to a true bertrand type
            /// at all times.  That's the only way we can guarantee that overload
            /// resolution can proceed with the correct type information.  The only way
            /// to do this is to import the `bertrand` module and call it to do the
            /// conversion.  I'll probably have to revisit this when I get back to
            /// implementing modules, as well as any existing code that imports Bertrand
            /// at the Python level.

            /* Convert the Python arguments into equivalent Bertrand types and insert any
            partial arguments in order to form a stable overload key with a consistent
            hash, suitable for trie traversal.  This method operates by side effect, such
            that the `array`, `nargs`, `flags`, and `kwnames` members can then be used to
            directly invoke a Python function via the vectorcall protocol.  Any existing
            iterators will be invalidated when this method is called. */
            template <impl::inherits<Partial> P>
                requires (
                    signature::args_are_convertible_to_python &&
                    signature::return_is_convertible_to_python
                )
            [[maybe_unused]] Vectorcall& normalize(P&& parts) {
                if (normalized()) {
                    return *this;
                }
                size_t n = nargs + kwcount + Partial::n;
                size_t n_kw = kwcount + call<0, 0, 0>::n_partial_kw(0, nargs);
                size_t idx = 0;
                size_t kw_idx = 0;
                size_t old_idx = 0;
                Object old_kwnames = std::move(kwnames);
                storage = new PyObject*[n + 1];
                try {
                    storage[0] = nullptr;
                    if (n_kw) {
                        Kwargs kwargs {array, nargs, ptr(old_kwnames), kwcount};
                        kwnames = reinterpret_steal<Object>(PyTuple_New(n_kw));
                        call<0, 0, 0>::normalize(
                            storage,
                            idx,
                            ptr(kwnames),
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            array + offset(),
                            old_idx,
                            nargs,
                            kwargs
                        );
                    } else {
                        call<0, 0, 0>::normalize(
                            storage,
                            idx,
                            nullptr,
                            kw_idx,
                            hash,
                            std::forward<P>(parts),
                            array + offset(),
                            old_idx,
                            nargs
                        );
                    }
                } catch (...) {
                    for (size_t i = 1; i <= idx; ++i) {
                        Py_DECREF(storage[i]);
                    }
                    delete[] storage;
                    storage = nullptr;
                    kwnames = std::move(old_kwnames);
                    hash = 0;
                    throw;
                }
                array = storage;
                nargs = n - n_kw;
                kwcount = n_kw;
                flags |= (PY_VECTORCALL_ARGUMENTS_OFFSET | Flags::NORMALIZED);
                return *this;
            }

            /* Assert that the arguments satisfy the enclosing signature, raising an
            appropriate TypeError if there are any mismatches.  Normally, such validation
            is done automatically as part of the normal call operator, but in the case of
            overloads, it is possible for an empty iterator to be returned upon searching
            with a vectorcall array.  If that occurs, then this method is called to
            diagnose the problem, without contributing any overhead to the rest of the call
            logic. */
            void validate() const {
                using Required = inspect::Required;
                constexpr signature sig;
                Required mask;
                size_t offset = this->offset();

                // validate positional arguments
                for (size_t i = 0; i < nargs; ++i) {
                    constexpr size_t cutoff = std::min({
                        signature::args_idx,
                        signature::kwonly_idx,
                        signature::kwargs_idx
                    });
                    Object value = reinterpret_borrow<Object>(array[i + offset]);
                    const Callback* callback;
                    if (i < cutoff) {
                        callback = &positional_table[i];
                        mask |= Required(1) << callback->index;
                    } else {
                        if constexpr (signature::has_args) {
                            callback = &positional_table[signature::args_idx];
                        } else {
                            throw TypeError(
                                "received unexpected positional argument at index " +
                                std::to_string(i)
                            );
                        }
                    }
                    if (!callback->isinstance(value)) {
                        throw TypeError(
                            "expected positional argument at index " +
                            std::to_string(i) + " to be a subclass of '" +
                            repr(callback->type()) + "', not: '" +
                            repr(value) + "'"
                        );
                    }
                }

                // validate keyword arguments
                for (size_t i = 0, transition = offset + nargs; i < kwcount; ++i) {
                    Object name = reinterpret_borrow<Object>(
                        PyTuple_GET_ITEM(ptr(kwnames), i)
                    );
                    Object value = reinterpret_borrow<Object>(
                        array[transition + i]
                    );
                    const Callback* callback = name_table[arg_name(ptr(name))];
                    if (!callback) {
                        if constexpr (signature::has_kwargs) {
                            callback = &positional_table[signature::kwargs_idx];
                        } else {
                            throw TypeError(
                                "received unexpected keyword argument '" +
                                repr(name) + "'"
                            );
                        }
                    } else if (!callback->kw()) {
                        throw TypeError(
                            "received unexpected keyword argument '" +
                            repr(name) + "'"
                        );
                    } else {
                        Required temp = Required(1) << callback->index;
                        if ((mask & temp)) {
                            throw TypeError(
                                "received multiple values for argument '" +
                                repr(name) + "'"
                            );
                        }
                        mask |= temp;
                    }
                    if (!callback->isinstance(array[i + nargs])) {
                        throw TypeError(
                            "expected keyword argument '" + repr(name) +
                            "' to be a subclass of '" + repr(callback->type()) +
                            "', not: '" + repr(value) + "'"
                        );
                    }
                }

                // check for missing required arguments
                mask &= signature::required;
                if (mask != signature::required) {
                    Required missing = signature::required & ~mask;
                    std::string msg = "missing required arguments: [";
                    size_t i = 0;
                    while (i < signature::n) {
                        if (missing & (Required(1) << i)) {
                            const Callback& callback = positional_table[i];
                            if (callback.name.empty()) {
                                msg += "<parameter " + std::to_string(i) + ">";
                            } else {
                                msg += "'" + std::string(callback.name) + "'";
                            }
                            ++i;
                            break;
                        }
                        ++i;
                    }
                    while (i < signature::n) {
                        if (missing & (Required(1) << i)) {
                            const Callback& callback = positional_table[i];
                            if (callback.name.empty()) {
                                msg += ", <parameter " + std::to_string(i) + ">";
                            } else {
                                msg += ", '" + std::string(callback.name) + "'";
                            }
                        }
                        ++i;
                    }
                    msg += "]";
                    throw TypeError(msg);
                }
            }

            /* Invoke a C++ function using denormalized vectorcall arguments by providing
            a separate partial tuple which will be directly merged into the C++ argument
            list, without any extra allocations. */
            template <impl::inherits<Partial> P, impl::inherits<Defaults> D, typename F>
                requires (signature::invocable<F>)
            Return operator()(P&& parts, D&& defaults, F&& func) const {
                if (kwcount) {
                    Kwargs kwargs {array, nargs, ptr(kwnames), kwcount};
                    return call<0, 0, 0>::invoke_with_keywords(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        array,
                        0,
                        nargs,
                        kwargs
                    );
                } else {
                    return call<0, 0, 0>::invoke(
                        std::forward<P>(parts),
                        std::forward<D>(defaults),
                        std::forward<F>(func),
                        array,
                        0,
                        nargs
                    );
                }
            }

            /* Invoke a C++ function using pre-normalized vectorcall arguments,
            disregarding partials. */
            template <impl::inherits<Defaults> D, typename F>
                requires (signature::invocable<F>)
            Return operator()(D&& defaults, F&& func) const {
                return typename signature::Unbind::Vectorcall{
                    kwcount,
                    nargs,
                    flags,
                    kwnames,
                    nullptr,
                    array,
                    hash
                }(
                    signature::Unbind::partial(),
                    std::forward<D>(defaults),
                    std::forward<F>(func)
                );
            }

            /* Invoke a Python function using normalized vectorcall arguments, converting
            the result to the expected return type. */
            Return operator()(PyObject* func) const {
                Object result = reinterpret_steal<Object>(PyObject_Vectorcall(
                    func,
                    array,
                    nargsf(),
                    ptr(kwnames)
                ));
                if (result.is(nullptr)) {
                    Exception::from_python();
                }
                if constexpr (!std::is_void_v<Return>) {
                    return result;
                }
            }

            /* Convert a normalized vectorcall array into a standardized key type that can
            be used to initiate a search of an overload trie. */
            [[nodiscard]] impl::OverloadKey overload_key() const {
                if (!normalized()) {
                    throw AssertionError(
                        "vectorcall arguments must be normalized before generating an "
                        "overload key"
                    );
                }
                impl::OverloadKey key{
                    .vec = {},
                    .hash = hash,
                    .has_hash = true
                };
                key->reserve(size());
                for (Param param : *this) {
                    key->emplace_back(
                        param.name,
                        reinterpret_borrow<Object>(param.value).
                        param.kind
                    );
                }
                return key;
            }

            /// TODO: after producing this key, I would specialize `bertrand.Function[]`
            /// accordingly, and then pass the normalized vectorcall arguments to its
            /// standard Python constructor, and everything should work as expected.

            /* Convert a normalized vectorcall array into a template key that can be used
            to specialize the `bertrand.Function` type on the Python side.  This is used to
            implement the `Function.bind()` method in Python, which produces a partial
            function with the given arguments already filled in.  That method is chainable,
            meaning that any existing partial arguments (inserted during normalization)
            will be carried over into the new partial function object. */
            [[nodiscard]] Object template_key() const {
                if (!normalized()) {
                    throw AssertionError(
                        "vectorcall arguments must be normalized before generating a "
                        "template key"
                    );
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object result = reinterpret_steal<Object>(
                    PyTuple_New(signature::size() + 1)
                );
                if (result.is(nullptr)) {
                    Exception::from_python();
                }

                // first element describes the return type
                if constexpr (std::is_void_v<Return>) {
                    PyTuple_SET_ITEM(ptr(result), 0, Py_NewRef(Py_None));
                } else {
                    PyTuple_SET_ITEM(ptr(result), 0, release(Type<Return>()));
                }

                // remaining are parameters, expressed as specializations of `bertrand.Arg`
                Kwargs kwargs{array, nargs, kwnames, kwcount};
                []<size_t I = 0>(
                    this auto&& recur,
                    const Vectorcall& self,
                    const Object& Arg,
                    const Object& BoundArg,
                    PyObject* result,
                    size_t tuple_idx,
                    size_t pos_idx,
                    size_t nargs,
                    Kwargs& kwargs
                ) {
                    if constexpr (I < signature::n) {
                        using T = signature::at<I>;

                        // positional-only arguments may be anonymous
                        if constexpr (ArgTraits<T>::name.empty()) {
                            if (pos_idx < nargs) {
                                Param param = self[pos_idx++];
                                PyTuple_SET_ITEM(result, tuple_idx++, release(BoundArg[
                                    Type<typename ArgTraits<T>::type>(),
                                    reinterpret_steal<Object>(
                                        PyObject_Type(param.value)
                                    )
                                ]));
                            } else {
                                PyTuple_SET_ITEM(result, tuple_idx++, release(
                                    Type<typename ArgTraits<T>::type>()
                                ));
                            }

                        // everything else is converted to a specialization of `bertrand.Arg`
                        } else {
                            Object specialization = reinterpret_steal<Object>(nullptr);

                            if constexpr (ArgTraits<T>::posonly()) {
                                specialization = getattr<"pos">(Arg[
                                    impl::template_string<ArgTraits<T>::name>(),
                                    Type<typename ArgTraits<T>::type>()
                                ]);
                                if constexpr (ArgTraits<T>::opt()) {
                                    specialization = getattr<"opt">(specialization);
                                }
                                if (pos_idx < nargs) {
                                    Param param = self[pos_idx++];
                                    specialization = BoundArg[
                                        specialization,
                                        reinterpret_steal<Object>(
                                            PyObject_Type(param.value)
                                        )
                                    ];
                                }

                            } else if constexpr (ArgTraits<T>::pos()) {
                                specialization = Arg[
                                    impl::template_string<ArgTraits<T>::name>(),
                                    Type<typename ArgTraits<T>::type>()
                                ];
                                if constexpr (ArgTraits<T>::opt()) {
                                    specialization = getattr<"opt">(specialization);
                                }
                                if (pos_idx < nargs) {
                                    Param param = self[pos_idx++];
                                    specialization = BoundArg[
                                        specialization,
                                        reinterpret_steal<Object>(
                                            PyObject_Type(param.value)
                                        )
                                    ];
                                } else if (auto node = kwargs.extract(
                                    std::string_view(ArgTraits<T>::name)
                                )) {
                                    specialization = BoundArg[
                                        specialization,
                                        reinterpret_steal<Object>(
                                            PyObject_Type(node.value().value)
                                        )
                                    ];
                                }

                            } else if constexpr (ArgTraits<T>::kw()) {
                                specialization = getattr<"kw">(Arg[
                                    impl::template_string<ArgTraits<T>::name>(),
                                    Type<typename ArgTraits<T>::type>()
                                ]);
                                if constexpr (ArgTraits<T>::opt()) {
                                    specialization = getattr<"opt">(specialization);
                                }
                                if (auto node = kwargs.extract(
                                    std::string_view(ArgTraits<T>::name)
                                )) {
                                    specialization = BoundArg[
                                        specialization,
                                        reinterpret_steal<Object>(
                                            PyObject_Type(node.value().value)
                                        )
                                    ];
                                }

                            } else if constexpr (ArgTraits<T>::args()) {
                                specialization = Arg[
                                    impl::template_string<"*" + ArgTraits<T>::name>(),
                                    Type<typename ArgTraits<T>::type>()
                                ];
                                if (pos_idx < nargs) {
                                    Object tuple = reinterpret_steal<Object>(
                                        PyTuple_New(nargs - pos_idx + 1)
                                    );
                                    if (tuple.is(nullptr)) {
                                        Exception::from_python();
                                    }
                                    PyTuple_SET_ITEM(
                                        ptr(tuple),
                                        0,
                                        release(specialization)
                                    );
                                    size_t start = pos_idx;
                                    while (pos_idx < nargs) {
                                        Param param = self[pos_idx++];
                                        PyTuple_SET_ITEM(
                                            ptr(tuple),
                                            pos_idx - start,
                                            PyObject_Type(param.value)
                                        );
                                    }
                                    specialization = BoundArg[tuple];
                                }

                            } else if constexpr (ArgTraits<T>::kwargs()) {
                                specialization = Arg[
                                    impl::template_string<"**" + ArgTraits<T>::name>(),
                                    Type<typename ArgTraits<T>::type>()
                                ];
                                auto it = kwargs.begin();
                                auto end = kwargs.end();
                                if (it != end) {
                                    Object tuple = reinterpret_steal<Object>(
                                        PyTuple_New(kwargs.size() + 1)
                                    );
                                    if (tuple.is(nullptr)) {
                                        Exception::from_python();
                                    }
                                    size_t i = 0;
                                    PyTuple_SET_ITEM(
                                        ptr(tuple),
                                        i++,
                                        release(specialization)
                                    );
                                    while (it != end) {
                                        auto node = kwargs.extract(it++);
                                        PyTuple_SET_ITEM(
                                            ptr(tuple),
                                            i++,
                                            release(getattr<"kw">(Arg[
                                                reinterpret_borrow<Object>(
                                                    node.value().unicode
                                                ),
                                                reinterpret_steal<Object>(
                                                    node.value().value
                                                )
                                            ]))
                                        );
                                    }
                                    specialization = BoundArg[tuple];
                                }

                            } else {
                                static_assert(false, "invalid parameter kind");
                                std::unreachable();
                            }

                            PyTuple_SET_ITEM(result, tuple_idx++, release(specialization));
                        }

                        std::forward<decltype(recur)>(recur).template operator()<I + 1>(
                            self,
                            Arg,
                            BoundArg,
                            result,
                            tuple_idx,
                            pos_idx,
                            nargs,
                            kwargs
                        );
                    }
                }(
                    *this,
                    getattr<"Arg">(bertrand),
                    getattr<"BoundArg">(bertrand),
                    ptr(result),
                    1,
                    0,
                    nargs,
                    kwargs
                );

                return result;
            }
        };

        /* Adopt a vectorcall array from Python in denormalized form. */
        [[nodiscard]] static Vectorcall vectorcall(
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            return {args, nargsf, kwnames};
        }

        /* Construct a normalized vectorcall array from C++. */
        template <impl::inherits<Partial> P, typename... A>
            requires (
                args_are_convertible_to_python &&
                return_is_convertible_to_python &&
                Bind<A...>::proper_argument_order &&
                Bind<A...>::no_qualified_arg_annotations &&
                Bind<A...>::no_duplicate_args &&
                Bind<A...>::no_extra_positional_args &&
                Bind<A...>::no_extra_keyword_args &&
                Bind<A...>::no_conflicting_values &&
                Bind<A...>::satisfies_required_args &&
                Bind<A...>::can_convert
            )
        [[nodiscard]] static Vectorcall vectorcall(
            P&& partial,
            A&&... args
        ) {
            return {std::forward<P>(partial), std::forward<A>(args)...};
        }

        /* Construct a normalized vectorcall array from C++, placing the contents into a
        stack-allocated array which is modified as an out parameter.  This avoids an extra
        heap allocation for the argument array.  The user must ensure that the array
        outlives the resulting vectorcall object. */
        template <impl::inherits<Partial> P, typename... A>
            requires (
                !(impl::arg_pack<A> || ...) &&
                !(impl::kwarg_pack<A> || ...) &&
                args_are_convertible_to_python &&
                return_is_convertible_to_python &&
                Bind<A...>::proper_argument_order &&
                Bind<A...>::no_qualified_arg_annotations &&
                Bind<A...>::no_duplicate_args &&
                Bind<A...>::no_extra_positional_args &&
                Bind<A...>::no_extra_keyword_args &&
                Bind<A...>::no_conflicting_values &&
                Bind<A...>::satisfies_required_args &&
                Bind<A...>::can_convert
            )
        [[nodiscard]] static Vectorcall vectorcall(
            std::array<PyObject*, Partial::n + sizeof...(A) + 1>& out,
            P&& partial,
            A&&... args
        ) {
            return {out, std::forward<P>(partial), std::forward<A>(args)...};
        }

        /// TODO: call operators.

        /// TODO: perhaps I implement two versions of to_python(), one that includes
        /// partial arguments and one that doesn't?

        /* Produce a Python `inspect.Signature` object that matches this signature,
        allowing a corresponding function to be seamlessly introspected from Python. */
        template <impl::inherits<Defaults> D>
            requires (
                args_are_convertible_to_python &&
                return_is_convertible_to_python
            )
        [[nodiscard]] static Object to_python(D&& defaults) {
            Object inspect = reinterpret_steal<Object>(PyImport_Import(
                ptr(impl::template_string<"inspect">())
            ));
            if (inspect.is(nullptr)) {
                Exception::from_python();
            }

            // build the parameter annotations
            Object tuple = reinterpret_steal<Object>(PyTuple_New(signature::n));
            if (tuple.is(nullptr)) {
                Exception::from_python();
            }
            []<size_t I = 0>(
                this auto&& self,
                PyObject* tuple,
                auto&& defaults,
                const Object& Parameter
            ) {
                if constexpr (I < signature::n) {
                    using T = signature::at<I>;

                    if constexpr (ArgTraits<T>::posonly()) {
                        if constexpr (ArgTraits<T>::name.empty()) {
                            constexpr static_str name = "_" + static_str<>::from_int<I + 1>;
                            if constexpr (ArgTraits<T>::opt()) {
                                PyTuple_SET_ITEM(
                                    tuple,
                                    I,
                                    release(Parameter(
                                        arg<"name"> = impl::template_string<name>(),
                                        arg<"kind"> = getattr<"POSITIONAL_ONLY">(Parameter),
                                        arg<"annotation"> = Type<typename ArgTraits<T>::type>(),
                                        arg<"default"> = std::forward<D>(defaults).template get<I>()
                                    ))
                                );
                            } else {
                                PyTuple_SET_ITEM(
                                    tuple,
                                    I,
                                    release(Parameter(
                                        arg<"name"> = impl::template_string<name>(),
                                        arg<"kind"> = getattr<"POSITIONAL_ONLY">(Parameter),
                                        arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                                    ))
                                );
                            }
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                PyTuple_SET_ITEM(
                                    tuple,
                                    I,
                                    release(Parameter(
                                        arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                        arg<"kind"> = getattr<"POSITIONAL_ONLY">(Parameter),
                                        arg<"annotation"> = Type<typename ArgTraits<T>::type>(),
                                        arg<"default"> = std::forward<D>(defaults).template get<I>()
                                    ))
                                );
                            } else {
                                PyTuple_SET_ITEM(
                                    tuple,
                                    I,
                                    release(Parameter(
                                        arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                        arg<"kind"> = getattr<"POSITIONAL_ONLY">(Parameter),
                                        arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                                    ))
                                );
                            }
                        }
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (ArgTraits<T>::opt()) {
                            PyTuple_SET_ITEM(
                                tuple,
                                I,
                                release(Parameter(
                                    arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                    arg<"kind"> = getattr<"POSITIONAL_OR_KEYWORD">(Parameter),
                                    arg<"annotation"> = Type<typename ArgTraits<T>::type>(),
                                    arg<"default"> = std::forward<D>(defaults).template get<I>()
                                ))
                            );
                        } else {
                            PyTuple_SET_ITEM(
                                tuple,
                                I,
                                release(Parameter(
                                    arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                    arg<"kind"> = getattr<"POSITIONAL_OR_KEYWORD">(Parameter),
                                    arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                                ))
                            );
                        }
                    } else if constexpr (ArgTraits<T>::kwonly()) {
                        if constexpr (ArgTraits<T>::opt()) {
                            PyTuple_SET_ITEM(
                                tuple,
                                I,
                                release(Parameter(
                                    arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                    arg<"kind"> = getattr<"KEYWORD_ONLY">(Parameter),
                                    arg<"annotation"> = Type<typename ArgTraits<T>::type>(),
                                    arg<"default"> = std::forward<D>(defaults).template get<I>()
                                ))
                            );
                        } else {
                            PyTuple_SET_ITEM(
                                tuple,
                                I,
                                release(Parameter(
                                    arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                    arg<"kind"> = getattr<"KEYWORD_ONLY">(Parameter),
                                    arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                                ))
                            );
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        PyTuple_SET_ITEM(
                            tuple,
                            I,
                            release(Parameter(
                                arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                arg<"kind"> = getattr<"VAR_POSITIONAL">(Parameter),
                                arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                            ))
                        );
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        PyTuple_SET_ITEM(
                            tuple,
                            I,
                            release(Parameter(
                                arg<"name"> = impl::template_string<ArgTraits<T>::name>(),
                                arg<"kind"> = getattr<"VAR_KEYWORD">(Parameter),
                                arg<"annotation"> = Type<typename ArgTraits<T>::type>()
                            ))
                        );
                    } else {
                        static_assert(false, "invalid argument kind");
                        std::unreachable();
                    }

                    std::forward<decltype(self)>(self).template operator()<I + 1>(
                        tuple,
                        std::forward<decltype(defaults)>(defaults),
                        Parameter
                    );
                }
            }(
                ptr(tuple),
                std::forward<D>(defaults),
                getattr<"Parameter">(inspect)
            );

            // construct the signature object
            return getattr<"Signature">(inspect)(
                tuple,
                arg<"return_annotation"> = Type<Return>()
            );
        }

        /* Convert a C++ signature into a template key that can be used to specialize the
        `bertrand.Function` type on the Python side. */
        template <typename sig = signature>
            requires (
                sig::args_are_python &&
                sig::return_is_python
            )
        [[nodiscard]] static Object template_key() {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(impl::template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(
                PyTuple_New(signature::n + 1)
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }

            // first element describes the return type
            if constexpr (std::is_void_v<Return>) {
                PyTuple_SET_ITEM(ptr(result), 0, Py_NewRef(Py_None));
            } else {
                PyTuple_SET_ITEM(ptr(result), 0, release(Type<Return>()));
            }

            // remaining are parameters, expressed as specializations of `bertrand.Arg`
            []<size_t I = 0>(
                this auto&& self,
                const Object& Arg,
                const Object& BoundArg,
                PyObject* result
            ) {
                if constexpr (I < n) {
                    using T = at<I>;

                    // positional-only arguments may be anonymous
                    if constexpr (ArgTraits<T>::name.empty()) {
                        if constexpr (ArgTraits<T>::bound()) {
                            PyTuple_SET_ITEM(result, I + 1, release(
                                BoundArg[
                                    Type<typename ArgTraits<T>::type>(),
                                    Type<typename ArgTraits<T>::bound_to::template at<0>>()
                                ]
                            ));
                        } else {
                            PyTuple_SET_ITEM(result, I + 1, release(
                                Type<typename ArgTraits<T>::type>()
                            ));
                        }

                    // everything else is converted to a specialization of `bertrand.Arg`
                    } else {
                        Object str = reinterpret_steal<Object>(
                            PyUnicode_FromStringAndSize(
                                ArgTraits<T>::name.data(),
                                ArgTraits<T>::name.size()
                            )
                        );
                        if (str.is(nullptr)) {
                            Exception::from_python();
                        }
                        Object specialization = Arg[
                            str,
                            Type<typename ArgTraits<T>::type>()
                        ];

                        // apply positional/keyword/optional flags
                        if constexpr (ArgTraits<T>::posonly()) {
                            specialization = getattr<"pos">(specialization);
                            if constexpr (ArgTraits<T>::opt()) {
                                specialization = getattr<"opt">(specialization);
                            }
                        } else if constexpr (ArgTraits<T>::pos()) {
                            if constexpr (ArgTraits<T>::opt()) {
                                specialization = getattr<"opt">(specialization);
                            }
                        } else if constexpr (ArgTraits<T>::kw()) {
                            specialization = getattr<"kw">(specialization);
                            if constexpr (ArgTraits<T>::opt()) {
                                specialization = getattr<"opt">(specialization);
                            }
                        } else if constexpr (!(ArgTraits<T>::args() || ArgTraits<T>::kwargs())) {
                            static_assert(false, "invalid argument kind");
                            std::unreachable();
                        }

                        // append bound partial value(s) if present
                        if constexpr (ArgTraits<T>::bound()) {
                            specialization = []<size_t... Js>(
                                std::index_sequence<Js...>,
                                Object& specialization,
                                const Object& BoundArg
                            ) {
                                return BoundArg[
                                    specialization,
                                    Type<
                                        typename ArgTraits<T>::bound_to::template at<Js>
                                    >()...
                                ];
                            }(
                                std::make_index_sequence<ArgTraits<T>::bound_to::size()>{},
                                specialization,
                                BoundArg
                            );
                        }

                        PyTuple_SET_ITEM(result, I + 1, release(specialization));
                    }

                    // recur until all parameters are processed
                    std::forward<decltype(self)>(self).template operator()<I + 1>(
                        Arg,
                        result
                    );
                }
            }(
                getattr<"Arg">(bertrand),
                getattr<"BoundArg">(bertrand),
                ptr(result)
            );

            return result;
        }

    };

    /* Backs a C++ function whose return and argument types are already Python objects.
    This extends the convertible equivalent above to support dynamic overload tries,
    which are necessary to convert to the final `py::Function` type, which is a valid
    Python object that can be returned to a Python runtime. */
    template <typename Param, typename Return, typename... Args>
    struct PySignature : CppToPySignature<Param, Return, Args...> {
    private:
        using base = CppToPySignature<Param, Return, Args...>;

    public:

        /* A trie-based data structure containing a set of topologically-sorted,
        dynamic overloads for a Python function object, which are used to emulate
        C++-style function overloading.  Uses vectorcall arrays as search keys, meaning
        that only one conversion per argument is needed to both search the trie and
        invoke a matching overload, with a stable hash for efficient caching. */
        struct Overloads : impl::SignatureOverloadsTag {
            struct instance {
                static bool operator()(PyObject* obj, PyObject* cls) {
                    int rc = PyObject_IsInstance(obj, cls);
                    if (rc < 0) {
                        Exception::from_python();
                    }
                    return rc;
                }
            };

            struct subclass {
                static bool operator()(PyObject* obj, PyObject* cls) {
                    int rc = PyObject_IsSubclass(obj, cls);
                    if (rc < 0) {
                        Exception::from_python();
                    }
                    return rc;
                }
            };

        private:
            struct Node;
            using IDs = impl::bitset<MAX_OVERLOADS>;

            template <typename T>
            static constexpr bool valid_check =
                std::same_as<T, instance> || std::same_as<T, subclass>;

            struct Metadata {
                IDs id;
                inspect signature;

                size_t memory_usage() const noexcept {
                    return sizeof(IDs) + this->signature.memory_usage();
                }

                struct Hash {
                    using is_transparent = void;
                    static size_t operator()(const Metadata& self) noexcept {
                        return std::hash<IDs>{}(self.id);
                    }
                    static size_t operator()(const IDs& self) noexcept {
                        return std::hash<IDs>{}(self);
                    }
                };

                struct Equal {
                    using is_transparent = void;
                    static bool operator()(const Metadata& lhs, const Metadata& rhs) noexcept {
                        return lhs.id == rhs.id;
                    }
                    static bool operator()(const Metadata& lhs, const IDs& rhs) noexcept {
                        return lhs.id == rhs;
                    }

                    static bool operator()(const IDs& lhs, const Metadata& rhs) noexcept {
                        return lhs == rhs.id;
                    }
                };
            };

            using Data = std::unordered_set<
                Metadata,
                typename Metadata::Hash,
                typename Metadata::Equal
            >;
            using Cache = std::unordered_map<size_t, PyObject*>;

            struct Edge {
                std::string name;
                std::shared_ptr<Node> node;

                /* Identifies the subset of overloads that include this node at some point
                along their paths.  As the trie is traversed, the subset of candidate
                overloads is determined by a sequence of bitwise ANDs against this bitset,
                such that by the end, we are left with the space of overloads that
                reference each node that was traversed. */
                IDs matches;

                struct Hash {
                    using is_transparent = void;
                    static size_t operator()(const Edge& edge) noexcept {
                        return std::hash<std::string_view>{}(edge.name);
                    }
                    static size_t operator()(std::string_view name) noexcept {
                        return std::hash<std::string_view>{}(name);
                    }
                };

                struct Equal {
                    using is_transparent = void;
                    static bool operator()(const Edge& lhs, const Edge& rhs) noexcept {
                        return lhs.name == rhs.name;
                    }
                    static bool operator()(const Edge& lhs, std::string_view rhs) noexcept {
                        return std::string_view(lhs.name) == rhs;
                    }
                    static bool operator()(std::string_view lhs, const Edge& rhs) noexcept {
                        return lhs == std::string_view(rhs.name);
                    }
                };
            };

            struct Node {
            private:
                friend Overloads;

                struct TopoSort {
                    using is_transparent = void;
                    static bool operator()(const Object& lhs, const Object& rhs) {
                        return
                            subclass{}(ptr(lhs), ptr(rhs)) ||
                            ptr(lhs) < ptr(rhs);  // ties broken by address
                    }
                    static bool operator()(const Object& lhs, PyObject* rhs) {
                        return subclass{}(ptr(lhs), rhs) || ptr(lhs) < rhs;
                    }
                    static bool operator()(PyObject* lhs, const Object& rhs) {
                        return subclass{}(lhs, ptr(rhs)) || lhs < ptr(rhs);
                    }
                };

                /// NOTE: all positional and variadic edges generate nodes with empty names
                /// in order to force a single entry per type, which will be continually
                /// reused.  Keyword edges, on the other hand, are registered under their
                /// actual names, meaning they must match exactly in order to be reused,
                /// and there may be more than one edge per candidate type/kind.

                using Edges = std::unordered_set<
                    Edge,
                    typename Edge::Hash,
                    typename Edge::Equal
                >;
                using Kinds = std::map<impl::ArgKind, Edges>;
                using Types = std::map<Object, Kinds, TopoSort>;

                struct Visited {
                    PyObject* type;
                    impl::ArgKind kind;
                    std::string_view name;
                    std::shared_ptr<Node> node;
                };

                Edge& insert_edge(
                    const Object& type,
                    impl::ArgKind kind,
                    std::string_view name
                ) {
                    auto a = outgoing.try_emplace(type, Kinds{}).first;
                    auto b = a->second.try_emplace(kind, Edges{}).first;
                    auto c = b->emplace(
                        kind.kw() ? name : std::string_view{},
                        nullptr,
                        IDs{}
                    ).first;
                    return *c;
                }

                Edge& insert_edge(
                    const Object& type,
                    impl::ArgKind kind,
                    std::string_view name,
                    std::vector<Visited>& path
                ) {
                    auto a = outgoing.try_emplace(type, Kinds{}).first;
                    auto b = a->second.try_emplace(kind, Edges{}).first;
                    auto c = b->emplace(
                        kind.kw() ? name : std::string_view{},
                        nullptr,
                        IDs{}
                    ).first;
                    path.emplace_back(
                        ptr(a->first),
                        b->first,
                        c->name,
                        c->node
                    );
                    return *c;
                }

                static void insert_cyclic_edge(
                    const Metadata& overload,
                    const inspect::Param& param,
                    const std::vector<Visited>& path,
                    std::vector<Node*>& origins
                ) {
                    if (param.kw() || param.kwargs()) {
                        for (size_t i = 0, n = origins.size(); i < n; ++i) {
                            Edge& edge = origins[i]->insert_edge(
                                param.type,
                                param.kwargs() ? param.kind : impl::ArgKind::KW,
                                param.name
                            );
                            if (!edge.node) {
                                for (size_t j = path.size(); j-- > 0;) {
                                    const inspect::Param& curr = overload.signature[j];
                                    if (
                                        (param.kw() && param.name == curr.name) ||
                                        (param.kwargs() && curr.kwargs())
                                    ) {
                                        edge.matches |= overload.id;
                                        edge.node = path[j].node;  // circular reference
                                        break;  // guaranteed to find a match
                                    }
                                }
                            } else if (!(edge.matches & overload.id)) {
                                edge.matches |= overload.id;  // color a reused edge
                                origins.push_back(edge.node.get());  // DAG continues from reused node
                            }
                        }
                    }
                };

                void check_for_ambiguity(
                    const Data& data,
                    const Metadata& overload,
                    const IDs& candidates,
                    std::vector<Visited>& path,
                    bool allow_positional
                ) {
                    using Required = inspect::Required;
                    static constexpr bool keep_defaults = false;
                    static constexpr size_t max_width = 80;
                    static constexpr size_t indent = 4;
                    static constexpr std::string prefix(8, ' ');

                    // if the overload is the only candidate, there can be no ambiguity
                    if (candidates == overload.id) {
                        return;
                    }

                    // generate a bitmask encoding the required arguments that have been
                    // visited on this path
                    Required mask;
                    for (size_t i = 0; i < path.size(); ++i) {
                        const Visited& followed = path[i];
                        if (followed.kind.pos()) {
                            mask |= Required(1) << i;
                        } else if (followed.kind.kw()) {
                            mask |= Required(1) <<
                                overload.signature[followed.name].index;
                        }
                    }

                    // if there are no missing required arguments, decompose the candidates
                    // and assert that none are fully satisfied
                    mask &= overload.signature.required();
                    if (mask == overload.signature.required()) {
                        for (const IDs& id : candidates.components()) {
                            if (id == overload.id) {
                                continue;
                            }
                            const Metadata& existing = data.at(id);
                            mask = 0;
                            for (size_t i = 0; i < path.size(); ++i) {
                                const Visited& followed = path[i];
                                if (followed.kind.pos()) {
                                    mask |= Required(1) << i;
                                } else if (followed.kind.kw()) {
                                    mask |= Required (1) <<
                                        existing.signature[followed.name].index;
                                }
                            }
                            mask &= existing.signature.required();
                            if (mask == existing.signature.required()) {
                                throw ValueError(
                                    "overload conflicts with existing signature\n"
                                    "    existing:\n" +
                                    existing.signature.to_string(
                                        keep_defaults,
                                        prefix,
                                        max_width,
                                        indent
                                    ) + "\n    new:\n" +
                                    overload.signature.to_string(
                                        keep_defaults,
                                        prefix,
                                        max_width,
                                        indent
                                    )
                                );
                            }
                        }
                    }

                    // recur for all outgoing edges associated with this overload that do
                    // not form a cycle with respect to the current path
                    for (const auto& [type, kinds] : outgoing) {
                        for (const auto& [kind, edges] : kinds) {
                            if ((kind.pos() || kind.args()) && !allow_positional) {
                                continue;
                            }
                            for (const Edge& edge : edges) {
                                if (edge.matches & overload.id) {
                                    bool has_cycle = false;
                                    for (const Visited& followed : path) {
                                        if (followed.node == edge.node || (
                                            followed.kind.kw() &&
                                            followed.name == edge.name
                                        )) {
                                            has_cycle = true;
                                            break;
                                        }
                                    }
                                    if (!has_cycle) {
                                        path.emplace_back(
                                            ptr(type),
                                            kind,
                                            edge.name,
                                            edge.node
                                        );
                                        edge.node->check_for_ambiguity(
                                            data,
                                            overload,
                                            candidates & edge.matches,
                                            path,
                                            kind.pos() || kind.args()
                                        );
                                        path.pop_back();
                                    }
                                }
                            }
                        }
                    }
                }

            public:
                /* A sorted, multi-level map that sorts the outgoing edges first by type,
                then by kind, and then by name.  Node iterators greatly simplify searches
                over these maps. */
                Types outgoing;

                /* The total number of outgoing edges originating from this node. */
                [[nodiscard]] size_t size() const noexcept {
                    size_t total = 0;
                    for (const auto& [type, kinds] : outgoing) {
                        for (const auto& [kind, edges] : kinds) {
                            total += edges.size();
                        }
                    }
                    return total;
                }

                /* Indicates whether this node has any outgoing edges. */
                [[nodiscard]] bool empty() const noexcept {
                    return outgoing.empty();
                }

                /* Estimate the total amount of memory used by this node and its outgoing
                edges, in bytes.  Uses a visited node set to avoid cycles and records the
                total number of visited edges as an out parameter.  Does not count any
                memory held by Python, or the string buffer, which often falls into small
                string optimizations. */
                [[nodiscard]] size_t memory_usage(
                    std::unordered_set<const Node*>& visited
                ) const noexcept {
                    size_t total = sizeof(Node) + sizeof(Types::value_type) * outgoing.size();
                    for (const auto& [type, kinds] : outgoing) {
                        total += sizeof(Kinds::value_type) * kinds.size();
                        for (const auto& [kind, edges] : kinds) {
                            total += sizeof(Edges::value_type) * edges.size();
                            for (const Edge& edge : edges) {
                                if (visited.insert(edge.node.get()).second) {
                                    total += edge.node->memory_usage(visited);
                                }
                            }
                        }
                    }
                    return total;
                }

                /* Recursively insert outgoing edges along the given path.  Existing edges
                will be reused if possible, and cycles may be created to represent
                arbitrary keyword order. */
                void insert(
                    const Data& data,
                    const Metadata& overload,
                    const IDs& candidates
                ) {
                    // loop 1: insert edges along canonical path
                    Node* node = this;
                    std::vector<Visited> path;
                    path.reserve(overload.signature.size());
                    for (size_t i = 0; i < overload.signature.size(); ++i) {
                        const inspect::Param& param = &overload.signature[i];
                        Edge& edge = node->insert_edge(
                            param.type,
                            param.pos() ?
                                impl::ArgKind::POS :
                                param.kw() ? impl::ArgKind::KW : param.kind,
                            param.name,
                            path
                        );
                        if (!edge.node) {
                            edge.node = std::make_shared<Node>(Types{});
                        }
                        edge.matches |= overload.id;
                        node = edge.node.get();
                    }

                    // loop 2 (reverse): backfill cyclic keywords
                    std::vector<Node*> origins;
                    origins.reserve(overload.signature.size());
                    for (size_t i = overload.signature.size(); i-- > 0;) {
                        const inspect::Param& curr = overload.signature[i];
                        node = path[i].node.get();
                        origins.push_back(node);

                        // consider all rightward arguments out to the tail of the
                        // signature, inserting lookahead edges to each keyword
                        for (size_t j = i; ++j < overload.signature.size();) {
                            insert_cyclic_edge(
                                overload,
                                overload.signature[j],
                                path,
                                origins
                            );
                        }
                        origins.clear();
                        origins.push_back(node);

                        // consider all leftward arguments out to the first keyword,
                        // inserting lookbehind edges to each keyword
                        for (size_t j = i; j-- > overload.signature.first_keyword();) {
                            insert_cyclic_edge(
                                overload,
                                overload.signature[j],
                                path,
                                origins
                            );
                        }

                        // stop after the rightmost required positional-only arg
                        if (curr.posonly() && !curr.opt()) {
                            break;
                        }
                        origins.clear();
                    }

                    // loop 3: recursively search for ambiguities along any path
                    path.clear();
                    check_for_ambiguity(
                        data,
                        overload,
                        candidates,
                        path,
                        true
                    );
                }

                /* Recursively purge all outgoing edges that are associated with the
                identified overloads. */
                void remove(const IDs& id, std::unordered_set<const Node*>& visited) {
                    // type -> kinds
                    auto types = outgoing.begin();
                    auto types_end = outgoing.end();
                    while (types != types_end) {

                        // kind -> edges
                        auto kinds = types->second.begin();
                        auto kinds_end = types->second.end();
                        while (kinds != kinds_end) {

                            // edges -> node
                            auto edges = kinds->second.begin();
                            auto edges_end = kinds->second.end();
                            while (edges != edges_end) {
                                if (edges->matches & id) {
                                    if (visited.insert(edges->node.get()).second) {
                                        edges->node->remove(id);  // recur
                                    }
                                    edges->matches &= ~id;
                                    if (!edges->matches) {
                                        edges = kinds->second.erase(edges);
                                    } else {
                                        ++edges;
                                    }
                                } else {
                                    ++edges;
                                }
                            }

                            if (kinds->second.empty()) {
                                kinds = types->second.erase(kinds);
                            } else {
                                ++kinds;
                            }
                        }

                        if (types->second.empty()) {
                            types = outgoing.erase(types);
                        } else {
                            ++types;
                        }
                    }
                }

                /* A sorted iterator over the individual edges that match a particular
                value within an overload key.  The top-level trie iterator consists of a
                nested stack of these, which grows and shrinks as the trie is explored,
                mimicking the call frame in a recursive function. */
                template <typename check> requires (valid_check<check>)
                struct Iterator {
                private:
                    friend Overloads;
                    friend Node;

                    std::string_view arg_name;
                    PyObject* arg_value;
                    impl::ArgKind arg_kind;

                    struct EdgeView {
                        inline static const Edges empty;
                        Iterator* self;
                        std::ranges::iterator_t<const Edges> it = empty.begin();
                        std::ranges::sentinel_t<const Edges> end = empty.end();

                        EdgeView& operator++() {
                            if (it != end) {
                                if (self->arg_name.empty()) {
                                    ++it;
                                } else {
                                    it = end;
                                }
                            }
                        }
                    };

                    struct KindView {
                    private:
                        EdgeView advance() {
                            while (it != end) {
                                if ((
                                    (self->arg_kind & impl::ArgKind::POS) &&
                                    (it->first & impl::ArgKind::POS)
                                ) || (
                                    (self->arg_kind & impl::ArgKind::KW) &&
                                    (it->first & impl::ArgKind::KW)
                                )) {
                                    EdgeView result{
                                        self,
                                        self->arg_name.empty() || it->first.kwargs() ?
                                            it->second.begin() :
                                            it->second.find(self->arg_name),
                                        it->second.end()
                                    };
                                    if (result.it != result.end) {
                                        return result;
                                    }
                                }
                            }
                            return {self};
                        }

                    public:
                        Iterator* self;
                        std::ranges::iterator_t<const Kinds> it;
                        std::ranges::sentinel_t<const Kinds> end;
                        EdgeView edges = advance();

                        KindView& operator++() {
                            if ((++edges).it == edges.end && ++it != end) {
                                edges = advance();
                            }
                            return *this;
                        }
                    };

                    struct TypeView {
                    private:
                        KindView advance() {
                            while (it != end) {
                                if (!self->arg_value || check{}(
                                    self->arg_value,
                                    ptr(it->first)
                                )) {
                                    KindView result{
                                        self,
                                        it->second.begin(),
                                        it->second.end()
                                    };
                                    if (result.it != result.end) {
                                        return result;
                                    }
                                }
                            }
                            return {self};
                        }

                    public:
                        inline static const Types empty;
                        Iterator* self;
                        std::ranges::iterator_t<const Types> it = empty.begin();
                        std::ranges::sentinel_t<const Types> end = empty.end();
                        KindView kinds = advance();

                        TypeView& operator++() {
                            if ((++kinds).it == kinds.end && ++it != end) {
                                kinds = advance();
                            }
                            return *this;
                        }
                    } types;

                    Iterator(
                        const impl::OverloadKey::Argument& arg,
                        std::ranges::iterator_t<const Types>&& it,
                        std::ranges::sentinel_t<const Types>&& end
                    ) : arg_name(arg.name),
                        arg_value(ptr(arg.value)),
                        arg_kind(arg.kind),
                        types(this, std::move(it), std::move(end))
                    {}

                public:
                    using iterator_category = std::input_iterator_tag;
                    using difference_type = std::ptrdiff_t;
                    using value_type = Node;
                    using pointer = const Node*;
                    using reference = const Node&;

                    const Object& type() const noexcept { return types->first; }
                    impl::ArgKind kind() const noexcept { return types.kinds->first; }
                    const std::string& name() const noexcept { return types.kinds.edges->name; }

                    const Edge& operator*() const { return *types.kinds.edges; }
                    const Edge* operator->() const { return &*types.kinds.edges; }

                    Iterator& operator++() {
                        ++types;
                        return *this;
                    }

                    friend bool operator==(const Iterator& self, sentinel) {
                        return self.types.it == self.types.end;
                    }

                    friend bool operator==(sentinel, const Iterator& self) {
                        return self.types.it == self.types.end;
                    }

                    friend bool operator!=(const Iterator& self, sentinel) {
                        return self.types.it != self.types.end;
                    }

                    friend bool operator!=(sentinel, const Iterator& self) {
                        return self.types.it != self.types.end;
                    }
                };

                /* Return a shallow iterator over the subset of outgoing edges that match a
                given parameter.  An empty name will match all outgoing edges, regardless
                of name.  A null value will do the same for types, yielding all matching
                candidates in topological order and ignoring the templated type check.
                Finally, a positional-or-keyword kind will match all edges, regardless of
                kind.

                Note that in the interest of performance, the iterator will only own a view
                into the underlying name buffer and a borrowed reference to the value,
                both of which must remain valid for the lifetime of the iterator.  This
                avoids any unnecessary allocations or reference counting overhead during
                overload resolution.  Both of these conditions can be guaranteed by using
                the outer `Overloads::begin()` method to iterate over the whole trie. */
                template <typename check = instance> requires (valid_check<check>)
                [[nodiscard]] Iterator<check> begin(
                    const impl::OverloadKey::Argument& arg = {
                        {},
                        reinterpret_steal<Object>(nullptr),
                        impl::ArgKind::POS | impl::ArgKind::KW
                    }
                ) const {
                    return {arg, outgoing.begin(), outgoing.end()};
                }

                [[nodiscard]] static sentinel end() noexcept {
                    return {};
                }
            };

            size_t m_depth = 0;
            Node::Types m_leading_edge;  // holds the root node if overloads are present
            Data m_data = {};  // always holds fallback function at ID 1
            mutable Cache m_cache = {};

            [[nodiscard]] Edge* root() noexcept {
                if (m_leading_edge.empty()) {
                    return nullptr;
                }
                auto types = m_leading_edge.begin();
                auto kinds = types->second.begin();
                auto edges = kinds->second.begin();
                return &*edges;
            }

            [[nodiscard]] const Edge* root() const noexcept {
                if (m_leading_edge.empty()) {
                    return nullptr;
                }
                auto types = m_leading_edge.begin();
                auto kinds = types->second.begin();
                auto edges = kinds->second.begin();
                return &*edges;
            }

        public:
            template <std::convertible_to<std::string_view> T>
                requires (args_are_python && return_is_python)
            Overloads(const T& name, const Object& fallback) {
                std::string_view str = name;
                inspect sig(fallback, str);

                /// TODO: if I receive an instance of `bertrand.Function`, then the
                /// signature check can be done just through some type checks rather than
                /// building a full signature object.  That would greatly increase the
                /// performance of conversions from Python -> C++ in the case where you're
                /// using bertrand types from the beginning.

                using Expected = py::signature<Return(Args...)>;
                if (sig != Expected{}) {
                    constexpr size_t max_width = 80;
                    constexpr size_t indent = 4;
                    constexpr std::string prefix(8, ' ');
                    throw TypeError(
                        "signature mismatch\n    expected:\n" + Expected{}.to_string(
                            str,
                            prefix,
                            max_width,
                            indent
                        ) + "\n    received:\n" + sig.to_string(
                            false,
                            prefix,
                            max_width,
                            indent
                        )
                    );
                };

                m_depth = sig.size();
                m_data.emplace(1, std::move(sig));
            }

            /* Return a reference to the fallback function that will be chosen if no other
            overloads match a valid argument list. */
            [[nodiscard]] const Object& function() const noexcept {
                return m_data.at(1).signature.function();
            }

            /* Get the stored signature of the fallback function. */
            [[nodiscard]] const inspect& signature() const noexcept {
                return m_data.at(1).signature;
            }

            /* Indicates whether the trie contains any overloads. */
            [[nodiscard]] auto empty() const noexcept {
                return m_data.size() == 1;
            }

            /* The total number of overloads stored within the trie, excluding the fallback
            function. */
            [[nodiscard]] auto size() const noexcept {
                return m_data.size() - 1;
            }

            /* The maximum depth of the trie, which is equivalent to the total number of
            arguments of its longest overload.  Variadic arguments take up a single index
            for the purposes of this calculation. */
            [[nodiscard]] size_t max_depth() const noexcept {
                return m_depth;
            }

            /* Estimate the total amount of memory consumed by the trie in bytes.  Note
            that this does not count any additional memory being managed by Python (i.e.
            the function objects themselves or the `inspect.Signature` instances used to
            back the trie's metadata). */
            [[nodiscard]] size_t memory_usage() const noexcept {
                return std::get<0>(stats());
            }

            /* The total number of nodes that have been allocated within the trie,
            including the root node. */
            [[nodiscard]] size_t total_nodes() const noexcept {
                return std::get<1>(stats());
            }

            /* Estimate the current memory usage and total number of nodes at the same
            time.  This is more efficient than using separate calls if both are needed. */
            [[nodiscard]] std::pair<size_t, size_t> stats() const noexcept {
                size_t total = sizeof(Overloads);
                total += sizeof(typename Cache::value_type) * m_cache.size();
                for (const Metadata& overload : m_data) {
                    total += overload.memory_usage();
                }
                const Node* root = nullptr;
                total += sizeof(typename Node::Types::value_type) * m_leading_edge.size();
                for (const auto& [type, kinds] : m_leading_edge) {
                    total += sizeof(typename Node::Kinds::value_type) * kinds.size();
                    for (const auto& [kind, edges] : kinds) {
                        total += sizeof(typename Node::Edges::value_type) * edges.size();
                        for (const Edge& edge : edges) {
                            root = edge.node.get();
                        }
                    }
                }
                if (root) {
                    std::unordered_set<const Node*> visited = {root};
                    total += root->memory_usage(visited);
                    return {total, visited.size()};
                }
                return {total, 0};
            }

            /* Search against the function's overload cache to find a precomputed path
            through the trie.  Whenever `begin()` is called with a vectorcall array, the
            first result is always inserted here to optimize repeated calls with the same
            signature.  If no cached function is found, this method returns null, forcing a
            full search of the trie during overload resolution.  Returns a borrowed
            reference otherwise.

            Note that all arguments must be properly normalized in order for cache searches
            to remain stable.  This means inserting any partial arguments and converting to
            proper Bertrand types before initiating a search.  If this is not done, then it
            is possible for several distinct signatures to resolve to a single overload,
            since some Python types (e.g. generics) are opaque to Python's type system, and
            will thus produce an ambiguous hash.  Normalizing to Bertrand types avoids this
            by narrowing such arguments sufficiently for cache searches to be effective. */
            [[nodiscard]] PyObject* cache_lookup(size_t hash) const noexcept {
                auto it = m_cache.find(hash);
                return it == m_cache.end() ? nullptr : it->second;
            }

            /* Manually clear the overload cache, forcing paths to be recomputed on
            subsequent searches. */
            void flush() noexcept {
                m_cache.clear();
            }

            /* Returns true if a function is present in the trie, as indicated by a
            linear search of `==` checks against each encoded function object. */
            [[nodiscard]] bool contains(const Object& func) const {
                for (const Metadata& metadata : m_data) {
                    if (metadata.signature.function() == func) {
                        return true;
                    }
                }
                return false;
            }

            /* Insert a function into the trie.  Throws a TypeError if the function is not
            a viable overload of the enclosing signature (as determined by an
            `inspect.signature()` call), or a ValueError if it conflicts with an existing
            overload. */
            void insert(const Object& func) {
                constexpr bool keep_defaults = false;
                constexpr size_t max_width = 80;
                constexpr size_t indent = 4;
                constexpr std::string prefix(8, ' ');

                // construct root node if it doesn't already exist
                Metadata& fallback = m_data.at(1);
                Edge* root = this->root();
                if (!root) {
                    auto [types, inserted] = m_leading_edge.emplace(
                        reinterpret_steal<Object>(nullptr),
                        typename Node::Kinds{{
                            impl::ArgKind::POS,
                            {Edge{   
                                .name = "",
                                .matches = 1,  // identifies all overloads, 1 = fallback
                                .node = std::make_shared<Node>()  // root node
                            }}
                        }}
                    );
                    if (!inserted) {
                        throw ValueError("root node already exists");
                    }
                    try {
                        auto kinds = types->second.begin();
                        auto edges = kinds->second.begin();
                        root = &*edges;
                        root->node->insert(m_data, fallback, root->matches);
                    } catch (...) {
                        m_leading_edge.clear();
                        throw;
                    }
                }

                IDs id = 1;
                id <<= root->matches.first_zero();

                try {
                    auto [overload, inserted] = m_data.emplace(id, inspect(func));
                    if (!inserted) {
                        throw ValueError("overload already exists");
                    }
                    root->matches |= id;

                    // ensure the function is a viable overload of the enclosing signature
                    if (overload->signature >= py::signature<Return(Args...)>{}) {
                        throw TypeError(
                            "overload must be more specific than the fallback signature\n"
                            "    fallback:\n" + fallback.signature.to_string(
                                keep_defaults,
                                prefix,
                                max_width,
                                indent
                            ) + "\n    new:\n" + overload->signature.to_string(
                                keep_defaults,
                                prefix,
                                max_width,
                                indent
                            )
                        );
                    }

                    root->node->insert(m_data, *overload, root->matches);
                    m_cache.clear();
                    if (overload->signature.size() > m_depth) {
                        m_depth = overload->signature.size();
                    }

                } catch (...) {
                    if (m_data.size() <= 2) {
                        clear();
                    } else {
                        root->node->remove(id);
                        m_data.erase(id);
                        root->matches &= ~id;
                    }
                    throw;
                }
            }

            /* Remove a function from the overload trie.  Returns the function that was
            removed (which may not be exactly identical to the input) or None if no
            matching function was found.  Raises a KeyError if an attempt is made to delete
            the fallback function.

            This works by iterating over the trie's contents in topological order and
            performing equality checks against the input key, allowing for transparent
            comparisons if the function and/or key overrides the `__eq__` method.  If
            multiple functions evaluate equal to the key, only the first (most specific)
            match will be removed. */
            [[maybe_unused]] Object remove(const Object& key) {
                Edge* root = this->root();
                if (!root) {
                    if (key == this->function()) {
                        throw KeyError("cannot remove the fallback implementation");
                    }
                    return reinterpret_borrow<Object>(Py_None);
                }

                auto it = this->begin();
                auto end = this->end();
                while (it != end) {
                    const Metadata& data = *it.curr;
                    if (data.signature.function() == key) {
                        if (data.id == 1) {
                            throw KeyError("cannot remove the fallback implementation");
                        }
                        Object result = data.signature.function();
                        if (m_data.size() <= 2) {
                            clear();
                        } else {
                            std::unordered_set<const Node*> visited;
                            visited.reserve(data.signature.size());
                            visited.emplace(root->node.get());
                            root->node->remove(data.id, visited);
                            root->matches &= ~data.id;
                            m_cache.clear();
                            m_data.erase(data);
                            m_depth = 0;
                            for (const Metadata& data : m_data) {
                                if (data.signature.size() > m_depth) {
                                    m_depth = data.signature.size();
                                }
                            }
                            return result;
                        }
                    }
                    ++it;
                }
                return reinterpret_borrow<Object>(Py_None);
            }

            /* Remove all overloads from the trie, resetting it to its default state. */
            void clear() noexcept {
                m_leading_edge.clear();  // drop root node
                m_cache.clear();
                auto it = m_data.begin();
                while (it != m_data.end()) {
                    if (it->id == 1) {  // skip removing fallback
                        m_depth = it->signature.size();
                        it->required.clear();
                        ++it;
                    } else {
                        it = m_data.erase(it);
                    }
                }
            }

            /* An iterator that traverses the trie in topological order, extracting the
            subset of overloads that match a given key.  As long as the key is valid, the
            final overload will always be the fallback implementation.  Otherwise, the
            iterator may be empty, indicating that the key is malformed in some way. */
            template <typename check> requires (valid_check<check>)
            struct Iterator {
            private:
                friend Overloads;
                using Argument = impl::OverloadKey::Argument;

                struct Frame {
                    /* An iterator over the outgoing edges of the current node, which will
                    be filtered by the type check, mask interesections, and edge
                    names/kinds. */
                    Node::template Iterator<check> outgoing;

                    /* The edge that was followed to get to this frame.  This is always
                    equal to the outgoing edge of the previous frame, except for the
                    leading frame, which sets this to null. */
                    const Node::template Iterator<check>* incoming = nullptr;

                    /* The index of the next argument to be matched from the key. */
                    size_t index = 0;

                    /* Rolling intersection of possible overloads along this path.  Set to
                    all overloads for the leading frame, and then intersected with the
                    matches of the incoming edge for each subsequent frame.  Outgoing edges
                    will only be considered if at least one of their IDs are contained
                    within this set.  The final space of candidate overloads can be found
                    by intersecting with the IDs of the last outgoing edge and then
                    decomposing into one-hot masks. */
                    IDs matches = outgoing->matches;
                };

                impl::OverloadKey key;
                std::vector<Frame> stack;
                const Data* data;
                IDs visited;
                const Metadata* curr = nullptr;

                /* Advance the last iterator in the stack.  If it reaches the end, pop it
                from the stack and advance the previous iterator, recurring until either
                the stack is exhausted or a new value is found. */
                void advance() {
                    while (++stack.back().outgoing == sentinel{}) {
                        stack.pop_back();
                        if (stack.empty()) {
                            return;
                        }
                    }
                    explore();
                }

                /* Recursively grow the stack until a valid overload has been found.  The
                stack must not be empty when this function is called.

                The search algorithm works by growing the stack until all key parameters
                have been exhausted.  If no matching edges are found for a given parameter,
                then we advance the stack until a match is found or the stack is empty,
                whichever comes first.  Wildcards (indicated by a null value) will traverse
                all outgoing edges of the corresponding kind, regardless of type.
                Otherwise, only the outgoing edges that pass the type check will be
                considered.  Variadic wildcards are handled by recursively consuming all
                outgoing, non-cyclic edges of that kind, stopping at the first leaf.

                Once all parameters have been exhausted and a candidate node has been
                reached, the algorithm will check to see if that node has a corresponding
                function whose required arguments are fully satisfied by the given
                arguments.  If not, we advance the stack and recur to identify the next
                candidate node, stopping when the stack is empty. */
                void explore() {
                    size_t index = stack.back().index;
                    while (index < key->size()) {
                        const Argument& arg = (*key)[index];
                        if (arg.kind.variadic() && arg.value.is(nullptr)) {
                            while (grow(arg, index));
                        } else if (!grow(arg, index)) {
                            advance();
                            return;
                        }
                        ++index;
                    }
                    IDs candidates = stack.back().matches & stack.back().outgoing->matches;
                    for (const IDs& id : candidates.components()) {
                        if (!(visited & id)) {
                            const Metadata& overload = data->at(id);
                            visited |= id;
                            if (validate(overload)) {
                                curr = &overload;
                                return;
                            }
                        }
                    }
                    advance();
                }

                /* Grow the stack one level by retrieving the last node and topologically
                searching for an outgoing edge that has not yet been traversed and matches
                the corresponding key parameter.  Returns the next outgoing edge or null if
                no candidates are found. */
                const Edge* grow(const Argument& arg, size_t index) {
                    IDs matches = stack.back().matches & stack.back().outgoing->matches;
                    if ((visited & matches) == matches) {
                        return nullptr;  // all candidates have been explored
                    }
                    stack.emplace_back(
                        stack.back().outgoing->node->template begin<check>(arg),
                        &stack.back(),
                        index,
                        matches
                    );
                    while (stack.back().outgoing != sentinel{}) {
                        if (!has_cycle(stack.back().outgoing)) {
                            return &*stack.back().outgoing;
                        }
                        ++stack.back().outgoing;
                    }
                    stack.pop_back();
                    return nullptr;
                }

                /* Check whether an outgoing edge is already contained within the stack,
                indicating a cycle or a duplicate keyword from an overlapping key. */
                bool has_cycle(const Node::template Iterator<check>& it) const {
                    if (it.kind().kw()) {
                        for (size_t i = stack.size(); i-- > 1;) {
                            const Frame& frame = stack[i];
                            if ((*frame.incoming)->node == it->node || (
                                frame.incoming->kind().kw() &&
                                frame.incoming->name() == it->name
                            )) {
                                return true;
                            }
                        }
                    } else {
                        for (size_t i = stack.size(); i-- > 1;) {
                            if ((*stack[i].incoming)->node == it->node) {
                                return true;
                            }
                        }
                    }
                    return false;
                }

                /* Reconstruct a bitmask representing the observed arguments and compare it
                against the overload's `required` bitmask to check whether all necessary
                arguments have been given for this path. */
                bool validate(const Metadata& overload) const {
                    using Required = inspect::Required;
                    Required mask;
                    for (size_t i = 1; i < stack.size(); ++i) {
                        const auto& it = stack[i].outgoing;
                        if (it.kind().pos()) {
                            mask |= Required(1) << (i - 1);
                        } else if (it.kind().kw()) {
                            const inspect::Param* lookup = overload.signature[it.name()];
                            if (!lookup) {
                                return false;
                            }
                            mask |= Required(1) << lookup->index;
                        }
                    }
                    mask &= overload.signature.required();
                    return mask == overload.signature.required();
                }

                Iterator(
                    impl::OverloadKey&& key,
                    size_t max_depth,
                    const Data& data,
                    const Node::Types& leading_edge,
                    Cache& cache
                ) : key(std::move(key)),
                    data(&data)
                {
                    // if there is no root node, then we return an empty iterator
                    if (leading_edge.empty()) {
                        return;
                    }

                    // the first result for vectorcall iterators is cached
                    if (key.has_hash) {
                        stack.reserve(key->size() + 2);  // +1 for leading edge, +1 for overflow
                        stack.emplace_back(typename Node::template Iterator<check>{
                            Argument{
                                .name = "",
                                .value = reinterpret_steal<Object>(nullptr),
                                .kind = impl::ArgKind::POS
                            },
                            leading_edge.begin(),
                            leading_edge.end()
                        });
                        explore();
                        if (!stack.empty()) {
                            cache[key.hash] = ptr(**this);
                        }

                    // partial iterators are not cached and have no fixed length
                    } else {
                        stack.reserve(max_depth + 2);
                        stack.emplace_back(typename Node::template Iterator<check>{
                            Argument{
                                .name = "",
                                .value = reinterpret_steal<Object>(nullptr),
                                .kind = impl::ArgKind::POS
                            },
                            leading_edge.begin(),
                            leading_edge.end()
                        });
                        explore();
                    }
                }

            public:
                using iterator_category = std::input_iterator_tag;
                using difference_type = std::ptrdiff_t;
                using value_type = Object;
                using pointer = const value_type*;
                using reference = const value_type&;

                /* Contextually convert the iterator to a bool in order to replicate Python
                `if func[...]:` syntax. */
                explicit operator bool() const { return stack.empty(); }

                /* The overall trie iterator dereferences only to those functions that are
                callable with the given key. */
                const Object& operator*() const { return curr->signature.function(); }
                const Object* operator->() const { return &curr->signature.function(); }

                /* Incrementing the iterator traverses the trie until the next full match
                is found.  As long as the args are valid, the iterator will always contain
                the function's base implementation as the final overload. */
                Iterator& operator++() {
                    advance();
                    return *this;
                }

                friend bool operator==(const Iterator& self, sentinel) {
                    return self.stack.empty();
                }

                friend bool operator==(sentinel, const Iterator& self) {
                    return self.stack.empty();
                }

                friend bool operator!=(const Iterator& self, sentinel) {
                    return !self.stack.empty();
                }

                friend bool operator!=(sentinel, const Iterator& self) {
                    return !self.stack.empty();
                }
            };

            /* Yield all overloads within the trie in topological order, without any
            filtering. */
            [[nodiscard]] Iterator<instance> begin() const {
                return {
                    impl::OverloadKey{
                        .vec = {
                            {
                                .name = "",
                                .value = reinterpret_steal<Object>(nullptr),
                                .kind = impl::ArgKind::VAR | impl::ArgKind::POS
                            },
                            {
                                .name = "",
                                .value = reinterpret_steal<Object>(nullptr),
                                .kind = impl::ArgKind::VAR | impl::ArgKind::KW
                            }
                        }
                    },
                    m_depth,
                    m_data,
                    m_leading_edge,
                    m_cache
                };
            }

            /* Topologically search the trie with a given argument list, returning a sorted
            iterator over the matching overloads.  The first item is always the most
            specific matching overload, and the last item is always the function's base
            implementation.  An empty iterator can be returned if the arguments do not
            conform to the enclosing signature.  Keys are typically constructed through
            either the `Vectorcall{}.key()` or `Partial{}.key()` methods, which may
            originate from other specializations of `signature<>`. */
            template <typename check = instance> requires (valid_check<check>)
            [[nodiscard]] Iterator<check> begin(impl::OverloadKey&& key) const {
                return {std::move(key), m_depth, m_data, m_leading_edge, m_cache};
            }

            [[nodiscard]] sentinel end() const {
                return {};
            }
        };

        /* Construct an overload trie with the given fallback function, which must be a
        Python function that is callable with the enclosing signature. */
        template <std::convertible_to<std::string_view> T>
            requires (args_are_python && return_is_python)
        [[nodiscard]] static Overloads overloads(const T& name, const Object& func) {
            return {name, func};
        }



    };




}


template <py::impl::has_python Return, py::impl::has_python... Args>
struct signature<Return(Args...)> :
    impl::CppToPySignature<impl::PyParam, Return, Args...>
{};


template <py::impl::python Return, py::impl::python... Args>
    requires (py::impl::has_python<Return> && (py::impl::has_python<Args> && ...))
struct signature<Return(Args...)> :
    impl::PySignature<impl::PyParam, Return, Args...>
{};


/* The canonical form of `py::signature`, which encapsulates all of the internal call
machinery, as much as possible of which is evaluated at compile time.  All other
specializations should redirect to this form in order to avoid reimplementing the nuts
and bolts of the function ecosystem. */
template <typename Return, typename... Args>
struct signature<Return(Args...)> : bertrand::signature<Return(Args...)> {
private:
    using base = bertrand::signature<Return(Args...)>;

public:


    /// TODO: py::Function determines which overload to call by checking whether
    /// overloads.function().is(self).  If true, then the function is implemented in
    /// C++, and we call the C++ overload with the internal std::function.  Otherwise,
    /// the function is implemented in Python, and we call the Python overload instead.

    /* Call a C++ function from C++ using Python-style arguments. */
    template <
        impl::inherits<Partial> P,
        impl::inherits<Defaults> D,
        typename F,
        typename... A
    >
        requires (
            invocable<F> &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::can_convert &&
            Bind<A...>::satisfies_required_args
        )
    static constexpr Return operator()(
        P&& partial,
        D&& defaults,
        F&& func,
        A&&... args
    ) {
        return Bind<A...>{}(
            std::forward<P>(partial),
            std::forward<D>(defaults),
            std::forward<F>(func),
            std::forward<A>(args)...
        );
    }

    /* Call a C++ function from C++ using Python-style arguments with possible
    overloads. */
    template <
        impl::inherits<Partial> P,
        impl::inherits<Defaults> D,
        impl::inherits<Overloads> O,
        typename F,
        typename... A
    >
        requires (
            invocable<F> &&
            args_are_convertible_to_python &&
            return_is_convertible_to_python &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::satisfies_required_args &&
            Bind<A...>::can_convert
        )
    static Return operator()(
        P&& partial,
        D&& defaults,
        O&& overloads,
        F&& func,
        A&&... args
    ) {
        if (overloads.empty()) {
            return Bind<A...>{}(
                std::forward<P>(partial),
                std::forward<D>(defaults),
                std::forward<F>(func),
                std::forward<A>(args)...
            );
        }

        using source = signature<Return(A...)>;
        if constexpr (!source::has_args && !source::has_kwargs) {
            /// NOTE: value array can be stack allocated in this case
            std::array<PyObject*, Partial::n + sizeof...(A) + 1> array;
            Vectorcall vectorcall{
                array,
                std::forward<P>(partial),
                std::forward<A>(args)...
            };
            if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
                return vectorcall(cached);
            }
            /// NOTE: it is impossible for the search to fail, since the arguments are
            /// validated at compile time and the overload trie is known not to be empty
            const Object& overload = *overloads.begin(vectorcall.overload_key());
            if (overload.is(overloads.function())) {
                return Bind<A...>{}(
                    std::forward<P>(partial),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );
            }
            return vectorcall(ptr(overload));
        } else {
            Vectorcall vectorcall{std::forward<P>(partial), std::forward<A>(args)...};
            if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
                return vectorcall(cached);
            }
            /// NOTE: it is impossible for the search to fail, since the arguments are
            /// validated at compile time and the overload trie is known not to be empty
            const Object& overload = *overloads.begin(vectorcall.overload_key());
            if (overload.is(overloads.function())) {
                return Bind<A...>{}(
                    std::forward<P>(partial),
                    std::forward<D>(defaults),
                    std::forward<F>(func),
                    std::forward<A>(args)...
                );
            }
            return vectorcall(ptr(overload));
        }
    }

    /* Call a Python function from C++ using Python-style arguments. */
    template <impl::inherits<Partial> P, typename... A>
        requires (
            args_are_convertible_to_python &&
            return_is_convertible_to_python &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::satisfies_required_args &&
            Bind<A...>::can_convert
        )
    static Return operator()(
        P&& partial,
        PyObject* func,
        A&&... args
    ) { 
        using source = signature<Return(A...)>;
        if constexpr (!source::has_args && !source::has_kwargs) {
            /// NOTE: value array can be stack allocated in this case
            std::array<PyObject*, Partial::n + sizeof...(A) + 1> array;
            return Vectorcall{
                array,
                std::forward<P>(partial),
                std::forward<A>(args)...
            }(func);
        } else {
            return Vectorcall{
                std::forward<P>(partial),
                std::forward<A>(args)...
            }(func);
        }
    }

    /* Call a Python function from C++ using Python-style arguments with possible
    overloads. */
    template <impl::inherits<Partial> P, impl::inherits<Overloads> O, typename... A>
        requires (
            args_are_convertible_to_python &&
            return_is_convertible_to_python &&
            Bind<A...>::proper_argument_order &&
            Bind<A...>::no_qualified_arg_annotations &&
            Bind<A...>::no_duplicate_args &&
            Bind<A...>::no_extra_positional_args &&
            Bind<A...>::no_extra_keyword_args &&
            Bind<A...>::no_conflicting_values &&
            Bind<A...>::satisfies_required_args &&
            Bind<A...>::can_convert
        )
    static Return operator()(
        P&& partial,
        O&& overloads,
        A&&... args
    ) {
        using source = signature<Return(A...)>;
        if constexpr (!source::has_args && !source::has_kwargs) {
            /// NOTE: value array can be stack allocated in this case
            std::array<PyObject*, Partial::n + sizeof...(A) + 1> array;
            Vectorcall vectorcall{
                array,
                std::forward<P>(partial),
                std::forward<A>(args)...
            };
            if (overloads.empty()) {
                return vectorcall(ptr(overloads.function()));
            }
            if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
                return vectorcall(cached);
            }
            /// NOTE: it is impossible for the search to fail, since the arguments are
            /// validated at compile time and the overload trie is known not to be empty
            return vectorcall(ptr(*overloads.begin(vectorcall.overload_key())));
        } else {
            Vectorcall vectorcall{
                std::forward<P>(partial),
                std::forward<A>(args)...
            };
            if (overloads.empty()) {
                return vectorcall(ptr(overloads.function()));
            }
            if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
                return vectorcall(cached);
            }
            /// NOTE: it is impossible for the search to fail, since the arguments are
            /// validated at compile time and the overload trie is known not to be empty
            return vectorcall(ptr(*overloads.begin(vectorcall.overload_key())));
        }
    }

    /* Call a C++ function from Python. */
    template <impl::inherits<Partial> P, impl::inherits<Defaults> D, typename F>
        requires (
            invocable<F> &&
            args_are_convertible_to_python &&
            return_is_convertible_to_python
        )
    static Return operator()(
        P&& partial,
        D&& defaults,
        PyObject* const* args,
        size_t nargsf,
        PyObject* kwnames,
        F&& func
    ) {
        return Vectorcall{args, nargsf, kwnames}(
            std::forward<P>(partial),
            std::forward<D>(defaults),
            std::forward<F>(func)
        );
    }

    /* Call a C++ function from Python with possible overloads. */
    template <
        impl::inherits<Partial> P,
        impl::inherits<Defaults> D,
        impl::inherits<Overloads> O,
        typename F
    >
        requires (
            invocable<F> &&
            args_are_convertible_to_python &&
            return_is_convertible_to_python
        )
    static Return operator()(
        P&& partial,
        D&& defaults,
        PyObject* const* args,
        size_t nargsf,
        PyObject* kwnames,
        F&& func,
        O&& overloads
    ) {
        Vectorcall vectorcall{args, nargsf, kwnames};
        if (overloads.empty()) {
            return vectorcall(
                std::forward<P>(partial),
                std::forward<D>(defaults),
                std::forward<F>(func)
            );
        }
        vectorcall.normalize(std::forward<P>(partial));
        if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
            if (overloads.function().is(cached)) {
                return vectorcall(
                    std::forward<D>(defaults),
                    std::forward<F>(func)
                );
            }
            return vectorcall(cached);
        }
        auto it = overloads.begin(vectorcall.overload_key());
        if (it == overloads.end()) {
            vectorcall.validate();  // noreturn in this case
            std::unreachable();
        }
        const Object& overload = *it;
        if (overload.is(overloads.function())) {
            return vectorcall(
                std::forward<D>(defaults),
                std::forward<F>(func)
            );
        }
        return vectorcall(ptr(overload));
    }

    /* Call a Python function from Python. */
    template <impl::inherits<Partial> P, typename F>
        requires (
            invocable<F> &&
            args_are_python &&
            return_is_python
        )
    static Return operator()(
        P&& partial,
        PyObject* const* args,
        size_t nargsf,
        PyObject* kwnames,
        PyObject* func
    ) {
        Vectorcall vectorcall{args, nargsf, kwnames};
        vectorcall.normalize(std::forward<P>(partial));
        return vectorcall(func);
    }

    /* Call a Python function from Python with possible overloads. */
    template <impl::inherits<Partial> P, impl::inherits<Overloads> O, typename F>
        requires (
            invocable<F> &&
            args_are_python &&
            return_is_python
        )
    static Return operator()(
        P&& partial,
        PyObject* const* args,
        size_t nargsf,
        PyObject* kwnames,
        O&& overloads
    ) {
        Vectorcall vectorcall{args, nargsf, kwnames};
        vectorcall.normalize(std::forward<P>(partial));
        if (overloads.empty()) {
            return vectorcall(ptr(overloads.function()));
        }
        if (PyObject* cached = overloads.cache_lookup(vectorcall.hash)) {
            return vectorcall(cached);
        }
        auto it = overloads.begin(vectorcall.overload_key());
        if (it == overloads.end()) {
            vectorcall.validate();  // noreturn in this case
            std::unreachable();
        }
        return vectorcall(ptr(*it));
    }

};


template <typename F>
[[nodiscard]] bool operator<(const inspect& lhs, const signature<F>& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };
    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    Object type = rhs.returns().type();
    if (!issubclass(ptr(lhs.return_annotation()), ptr(type))) {
        return false;
    }
    bool equal = isequal(ptr(lhs.return_annotation()), ptr(type));

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        type = r->type();
        if (r->args()) {
            while (l != l_end && l->pos()) {
                equal = false;
                if (!issubclass(ptr(l->type), ptr(type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(l->type), ptr(type));
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                equal = false;
                if (!issubclass(ptr(l->type), ptr(type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(l->type), ptr(type));
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type),
                ptr(type)
            )) {
                return false;
            }
            if (equal) {
                equal = isequal(ptr(l->type), ptr(type));
            }
            ++l;
        }
        ++r;
    }
    return !equal;
}
template <typename F>
[[nodiscard]] bool operator<(const signature<F>& lhs, const inspect& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };
    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    Object type = lhs.returns().type();
    if (!issubclass(ptr(type), ptr(rhs.return_annotation()))) {
        return false;
    }
    bool equal = isequal(ptr(type), ptr(rhs.return_annotation()));

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        if (r->args()) {
            while (l != l_end && l->pos()) {
                equal = false;
                if (!issubclass(ptr(l->type()), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                type = l->type();
                if (l->name != r->name || !issubclass(
                    ptr(type),
                    ptr(r->type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(type), ptr(r->type));
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                equal = false;
                if (!issubclass(ptr(l->type()), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                type = l->type();
                if (l->name != r->name || !issubclass(
                    ptr(type),
                    ptr(r->type)
                )) {
                    return false;
                }
                if (equal) {
                    equal = isequal(ptr(type), ptr(r->type));
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type()),
                ptr(r->type)
            )) {
                return false;
            }
            if (equal) {
                equal = isequal(ptr(type), ptr(r->type));
            }
            ++l;
        }
        ++r;
    }
    return !equal;
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator<(const signature<L>& lhs, const signature<R>& rhs) {
    using LHS = signature<L>;
    using RHS = signature<R>;

    if constexpr (
        LHS::size() < RHS::size() ||
        !issubclass<typename LHS::Return, typename RHS::Return>()
    ) {
        return false;
    }

    return []<size_t I = 0, size_t J = 0>(this auto&& self, bool equal) {
        // recursive case
        if constexpr (J < RHS::size()) {
            using right = RHS::template at<J>;

            // variadic positional
            if constexpr (ArgTraits<right>::args()) {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (ArgTraits<left>::pos()) {
                        if constexpr (!issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >(false);
                    } else if constexpr (ArgTraits<left>::args()) {
                        if constexpr (
                            ArgTraits<left>::name != ArgTraits<right>::name ||
                            !issubclass<
                                typename ArgTraits<left>::type,
                                typename ArgTraits<right>::type
                            >()
                        ) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >(equal && std::same_as<left, right>);
                    } else {
                        return std::forward<decltype(self)>(self).template operator()<
                            I,
                            J + 1
                        >(equal);
                    }
                } else {
                    return false;
                }

            // variadic keywords
            } else if constexpr (ArgTraits<right>::kwargs()) {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (ArgTraits<left>::kwonly()) {
                        if constexpr (!issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >(false);
                    } else if constexpr (ArgTraits<left>::kwargs()) {
                        if constexpr (
                            ArgTraits<left>::name != ArgTraits<right>::name ||
                            !issubclass<
                                typename ArgTraits<left>::type,
                                typename ArgTraits<right>::type
                            >()
                        ) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >(equal && std::same_as<left, right>);
                    } else {
                        return std::forward<decltype(self)>(self).template operator()<
                            I,
                            J + 1
                        >(equal);
                    }
                } else {
                    return false;
                }

            // single arg
            } else {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (
                        ArgTraits<left>::name != ArgTraits<right>::name ||
                        ArgTraits<left>::kind != ArgTraits<right>::kind ||
                        !issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()
                    ) {
                        return false;
                    }
                    return std::forward<decltype(self)>(self).template operator()<
                        I + 1,
                        J + 1
                    >(equal && std::same_as<left, right>);
                } else {
                    return false;
                }
            }

        // base case
        } else {
            return !equal;
        }
    }(std::same_as<typename LHS::Return, typename RHS::Return>);
}


template <typename F>
[[nodiscard]] bool operator<=(const inspect& lhs, const signature<F>& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    Object type = rhs.returns().type();
    if (!issubclass(ptr(lhs.return_annotation()), ptr(type))) {
        return false;
    }

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        type = r->type();
        if (r->args()) {
            while (l != l_end && l->pos()) {
                if (!issubclass(ptr(l->type), ptr(type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(type)
                )) {
                    return false;
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                if (!issubclass(ptr(l->type), ptr(type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type),
                    ptr(type)
                )) {
                    return false;
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type),
                ptr(type)
            )) {
                return false;
            }
            ++l;
        }
        ++r;
    }
    return true;
}
template <typename F>
[[nodiscard]] bool operator<=(const signature<F>& lhs, const inspect& rhs) {
    if (lhs.size() < rhs.size()) {
        return false;
    }

    constexpr auto issubclass = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_IsSubclass(lhs, rhs);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    Object type = lhs.returns().type();
    if (!issubclass(ptr(type), ptr(lhs.return_annotation()))) {
        return false;
    }

    auto l = lhs.begin();
    auto l_end = lhs.end();
    auto r = rhs.begin();
    auto r_end = rhs.end();
    while (r != r_end) {
        if (r->args()) {
            while (l != l_end && l->pos()) {
                if (!issubclass(ptr(l->type()), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->args()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type()),
                    ptr(r->type)
                )) {
                    return false;
                }
                ++l;
            }
        } else if (r->kwargs()) {
            while (l != l_end && l->kwonly()) {
                if (!issubclass(ptr(l->type()), ptr(r->type))) {
                    return false;
                }
                ++l;
            }
            if (l != l_end && l->kwargs()) {
                if (l->name != r->name || !issubclass(
                    ptr(l->type()),
                    ptr(r->type)
                )) {
                    return false;
                }
                ++l;
            }
        } else {
            if (l == l_end || l->name != r->name || l->kind != r->kind || !issubclass(
                ptr(l->type()),
                ptr(r->type)
            )) {
                return false;
            }
            ++l;
        }
        ++r;
    }
    return true;
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator<=(const signature<L>& lhs, const signature<R>& rhs) {
    using LHS = signature<L>;
    using RHS = signature<R>;

    if constexpr (
        LHS::size() < RHS::size() ||
        !issubclass<typename LHS::Return, typename RHS::Return>()
    ) {
        return false;
    }

    return []<size_t I = 0, size_t J = 0>(this auto&& self) {
        // recursive case
        if constexpr (J < RHS::size()) {
            using right = RHS::template at<J>;

            // variadic positional
            if constexpr (ArgTraits<right>::args()) {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (ArgTraits<left>::pos()) {
                        if constexpr (!issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >();
                    } else if constexpr (ArgTraits<left>::args()) {
                        if constexpr (
                            ArgTraits<left>::name != ArgTraits<right>::name ||
                            !issubclass<
                                typename ArgTraits<left>::type,
                                typename ArgTraits<right>::type
                            >()
                        ) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >();
                    } else {
                        return std::forward<decltype(self)>(self).template operator()<
                            I,
                            J + 1
                        >();
                    }
                } else {
                    return false;
                }

            // variadic keywords
            } else if constexpr (ArgTraits<right>::kwargs()) {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (ArgTraits<left>::kwonly()) {
                        if constexpr (!issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >();
                    } else if constexpr (ArgTraits<left>::kwargs()) {
                        if constexpr (
                            ArgTraits<left>::name != ArgTraits<right>::name ||
                            !issubclass<
                                typename ArgTraits<left>::type,
                                typename ArgTraits<right>::type
                            >()
                        ) {
                            return false;
                        }
                        return std::forward<decltype(self)>(self).template operator()<
                            I + 1,
                            J
                        >();
                    } else {
                        return std::forward<decltype(self)>(self).template operator()<
                            I,
                            J + 1
                        >();
                    }
                } else {
                    return false;
                }

            // single arg
            } else {
                if constexpr (I < LHS::size()) {
                    using left = LHS::template at<I>;
                    if constexpr (
                        ArgTraits<left>::name != ArgTraits<right>::name ||
                        ArgTraits<left>::kind != ArgTraits<right>::kind ||
                        !issubclass<
                            typename ArgTraits<left>::type,
                            typename ArgTraits<right>::type
                        >()
                    ) {
                        return false;
                    }
                    return std::forward<decltype(self)>(self).template operator()<
                        I + 1,
                        J + 1
                    >();
                } else {
                    return false;
                }
            }

        // base case
        } else {
            return true;
        }
    }();
}


template <typename F>
[[nodiscard]] bool operator==(const inspect& lhs, const signature<F>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    if (!isequal(
        ptr(lhs.return_annotation()),
        ptr(rhs.returns().type())
    )) {
        return false;
    }

    auto l = lhs.begin();
    auto r = rhs.begin();
    auto end = rhs.end();
    while (r != end) {
        if (l->name != r->name || l->kind != r->kind || !isequal(
            ptr(l->type),
            ptr(r->type())
        )) {
            return false;
        }
        ++l;
        ++r;
    }
    return true;
}
template <typename F>
[[nodiscard]] bool operator==(const signature<F>& lhs, const inspect& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    constexpr auto isequal = [](PyObject* lhs, PyObject* rhs) {
        int rc = PyObject_RichCompareBool(lhs, rhs, Py_EQ);
        if (rc < 0) {
            Exception::from_python();
        }
        return rc;
    };

    if (!isequal(
        ptr(lhs.returns().type()),
        ptr(rhs.return_annotation())
    )) {
        return false;
    }

    auto l = lhs.begin();
    auto r = rhs.begin();
    auto end = rhs.end();
    while (r != end) {
        if (l->name != r->name || l->kind != r->kind || !isequal(
            ptr(l->type()),
            ptr(r->type)
        )) {
            return false;
        }
        ++l;
        ++r;
    }
    return true;
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator==(const signature<L>& lhs, const signature<R>& rhs) {
    using LHS = signature<L>;
    using RHS = signature<R>;

    if constexpr (
        LHS::size() != RHS::size() ||
        !std::same_as<typename LHS::Return, typename RHS::Return>
    ) {
        return false;
    }

    return []<size_t I = 0>(this auto&& self) {
        if constexpr (I < RHS::size()) {
            using left = LHS::template at<I>;
            using right = RHS::template at<I>;
            return
                std::same_as<left, right> &&
                std::forward<decltype(self)>(self).template operator()<I + 1>();
        } else {
            return true;
        }
    }();
}


template <typename F>
[[nodiscard]] bool operator!=(const inspect& lhs, const signature<F>& rhs) {
    return !(lhs == rhs);
}
template <typename F>
[[nodiscard]] bool operator!=(const signature<F>& lhs, const inspect& rhs) {
    return !(rhs == lhs);
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator!=(const signature<L>& lhs, const signature<R>& rhs) {
    return !(lhs == rhs);
}


template <typename F>
[[nodiscard]] bool operator>=(const inspect& lhs, const signature<F>& rhs) {
    return rhs <= lhs;
}
template <typename F>
[[nodiscard]] bool operator>=(const signature<F>& lhs, const inspect& rhs) {
    return rhs <= lhs;
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator>=(const signature<L>& lhs, const signature<R>& rhs) {
    return rhs <= lhs;
}


template <typename F>
[[nodiscard]] bool operator>(const inspect& lhs, const signature<F>& rhs) {
    return rhs < lhs;
}
template <typename F>
[[nodiscard]] bool operator>(const signature<F>& lhs, const inspect& rhs) {
    return rhs < lhs;
}
template <typename L, typename R>
[[nodiscard]] constexpr bool operator>(const signature<L>& lhs, const signature<R>& rhs) {
    return rhs < lhs;
}


////////////////////////
////    FUNCTION    ////
////////////////////////


template <typename F = Object(Arg<"*args", Object>, Arg<"**kwargs", Object>)>
    requires (
        impl::canonical_function_type<F> &&
        signature<F>::args_fit_within_bitset &&
        signature<F>::no_qualified_args &&
        signature<F>::no_qualified_return &&
        signature<F>::proper_argument_order &&
        signature<F>::no_duplicate_args &&
        signature<F>::args_are_python &&
        signature<F>::return_is_python
    )
struct Function;


/// TODO: CTAD guides take in a function annotated with Arg<>, which has no bound
/// arguments, as well as a list of partial arguments.  It then extends the annotation
/// for each argument, synthesizing a new signature that binds the partial arguments
/// and discards any defaults that might be present for those arguments.  The
/// synthesized signature is what is then used to construct and call the Function<>
/// object.


template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(std::string, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(std::string, Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial>
    requires (partially_callable<F, Partial...> && Defaults<F>::n == 0)
Function(std::string, std::string, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;

template <typename F, typename... Partial> requires (partially_callable<F, Partial...>)
Function(std::string, std::string, Defaults<F>, F&&, Partial&&...)
    -> Function<typename impl::get_signature<F>::to_ptr::type>;


namespace impl {

    /* decorators with and without arguments:

    def name(_func=None, *, key1=value1, key2=value2, ...):
        def decorator_name(func):
            ...  # Create and return a wrapper function.
            return func

        if _func is None:
            return decorator_name
        else:
            return decorator_name(_func)

    */
    /// TODO: ^ that is really hard to do from C++, particularly as it relates to
    /// function capture.

    /* A descriptor proxy for an unbound Bertrand function, which enables the
    `func.method` access specifier.  Unlike the others, this descriptor is never
    attached to a type, it merely forwards the underlying function to match Python's
    PyFunctionObject semantics, and leverage optimizations in the type flags, etc. */
    struct Method : PyObject {
        static constexpr static_str __doc__ =
R"doc(A descriptor that binds a Bertrand function as an instance method of a Python
class.

Notes
-----
The `func.method` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.method & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Note that unlike the other descriptors, this one is not actually attached to
the decorated type.  Instead, it is used to expose the structural operators for
consistency with the rest of the function interface, and will attach the
underlying function (rather than this descriptor) when invoked.  This allows
for optimizations in the underlying CPython API, and conforms to Python's
ordinary function semantics.

Examples
--------
This descriptor is primarily used via the `@func.method` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(self, x: int) -> int:
...     return x + 1
...
>>> @foo.method
... class Bar:
...     pass
...
>>> Bar().foo(1)
2

It is also possible to create a Bertrand method in-place by explicitly calling
the `@bertrand` decorator on a standard method declaration, just like you would
for a non-member Bertrand function.

>>> class Baz:
...     @bertrand
...     def foo(self, x: int) -> int:
...         return x + 1
...
>>> Baz().foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining methods in Bertrand.

Additionally, the result of the `bertrand.method` property can be used in
`isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.method)  # Bar() implements foo as an instance method
True
>>> issubclass(Bar, foo.method)  # Bar implements foo as an instance method
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.method`, although that is by
far the easiest way to guarantee that this check always succeeds.  Technically,
any type for which `obj.foo(...)` is well-formed will pass the check,
regardless of how that method is exposed, making this a true structural type
check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;

        explicit Method(const Object& func) : func(func) {}
        explicit Method(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(Method* self) noexcept {
            self->~Method();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Method* self = reinterpret_cast<Method*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) Method(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            Method* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:method",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object wrapped = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                getattr<"bind_partial">(
                    getattr<"__signature__">(wrapped)
                )(None);
                self->func = wrapped;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(Method* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            Method* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// TODO: accept a single, optional positional-only argument plus
                /// optional keyword-only args.  Then, within the body of this
                /// function, create another function that takes a single argument,
                /// which is the actual decorator itself.  This gets complicated, but
                /// is necessary to allow easy use from Python itself.

                /// TODO: maybe I return a PyFunction here?  I can maybe use the Code
                /// constructor to create the internal function?  That gets a little
                /// spicy, but maybe I can use py::Function<> itself instead?  That
                /// would allow me to use a capturing lambda here, which is much
                /// closer to the Python syntax.  That would require a forward
                /// declaration here, though.  And/or the CTAD constructors would
                /// need to be moved up above this point.



                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_method">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            Method* self,
            PyObject* obj,
            PyObject* type
        ) noexcept { 
            PyTypeObject* cls = Py_TYPE(ptr(self->func));
            return cls->tp_descr_get(ptr(self->func), obj, type);
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<Method*>(lhs)->func),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<Method*>(rhs)->func)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<Method*>(lhs)->func),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<Method*>(rhs)->func)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(Method* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(obj, ptr(self->func));
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(Method* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(cls, ptr(self->func));
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(Method* self) noexcept {
            try {
                std::string str = "<method(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__)
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject Method::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Method).name(),
        .tp_basicsize = sizeof(Method),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Method::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Method::__repr__),
        .tp_as_number = &Method::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(Method::__doc__),
        .tp_methods = Method::methods,
        .tp_getset = Method::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Method::__get__),
        .tp_init = reinterpret_cast<initproc>(&Method::__init__),
        .tp_new = reinterpret_cast<newfunc>(&Method::__new__),
        .tp_vectorcall_offset = offsetof(Method, __vectorcall__)
    };

    /* A `@classmethod` descriptor for a Bertrand function type, which references an
    unbound function and produces bound equivalents that pass the enclosing type as the
    first argument when accessed. */
    struct ClassMethod : PyObject {
        static constexpr static_str __doc__ =
R"doc(A descriptor that binds a Bertrand function as a class method of a Python
class.

Notes
-----
The `func.classmethod` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.classmethod` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> Bar.foo(1)
2

It is also possible to create a classmethod in-place by explicitly calling
`@bertrand.classmethod` within a class definition, just like the normal
Python `@classmethod` decorator.

>>> class Baz:
...     @bertrand.classmethod
...     def foo(cls, x: int) -> int:
...         return x + 1
...
>>> Baz.foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining class methods in Bertrand.

Additionally, the result of the `bertrand.classmethod` property can be used
in `isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(cls, x: int) -> int:
...     return x + 1
...
>>> @foo.classmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.classmethod)  # Bar() implements foo as a classmethod
True
>>> issubclass(Bar, foo.classmethod)  # Bar implements foo as a classmethod
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.classmethod`, although that is
by far the easiest way to guarantee that this check always succeeds.
Technically, any type for which `obj.foo(...)` is well-formed will pass the
check, regardless of how that method is exposed, making this a true structural
type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;
        Object member_type;

        explicit ClassMethod(const Object& func) : func(func) {}
        explicit ClassMethod(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(ClassMethod* self) noexcept {
            self->~ClassMethod();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                ClassMethod* self = reinterpret_cast<ClassMethod*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) ClassMethod(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            ClassMethod* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:classmethod",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object wrapped = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                getattr<"bind_partial">(
                    getattr<"__signature__">(wrapped)
                )(None);
                self->func = wrapped;
                self->member_type = None;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(ClassMethod* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            ClassMethod* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                PyObject* result = PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_classmethod">()),
                    forward,
                    3,
                    nullptr
                );
                if (result == nullptr) {
                    return nullptr;
                }
                try {
                    self->member_type = self->member_function_type(
                        bertrand,
                        reinterpret_borrow<Object>(cls)
                    );
                } catch (...) {
                    Py_DECREF(result);
                    throw;
                }
                return result;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            ClassMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            PyObject* cls = type == Py_None ?
                reinterpret_cast<PyObject*>(Py_TYPE(obj)) :
                type;
            if (self->member_type.is(None)) {
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                self->member_type = self->member_function_type(
                    bertrand,
                    reinterpret_borrow<Object>(cls)
                );
            }
            PyObject* const args[] = {
                ptr(self->member_type),
                ptr(self->func),
                cls,
            };
            return PyObject_VectorcallMethod(
                ptr(template_string<"_capture">()),
                args,
                3,
                nullptr
            );
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<ClassMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<ClassMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<ClassMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<ClassMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(ClassMethod* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(ClassMethod* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(ClassMethod* self) noexcept {
            try {
                std::string str = "<classmethod(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        Object member_function_type(const Object& bertrand, const Object& cls) const {
            Object key = getattr<"__template_key__">(func);
            Py_ssize_t len = PyTuple_GET_SIZE(ptr(key));
            Object new_key = reinterpret_steal<Object>(PyTuple_New(len - 1));
            if (new_key.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = reinterpret_steal<Object>(PySlice_New(
                ptr(reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(&PyType_Type)
                )[cls]),
                Py_None,
                reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(key), 0)
                )->step
            ));
            if (rtype.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(new_key), 0, release(rtype));
            for (Py_ssize_t i = 2; i < len; ++i) {
                PyTuple_SET_ITEM(
                    ptr(new_key),
                    i - 1,
                    Py_NewRef(PyTuple_GET_ITEM(ptr(key), i))
                );
            }
            Object specialization = reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(func)))
            )[new_key];
            return getattr<"Function">(bertrand)[specialization];
        }

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object self_type = getattr<"_self_type">(func);
            if (self_type.is(None)) {
                throw TypeError("function must accept at least one positional argument");
            }
            Object specialization = member_function_type(bertrand, self_type);
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(func)),
                ptr(specialization),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject ClassMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(ClassMethod).name(),
        .tp_basicsize = sizeof(ClassMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&ClassMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&ClassMethod::__repr__),
        .tp_as_number = &ClassMethod::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(ClassMethod::__doc__),
        .tp_methods = ClassMethod::methods,
        .tp_getset = ClassMethod::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&ClassMethod::__get__),
        .tp_init = reinterpret_cast<initproc>(&ClassMethod::__init__),
        .tp_new = reinterpret_cast<newfunc>(&ClassMethod::__new__),
        .tp_vectorcall_offset = offsetof(ClassMethod, __vectorcall__)
    };

    /* A `@staticmethod` descriptor for a C++ function type, which references an
    unbound function and directly forwards it when accessed. */
    struct StaticMethod : PyObject {
        static constexpr static_str __doc__ =
R"doc(A descriptor that binds a Bertrand function as a static method of a Python
class.

Notes
-----
The `func.staticmethod` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.staticmethod` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(x: int) -> int:
...     return x + 1
...
>>> @foo.staticmethod
... class Bar:
...     pass
...
>>> Bar.foo(1)
2

It is also possible to create a staticmethod in-place by explicitly calling
`@bertrand.staticmethod` within a class definition, just like the normal
Python `@staticmethod` decorator.

>>> class Baz:
...     @bertrand.staticmethod
...     def foo(x: int) -> int:
...         return x + 1
...
>>> Baz.foo(1)
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining static methods in Bertrand.

Additionally, the result of the `bertrand.staticmethod` property can be used
in `isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(x: int) -> int:
...     return x + 1
...
>>> @foo.staticmethod
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.staticmethod)  # Bar() implements foo as a staticmethod
True
>>> issubclass(Bar, foo.staticmethod)  # Bar implements foo as a staticmethod
True

This works by checking whether the operand has an attribute `foo`, which is a
callable with the same signature as the free-standing function.  Note that
this does not strictly require the use of `@foo.staticmethod`, although that is
by far the easiest way to guarantee that this check always succeeds.
Technically, any type for which `obj.foo(...)` is well-formed will pass the
check, regardless of how that method is exposed, making this a true structural
type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object func;

        explicit StaticMethod(const Object& func) : func(func) {}
        explicit StaticMethod(Object&& func) : func(std::move(func)) {}

        static void __dealloc__(StaticMethod* self) noexcept {
            self->~StaticMethod();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                StaticMethod* self = reinterpret_cast<StaticMethod*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) StaticMethod(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            StaticMethod* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                const char* kwlist[] = {nullptr};
                PyObject* func;
                if (PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O:staticmethod",
                    const_cast<char**>(kwlist),
                    &func
                )) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                self->func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(func)
                );
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(StaticMethod* self, void*) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __call__(
            StaticMethod* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "staticmethod() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "staticmethod() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->func),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_staticmethod">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            StaticMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return Py_NewRef(ptr(self->func));
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<StaticMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<StaticMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<StaticMethod*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<StaticMethod*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(StaticMethod* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(StaticMethod* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(StaticMethod* self) noexcept {
            try {
                std::string str = "<staticmethod(" + repr(self->func) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(func)),
                reinterpret_cast<PyObject*>(Py_TYPE(ptr(func))),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(&StaticMethod::__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject StaticMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(StaticMethod).name(),
        .tp_basicsize = sizeof(StaticMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&StaticMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&StaticMethod::__repr__),
        .tp_as_number = &StaticMethod::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(StaticMethod::__doc__),
        .tp_getset = StaticMethod::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&StaticMethod::__get__),
        .tp_init = reinterpret_cast<initproc>(&StaticMethod::__init__),
        .tp_new = reinterpret_cast<newfunc>(&StaticMethod::__new__),
        .tp_vectorcall_offset = offsetof(StaticMethod, __vectorcall__)
    };

    /* A `@property` descriptor for a C++ function type that accepts a single
    compatible argument, which will be used as the getter for the property.  Setters
    and deleters can also be registered with the same `self` parameter.  The setter can
    accept any type for the assigned value, allowing overloads. */
    struct Property : PyObject {
        static constexpr static_str __doc__ =
R"doc(A descriptor that binds a Bertrand function as a property getter of a
Python class.

Notes
-----
The `func.property` accessor is actually a property that returns an unbound
instance of this type.  That instance then implements a call operator, which
allows it to be used as a decorator that self-attaches the descriptor to a
Python class.

This architecture allows the unbound descriptor to implement the `&` and `|`
operators, which allow for extremely simple structural types in Python:

```
@bertrand
def func(x: foo | (bar.classmethod & baz.property) | qux.staticmethod) -> int:
    ...
```

This syntax is not available in C++, which requires the use of explicit
`Union<...>` and `Intersection<...>` types instead.

Examples
--------
This descriptor is primarily used via the `@func.property` decorator of a
Bertrand function, which automatically binds the function to the decorated
type.

>>> import bertrand
>>> @bertrand
... def foo(self) -> int:
...     return 2
...
>>> @foo.property
... class Bar:
...     pass
...
>>> Bar().foo
2

It is also possible to create a property in-place by explicitly calling
`@bertrand.property` within a class definition, just like the normal Python
`@property` decorator.

>>> class Baz:
...     @bertrand.property
...     def foo(self) -> int:
...         return 2
...
>>> Baz().foo
2

Both syntaxes achieve the same effect, but the first allows the function to
be defined separately from the class, enables UFCS, and allows for easy
structural typing and function overloading.  It is thus the preferred way of
defining properties in Bertrand.

Additionally, the result of the `bertrand.property` property can be used in
`isinstance()` and `issubclass()` checks in order to enforce the structural
types created by the `&` and `|` operators.

>>> @bertrand
... def foo(self) -> int:
...     return 2
...
>>> @foo.property
... class Bar:
...     pass
...
>>> isinstance(Bar(), foo.property)  # Bar() has an attribute 'foo' with the same return type 
True
>>> issubclass(Bar, foo.property)  # Bar has an attribute 'foo' with the same return type
True

Unlike the `classmethod` and `staticmethod` descriptors, the `property`
descriptor does not require that the resulting attribute is callable, just that
it has the same type as the return type of the free-standing function.  It
effectively devolves into a structural check against a simple type, in this
case equivalent to:

>>> isinstance(Bar(), bertrand.Intersection["foo": int])
True
>>> issubclass(Bar, bertrand.Intersection["foo": int])
True

Technically, any type for which `obj.foo` is well-formed and returns an integer
will pass the check, regardless of how it is exposed, making this a true
structural type check.)doc";

        static PyTypeObject __type__;

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object fget;
        Object fset;
        Object fdel;
        Object doc;

        explicit Property(
            const Object& fget,
            const Object& fset = None,
            const Object& fdel = None,
            const Object& doc = None
        ) : fget(fget), fset(fset), fdel(fdel), doc(doc)
        {}

        static void __dealloc__(Property* self) noexcept {
            self->~Property();
        }

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Property* self = reinterpret_cast<Property*>(
                    type->tp_alloc(type, 0)
                );
                if (self == nullptr) {
                    return nullptr;
                }
                try {
                    new (self) Property(None);
                } catch (...) {
                    Py_DECREF(self);
                    throw;
                }
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            Property* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object Function = getattr<"Function">(bertrand);
                PyObject* fget = nullptr;
                PyObject* fset = nullptr;
                PyObject* fdel = nullptr;
                PyObject* doc = nullptr;
                const char* const kwnames[] {
                    "fget",
                    "fset",
                    "fdel",
                    "doc",
                    nullptr
                };
                PyArg_ParseTupleAndKeywords(
                    args,
                    kwargs,
                    "O|OOU:property",
                    const_cast<char**>(kwnames),  // necessary for Python API
                    &fget,
                    &fset,
                    &fdel,
                    &doc
                );
                Object getter = Function(reinterpret_borrow<Object>(fget));
                Object self_type = getattr<"_self_type">(getter);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "getter must accept exactly one positional argument"
                    );
                    return -1;
                }
                Object setter = reinterpret_borrow<Object>(fset);
                if (fset) {
                    setter = Function(setter);
                    getattr<"bind">(getattr<"__signature__">(setter))(None, None);
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(setter))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() setter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                Object deleter = reinterpret_borrow<Object>(fdel);
                if (fdel) {
                    deleter = Function(deleter);
                    getattr<"bind">(getattr<"__signature__">(getter))(None);
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(deleter))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() deleter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fget = getter;
                self->fset = setter;
                self->fdel = deleter;
                self->doc = reinterpret_borrow<Object>(doc);
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __wrapped__(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fget));
        }

        static PyObject* get_fget(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fget));
        }

        static int set_fget(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fget = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "getter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fset.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(self->fset))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() getter must accept the same type as "
                            "the setter"
                        );
                        return -1;
                    }
                }
                if (!self->fdel.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(self_type),
                        ptr(getattr<"_self_type">(self->fdel))
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() getter must accept the same type as "
                            "the deleter"
                        );
                        return -1;
                    }
                }
                self->fget = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* get_fset(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fset));
        }

        static int set_fset(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fset = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "setter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fget.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(getattr<"_self_type">(self->fget)),
                        ptr(self_type)
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() setter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fset = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* get_fdel(Property* self, void*) noexcept {
            return Py_NewRef(ptr(self->fdel));
        }

        static int set_fdel(Property* self, PyObject* value, void*) noexcept {
            try {
                if (!value) {
                    self->fdel = None;
                    return 0;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                Object func = getattr<"Function">(bertrand)(
                    reinterpret_borrow<Object>(value)
                );
                Object self_type = getattr<"_self_type">(func);
                if (self_type.is(None)) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "deleter must accept exactly one positional argument"
                    );
                    return -1;
                }
                if (!self->fget.is(None)) {
                    int rc = PyObject_IsSubclass(
                        ptr(getattr<"_self_type">(self->fget)),
                        ptr(self_type)
                    );
                    if (rc < 0) {
                        return -1;
                    } else if (!rc) {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "property() deleter must accept the same type as "
                            "the getter"
                        );
                        return -1;
                    }
                }
                self->fdel = func;
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* getter(Property* self, PyObject* func) noexcept {
            if (set_fget(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fget));
        }

        static PyObject* setter(Property* self, PyObject* func) noexcept {
            if (set_fset(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fset));
        }

        static PyObject* deleter(Property* self, PyObject* func) noexcept {
            if (set_fdel(self, func, nullptr)) {
                return nullptr;
            }
            return Py_NewRef(ptr(self->fdel));
        }

        /// TODO: Property::__call__() should also accept optional setter/deleter/
        /// docstring as keyword-only arguments, so that you can use
        /// `@func.property(setter=fset, deleter=fdel, doc="docstring")`.

        /// TODO: in fact, each of the previous descriptors' call operators may want
        /// to accept an optional docstring.

        static PyObject* __call__(
            Property* self,
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if (kwnames) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() does not accept keyword arguments"
                    );
                    return nullptr;
                }
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires exactly one positional argument"
                    );
                    return nullptr;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                    ptr(template_string<"bertrand">())
                ));
                if (bertrand.is(nullptr)) {
                    Exception::from_python();
                }
                /// TODO: _bind_property() may need to check the self argument of
                /// multiple functions simultaneously.
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    ptr(self->fget),
                    cls,
                    self
                };
                return PyObject_VectorcallMethod(
                    ptr(template_string<"_bind_property">()),
                    forward,
                    3,
                    nullptr
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get__(
            Property* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return PyObject_CallOneArg(ptr(self->fget), obj);
        }

        static PyObject* __set__(
            Property* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            try {
                if (value) {
                    if (self->fset.is(None)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "property '%U' of '%R' object has no setter",
                            ptr(getattr<"__name__">(self->fget)),
                            reinterpret_cast<PyObject*>(Py_TYPE(obj))
                        );
                        return nullptr;
                    }
                    PyObject* const args[] = {obj, value};
                    return PyObject_Vectorcall(
                        ptr(self->fset),
                        args,
                        2,
                        nullptr
                    );
                }

                if (self->fdel.is(None)) {
                    PyErr_Format(
                        PyExc_AttributeError,
                        "property '%U' of '%R' object has no deleter",
                        ptr(getattr<"__name__">(self->fget)),
                        reinterpret_cast<PyObject*>(Py_TYPE(obj))
                    );
                    return nullptr;
                }
                return PyObject_CallOneArg(ptr(self->fdel), obj);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<Property*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<Property*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(Py_TYPE(lhs), &__type__)) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<Property*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<Property*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(Property* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(Property* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(Property* self) noexcept {
            try {
                std::string str = "<property(" + repr(self->fget) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __get_doc__(Property* self, void*) noexcept {
            if (!self->doc.is(None)) {
                return Py_NewRef(ptr(self->doc));
            }
            return release(getattr<"__doc__">(self->fget));
        }

    private:

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = getattr<"_return_type">(fget);
            if (rtype.is(None)) {
                throw TypeError("getter must not return void");
            }
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(getattr<"__name__">(fget)),
                ptr(rtype),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        /// TODO: document these?

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {
                "getter",
                reinterpret_cast<PyCFunction>(&getter),
                METH_O,
                nullptr
            },
            {
                "setter",
                reinterpret_cast<PyCFunction>(&setter),
                METH_O,
                nullptr
            },
            {
                "deleter",
                reinterpret_cast<PyCFunction>(&deleter),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__wrapped__",
                reinterpret_cast<::getter>(&__wrapped__),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fget",
                reinterpret_cast<::getter>(&get_fget),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fset",
                reinterpret_cast<::getter>(&get_fset),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "fdel",
                reinterpret_cast<::getter>(&get_fdel),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "__doc__",
                reinterpret_cast<::getter>(&__get_doc__),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    PyTypeObject Property::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Property).name(),
        .tp_basicsize = sizeof(Property),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Property::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Property::__repr__),
        .tp_as_number = &Property::number,
        .tp_call = PyVectorcall_Call,
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(Property::__doc__),
        .tp_methods = Property::methods,
        .tp_getset = Property::getset,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Property::__get__),
        .tp_descr_set = reinterpret_cast<descrsetfunc>(&Property::__set__),
        .tp_init = reinterpret_cast<initproc>(&Property::__init__),
        .tp_new = reinterpret_cast<newfunc>(&Property::__new__),
        .tp_vectorcall_offset = offsetof(Property, __vectorcall__),
    };

    // /* The Python `bertrand.Function[]` template interface type, which holds all
    // instantiations, each of which inherit from this class, and allows for CTAD-like
    // construction via the `__new__()` operator.  Has no interface otherwise, requiring
    // the user to manually instantiate it as if it were a C++ template. */
    // struct FunctionTemplates : PyObject {

    //     /// TODO: this HAS to be a heap type because it is an instance of the metaclass,
    //     /// and therefore always has mutable state.
    //     /// -> Maybe when writing bindings, this is just given as a function to the
    //     /// binding generator, and it would be responsible for implementing the
    //     /// template interface's CTAD constructor, and it would use Python-style
    //     /// argument annotations just like any other function.

    //     /// TODO: Okay, the way to do this is to have the bindings automatically
    //     /// populate tp_new with an overloadable function, and then the user can
    //     /// register overloads directly from Python.  The function you supply to the
    //     /// binding helper would be inserted as the base case, which defaults to
    //     /// raising a TypeError if the user tries to instantiate the template.  If
    //     /// that is the case, then I might be able to automatically register overloads
    //     /// as each type is instantiated, in a way that doesn't cause errors if the
    //     /// overload conflicts with an existing one.
    //     /// -> If I implement that using argument annotations, then this gets
    //     /// substantially simpler as well, since I don't need to extract the arguments
    //     /// manually.

    //     /// TODO: remember to set tp_vectorcall to this method, so I don't need to
    //     /// implement real __new__/__init__ constructors.
    //     static PyObject* __new__(
    //         FunctionTemplates* self,
    //         PyObject* const* args,
    //         size_t nargsf,
    //         PyObject* kwnames
    //     ) {
    //         try {
    //             size_t nargs = PyVectorcall_NARGS(nargsf);
    //             size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
    //             if (nargs != 1) {
    //                 throw TypeError(
    //                     "expected a single, positional-only argument, but "
    //                     "received " + std::to_string(nargs)
    //                 );
    //             }
    //             PyObject* func = args[0];
    //             Object name = reinterpret_steal<Object>(nullptr);
    //             Object doc = reinterpret_steal<Object>(nullptr);
    //             if (kwcount) {
    //                 for (size_t i = 0; i < kwcount; ++i) {
    //                     PyObject* key = PyTuple_GET_ITEM(kwnames, i);
    //                     int is_name = PyObject_RichCompareBool(
    //                         key,
    //                         ptr(template_string<"name">()),
    //                         Py_EQ
    //                     );
    //                     if (is_name < 0) {
    //                         Exception::from_python();
    //                     } else if (is_name) {
    //                         name = reinterpret_borrow<Object>(args[nargs + i]);
    //                         if (!PyUnicode_Check(ptr(name))) {
    //                             throw TypeError(
    //                                 "expected 'name' to be a string, but received " +
    //                                 repr(name)
    //                             );
    //                         }
    //                     }
    //                     int is_doc = PyObject_RichCompareBool(
    //                         key,
    //                         ptr(template_string<"doc">()),
    //                         Py_EQ
    //                     );
    //                     if (is_doc < 0) {
    //                         Exception::from_python();
    //                     } else if (is_doc) {
    //                         doc = reinterpret_borrow<Object>(args[nargs + i]);
    //                         if (!PyUnicode_Check(ptr(doc))) {
    //                             throw TypeError(
    //                                 "expected 'doc' to be a string, but received " +
    //                                 repr(doc)
    //                             );
    //                         }
    //                     }
    //                     if (!is_name && !is_doc) {
    //                         throw TypeError(
    //                             "unexpected keyword argument '" +
    //                             repr(reinterpret_borrow<Object>(key)) + "'"
    //                         );
    //                     }
    //                 }
    //             }

    //             // inspect the input function and subscript the template interface to
    //             // get the correct specialization
    //             impl::Inspect signature = {
    //                 func,
    //                 impl::fnv1a_seed,
    //                 impl::fnv1a_prime
    //             };
    //             Object specialization = reinterpret_steal<Object>(
    //                 PyObject_GetItem(
    //                     self,
    //                     ptr(signature.template_key())
    //                 )
    //             );
    //             if (specialization.is(nullptr)) {
    //                 Exception::from_python();
    //             }

    //             // if the parameter list contains unions, then we need to default-
    //             // initialize the specialization and then register separate overloads
    //             // for each path through the parameter list.  Note that if the function
    //             // is the only argument and already exactly matches the deduced type,
    //             // then we can just return it directly to avoid unnecessary nesting.
    //             Object result = reinterpret_steal<Object>(nullptr);
    //             if (signature.size() > 1) {
    //                 if (!kwcount) {
    //                     if (specialization.is(
    //                         reinterpret_cast<PyObject*>(Py_TYPE(func))
    //                     )) {
    //                         return release(specialization);
    //                     }
    //                     result = reinterpret_steal<Object>(PyObject_CallNoArgs(
    //                         ptr(specialization)
    //                     ));
    //                 } else if (name.is(nullptr)) {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(doc),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 } else if (doc.is(nullptr)) {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(name),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 } else {
    //                     PyObject* args[] = {
    //                         nullptr,
    //                         ptr(name),
    //                         ptr(doc),
    //                     };
    //                     result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                         ptr(specialization),
    //                         args,
    //                         kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                         kwnames
    //                     ));
    //                 }
    //                 if (result.is(nullptr)) {
    //                     Exception::from_python();
    //                 }
    //                 Object rc = reinterpret_steal<Object>(PyObject_CallMethodOneArg(
    //                     ptr(result),
    //                     ptr(impl::template_string<"overload">()),
    //                     func
    //                 ));
    //                 if (rc.is(nullptr)) {
    //                     Exception::from_python();
    //                 }
    //                 return release(result);
    //             }

    //             // otherwise, we can initialize the specialization directly, which
    //             // captures the function and uses it as the base case
    //             if (!kwcount) {
    //                 if (specialization.is(
    //                     reinterpret_cast<PyObject*>(Py_TYPE(func))
    //                 )) {
    //                     return release(specialization);
    //                 }
    //                 result = reinterpret_steal<Object>(PyObject_CallOneArg(
    //                     ptr(specialization),
    //                     func
    //                 ));
    //             } else if (name.is(nullptr)) {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(doc),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             } else if (doc.is(nullptr)) {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(name),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             } else {
    //                 PyObject* args[] = {
    //                     nullptr,
    //                     func,
    //                     ptr(name),
    //                     ptr(doc),
    //                 };
    //                 result = reinterpret_steal<Object>(PyObject_Vectorcall(
    //                     ptr(specialization),
    //                     args,
    //                     kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
    //                     kwnames
    //                 ));
    //             }
    //             if (result.is(nullptr)) {
    //                 Exception::from_python();
    //             }
    //             return release(result);

    //         } catch (...) {
    //             Exception::to_python();
    //             return nullptr;
    //         }
    //     }

    // };

}  // namespace impl


template <typename F>
struct interface<Function<F>> : impl::FunctionTag {

    /* The normalized function pointer type for this specialization. */
    using Signature = impl::Signature<F>::type;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = impl::Signature<F>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature. */
    using Defaults = impl::Signature<F>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = impl::Signature<F>::Overloads;

    /* The function's return type. */
    using Return = impl::Signature<F>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return =
        Function<typename impl::Signature<F>::template with_return<R>::type>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self =
        Function<typename impl::Signature<F>::template with_self<C>::type>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_args &&
            impl::Arguments<A...>::no_qualified_arg_annotations
        )
    using with_args =
        Function<typename impl::Signature<F>::template with_args<A...>::type>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = false;

    template <typename Func>
        requires (impl::Signature<std::remove_cvref_t<Func>>::enable)
    static constexpr bool compatible<Func> =
        []<size_t... Is>(std::index_sequence<Is...>) {
            return impl::Signature<F>::template compatible<
                typename impl::Signature<std::remove_cvref_t<Func>>::Return,
                typename impl::Signature<std::remove_cvref_t<Func>>::template at<Is>...
            >;
        }(std::make_index_sequence<impl::Signature<std::remove_cvref_t<Func>>::n>{});

    template <typename Func>
        requires (
            !impl::Signature<std::remove_cvref_t<Func>>::enable &&
            impl::inherits<Func, impl::FunctionTag>
        )
    static constexpr bool compatible<Func> = compatible<
        typename std::remove_reference_t<Func>::Signature
    >;

    template <typename Func>
        requires (
            !impl::Signature<Func>::enable &&
            !impl::inherits<Func, impl::FunctionTag> &&
            impl::has_call_operator<Func>
        )
    static constexpr bool compatible<Func> = 
        impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::enable &&
        compatible<
            typename impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::
            template with_self<void>::type
        >;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = impl::Signature<F>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = impl::Signature<F>::template Bind<Args...>::enable;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::n_opt_pos;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = impl::Signature<F>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = impl::Signature<F>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <static_str Name>
    static constexpr bool has = impl::Signature<F>::template has<Name>;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::has_opt_pos;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = impl::Signature<F>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = impl::Signature<F>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = impl::Signature<F>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = impl::Signature<F>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = impl::Signature<F>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <static_str Name> requires (has<Name>)
    static constexpr size_t idx = impl::Signature<F>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = impl::Signature<F>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = impl::Signature<F>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = impl::Signature<F>::opt_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::opt_pos_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = impl::Signature<F>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = impl::Signature<F>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = impl::Signature<F>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = impl::Signature<F>::kwargs_index;

    /* Get the (annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = impl::Signature<F>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr inspect::Required required = impl::Signature<F>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = impl::Signature<F>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = impl::Signature<F>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Register an overload for this function from C++. */
    template <typename Self, typename Func>
        requires (
            !std::is_const_v<std::remove_reference_t<Self>> &&
            compatible<Func>
        )
    void overload(this Self&& self, const Function<Func>& func);

    /// TODO: key() should return the function's overload key as a tuple of slices,
    /// for inspection from Python.

    /* Attach the function as a bound method of a Python type. */
    template <typename T>
    void method(this const auto& self, Type<T>& type);

    template <typename T>
    void classmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void staticmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void property(
        this const auto& self,
        Type<T>& type,
        /* setter */,
        /* deleter */
    );

    /// TODO: when getting and setting these properties, do I need to use Attr
    /// proxies for consistency?

    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    /// TODO: __defaults__ should return a std::tuple of default values, as they are
    /// given in the signature.

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    /// TODO: This should return a std::tuple of Python type annotations for each
    /// argument.

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    /// TODO: __signature__, which returns a proper Python `inspect.Signature` object.

};


template <typename F>
struct interface<Type<Function<F>>> {

    /* The normalized function pointer type for this specialization. */
    using Signature = interface<Function<F>>::Signature;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = interface<Function<F>>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = interface<Function<F>>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = interface<Function<F>>::Overloads;

    /* The function's return type. */
    using Return = interface<Function<F>>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return = interface<Function<F>>::template with_return<R>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self = interface<Function<F>>::template with_self<C>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_args &&
            impl::Arguments<A...>::no_qualified_arg_annotations
        )
    using with_args = interface<Function<F>>::template with_args<A...>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = interface<Function<F>>::template compatible<Func>;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = interface<Function<F>>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = interface<Function<F>>::template bind<Args...>;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = interface<Function<F>>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = interface<Function<F>>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = interface<Function<F>>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = interface<Function<F>>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = interface<Function<F>>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = interface<Function<F>>::n_opt_pos;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = interface<Function<F>>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = interface<Function<F>>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <static_str Name>
    static constexpr bool has = interface<Function<F>>::template has<Name>;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = interface<Function<F>>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = interface<Function<F>>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = interface<Function<F>>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = interface<Function<F>>::has_opt_pos;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = interface<Function<F>>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = interface<Function<F>>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = interface<Function<F>>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = interface<Function<F>>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = interface<Function<F>>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <static_str Name> requires (has<Name>)
    static constexpr size_t idx = interface<Function<F>>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = interface<Function<F>>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = interface<Function<F>>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = interface<Function<F>>::opt_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = interface<Function<F>>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = interface<Function<F>>::opt_pos_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = interface<Function<F>>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = interface<Function<F>>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = interface<Function<F>>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = interface<Function<F>>::kwargs_index;

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = interface<Function<F>>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr inspect::Required required = interface<Function<F>>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = interface<Function<F>>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = interface<Function<F>>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Register an overload for this function. */
    template <impl::inherits<interface<Function<F>>> Self, typename Func>
        requires (!std::is_const_v<std::remove_reference_t<Self>> && compatible<Func>)
    void overload(Self&& self, const Function<Func>& func) {
        std::forward<Self>(self).overload(func);
    }

    /* Attach the function as a bound method of a Python type. */
    template <impl::inherits<interface<Function<F>>> Self, typename T>
    void method(const Self& self, Type<T>& type) {
        std::forward<Self>(self).method(type);
    }

    template <impl::inherits<interface<Function<F>>> Self, typename T>
    void classmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).classmethod(type);
    }

    template <impl::inherits<interface<Function<F>>> Self, typename T>
    void staticmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).staticmethod(type);
    }

    template <impl::inherits<interface<Function<F>>> Self, typename T>
    void property(const Self& self, Type<T>& type, /* setter */, /* deleter */) {
        std::forward<Self>(self).property(type);
    }

    template <impl::inherits<interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }

    template <impl::inherits<interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }

    template <impl::inherits<interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);

    template <impl::inherits<interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __annotations__(const Self& self);

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
template <typename F>
    requires (
        impl::canonical_function_type<F> &&
        Signature<F>::args_fit_within_bitset &&
        Signature<F>::no_qualified_args &&
        Signature<F>::no_qualified_return &&
        Signature<F>::proper_argument_order &&
        Signature<F>::no_duplicate_args &&
        Signature<F>::args_are_python &&
        Signature<F>::return_is_python
    )
struct Function : Object, interface<Function<F>> {
private:

    /* Non-member function type. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr static_str __doc__ =
R"doc(A wrapper around a C++ or Python function, which allows it to be used from
both languages.

Notes
-----
This type is not directly instantiable from Python.  Instead, it can only be
accessed through the `bertrand.Function` template interface, which can be
navigated by subscripting the interface according to a possible function
signature.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);
        Object pyfunc = None;
        Object pysignature = None;
        /// TODO: cache the member function type for structural |, &, isinstance(), and issubclass()
        Object member_type = None;
        Object name = None;
        Object docstring = None;
        Sig::Defaults defaults;
        std::function<typename Sig::to_value::type> func;
        Sig::Overloads overloads;

        /* Exposes a C++ function to Python */
        explicit PyFunction(
            Object&& name,
            Object&& docstring,
            Sig::Defaults&& defaults,
            std::function<typename Sig::to_value::type>&& func
        ) : defaults(std::move(defaults)), func(std::move(func)),
            name(std::move(name)), docstring(std::move(docstring))
        {}

        /* Exposes a Python function to C++ by generating a capturing lambda wrapper,
        after a quick signature validation.  The function must exactly match the
        enclosing signature, including argument names, types, and
        posonly/kwonly/optional/variadic qualifiers. */
        explicit PyFunction(
            PyObject* pyfunc,
            PyObject* name = nullptr,
            PyObject* docstring = nullptr,
            impl::Inspect* signature = nullptr
        ) :
            pyfunc(pyfunc),
            defaults([](PyObject* pyfunc, impl::Inspect* signature) {
                if (signature) {
                    return validate_signature(pyfunc, *signature);
                } else {
                    impl::Inspect signature = {pyfunc, Sig::seed, Sig::prime};
                    return validate_signature(pyfunc, signature);
                }
            }(pyfunc, signature)),
            func(Sig::capture(pyfunc))
        {
            this->name = name ? Py_NewRef(name) : PyObject_GetAttr(
                name,
                ptr(impl::template_string<"__name__">())
            );
            if (this->name == nullptr) {
                Exception::from_python();
            }
            this->docstring = docstring ? Py_NewRef(docstring) : PyObject_GetAttr(
                docstring,
                ptr(impl::template_string<"__doc__">())
            );
            if (this->docstring == nullptr) {
                Py_DECREF(this->name);
                Exception::from_python();
            }
            Py_INCREF(this->pyfunc);
        }

        template <static_str ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
            if (self == nullptr) {
                return nullptr;
            }
            self->__vectorcall__ = reinterpret_cast<vectorcallfunc>(__call__);
            new (&self->pyfunc) Object(None);
            new (&self->pysignature) Object(None);
            new (&self->member_function_type) Object(None);
            new (&self->name) Object(None);
            new (&self->docstring) Object(None);
            new (&self->defaults) Sig::Defaults();
            new (&self->func) std::function<typename Sig::to_value::type>();
            new (&self->overloads) Sig::Overloads();
            return reinterpret_cast<PyObject*>(self);
        }

        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            /// TODO: if no positional arguments are provided, generate a default base
            /// function that immediately raises a TypeError.  In this case, the name
            /// and docstring can be passed in as keyword arguments, otherwise they
            /// are inferred from the function itself.
            /// -> Actually what I should do is allow the keyword arguments to be
            /// supplied at all times, in order to allow for binding lambdas and other
            /// function objects in Python.

            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs > 1) {
                    throw TypeError(
                        "expected at most one positional argument, but received " +
                        std::to_string(nargs)
                    );
                }
                Object name = reinterpret_steal<Object>(nullptr);
                Object doc = reinterpret_steal<Object>(nullptr);
                if (kwargs) {
                    name = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"name">())
                    ));
                    if (!name.is(nullptr) && !PyUnicode_Check(ptr(name))) {
                        throw TypeError(
                            "expected 'name' to be a string, not: " + repr(name)
                        );
                    }
                    doc = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"doc">())
                    ));
                    if (!doc.is(nullptr) && !PyUnicode_Check(ptr(doc))) {
                        throw TypeError(
                            "expected 'doc' to be a string, not: " + repr(doc)
                        );
                    }
                    Py_ssize_t observed = name.is(nullptr) + doc.is(nullptr);
                    if (observed != PyDict_Size(kwargs)) {
                        throw TypeError(
                            "received unexpected keyword argument(s): " +
                            repr(reinterpret_borrow<Object>(kwargs))
                        );
                    }
                }

                if (nargs == 0) {
                    /// TODO: generate a default base function that raises a TypeError
                    /// when called, and forward to first constructor.
                }


                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                // remember the original signature for the benefit of static analyzers,
                // documentation purposes, etc.
                new (self) PyFunction(
                    func,
                    nullptr,  /// TODO: name and docstring passed into constructor as kwargs
                    nullptr,
                    &signature
                );
                self->pysignature = release(signature.signature);
                PyObject_GC_Track(self);
                return 0;

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /// TODO: implement a private _call() method that avoids conversions and
        /// directly invokes the function with the preconverted vectorcall arguments.
        /// That would make the overload system signficantly faster, since it avoids
        /// extra heap allocations and overload checks.

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// convert the vectorcall arguments into bertrand types
                typename Sig::Vectorcall vectorcall {args, nargsf, kwnames};

                // check for overloads and forward if one is found
                if (self->overloads.root) {
                    PyObject* overload = self->overloads.search_instance(
                        vectorcall.key()
                    );
                    if (overload) {
                        return PyObject_Vectorcall(
                            overload,
                            vectorcall.args(),
                            vectorcall.nargsf(),
                            vectorcall.kwnames()
                        );
                    }
                }

                // if this function wraps a captured Python function, then we can
                // immediately forward to it as an optimization
                if (!self->pyfunc.is(None)) {
                    return PyObject_Vectorcall(
                        ptr(self->pyfunc),
                        vectorcall.args(),
                        vectorcall.nargsf(),
                        vectorcall.kwnames()
                    );
                }

                // otherwise, we fall back to the base C++ implementation, which
                // translates the arguments according to the template signature
                return release(to_python(
                    vectorcall(self->defaults, self->func)
                ));

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Bind a set of arguments to this function, producing a partial function that
        injects them 
         */
        static PyObject* bind(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            /// TODO: get the types of all the arguments, confirm that they match the
            /// enclosing signature, and then produce a corresponding function type,
            /// which will probably involve a private constructor call.  I might be
            /// able to determine the type ahead of time, and then call its Python-level
            /// constructor to do the validation + error handling.
        }

        /* Simulate a function call, returning the overload that would be chosen if
        the function were to be called with the given arguments, or None if they are
        malformed. */
        static PyObject* resolve(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                typename Sig::Vectorcall vectorcall {args, nargsf, kwnames};
                std::optional<PyObject*> func =
                    self->overloads.get_instance(vectorcall.key());
                PyObject* value = func.value_or(Py_None);
                return Py_NewRef(value ? value : self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Register an overload from Python.  Accepts only a single argument, which
        must be a function or other callable object that can be passed to the
        `inspect.signature()` factory function.  That includes user-defined types with
        overloaded call operators, as long as the operator is properly annotated
        according to Python style, or the object provides a `__signature__` property
        that returns a valid `inspect.Signature` object.  This method can be used as a
        decorator from Python. */
        static PyObject* overload(PyFunction* self, PyObject* func) noexcept {
            try {
                Object obj = reinterpret_borrow<Object>(func);
                impl::Inspect signature(obj, Sig::seed, Sig::prime);
                if (!issubclass<typename Sig::Return>(signature.returns())) {
                    std::string message =
                        "overload return type '" + repr(signature.returns()) +
                        "' is not a subclass of " +
                        repr(Type<typename Sig::Return>());
                    PyErr_SetString(PyExc_TypeError, message.c_str());
                    return nullptr;
                }
                self->overloads.insert(signature.key(), obj);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Remove an overload from this function.  Throws a KeyError if the function
        is not found. */
        static PyObject* remove(PyFunction* self, PyObject* func) noexcept {
            try {
                self->overloads.remove(reinterpret_borrow<Object>(func));
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload trie from Python. */
        static PyObject* clear(PyFunction* self) noexcept {
            try {
                self->overloads.clear();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload cache from Python. */
        static PyObject* flush(PyFunction* self) noexcept {
            try {
                self->overloads.flush();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __getitem__(PyFunction* self, PyObject* specifier) noexcept {
            try {
                if (PyTuple_Check(specifier)) {
                    Py_INCREF(specifier);
                } else {
                    specifier = PyTuple_Pack(1, specifier);
                    if (specifier == nullptr) {
                        return nullptr;
                    }
                }
                auto key = subscript_key(
                    reinterpret_borrow<Object>(specifier)
                );
                std::optional<PyObject*> func = self->overloads.get_subclass(key);
                PyObject* value = func.value_or(Py_None);
                return Py_NewRef(value ? value : self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            try {
                if (value) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "functions do not support item assignment: use "
                        "`@func.overload` to register an overload instead"
                    );
                    return -1;
                }
                if (PyTuple_Check(specifier)) {
                    Py_INCREF(specifier);
                } else {
                    specifier = PyTuple_Pack(1, specifier);
                    if (specifier == nullptr) {
                        return -1;
                    }
                }
                auto key = subscript_key(
                    reinterpret_borrow<Object>(specifier)
                );
                Object func = reinterpret_borrow<Object>(
                    self->overloads.search_subclass(key)
                );
                if (func.is(nullptr)) {
                    PyErr_SetString(
                        PyExc_ValueError,
                        "cannot delete a function's base overload"
                    );
                    return -1;
                }
                self->overloads.remove(func);
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static int __bool__(PyFunction* self) noexcept {
            /// NOTE: `bool()` typically forwards to `len()`, which would cause
            /// functions to erroneously evaluate to false in some circumstances.
            return true;
        }

        static Py_ssize_t __len__(PyFunction* self) noexcept {
            return self->overloads.data.size();
        }

        static PyObject* __iter__(PyFunction* self) noexcept {
            try {
                return release(Iterator(
                    self->overloads.data | std::views::transform(
                        [](const Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            try {
                for (const auto& data : self->overloads.data) {
                    if (ptr(data.func) == func) {
                        return 1;
                    }
                }
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* Attach a function to a type as an instance method descriptor.  Accepts the
        type to attach to, which can be provided by calling this method as a decorator
        from Python. */
        static PyObject* method(PyFunction* self, void*) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    ArgTraits<typename Sig::template at<0>>::pos() ||
                    ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    impl::Method* descr = reinterpret_cast<impl::Method*>(
                        impl::Method::__type__.tp_alloc(
                            &impl::Method::__type__,
                            0
                        )
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) impl::Method(reinterpret_borrow<Object>(self));
                    } catch (...) {
                        Py_DECREF(descr);
                        throw;
                    }
                    return descr;
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a class method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* classmethod(PyFunction* self, void*) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    ArgTraits<typename Sig::template at<0>>::pos() ||
                    ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    impl::ClassMethod* descr = reinterpret_cast<impl::ClassMethod*>(
                        impl::ClassMethod::__type__.tp_alloc(
                            &impl::ClassMethod::__type__,
                            0
                        )
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) impl::ClassMethod(reinterpret_borrow<Object>(self));
                    } catch (...) {
                        Py_DECREF(descr);
                        throw;
                    }
                    return descr;
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a static method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* staticmethod(PyFunction* self, void*) noexcept {
            try {
                impl::StaticMethod* descr = reinterpret_cast<impl::StaticMethod*>(
                    impl::StaticMethod::__type__.tp_alloc(&impl::StaticMethod::__type__, 0)
                );
                if (descr == nullptr) {
                    return nullptr;
                }
                try {
                    new (descr) impl::StaticMethod(reinterpret_borrow<Object>(self));
                } catch (...) {
                    Py_DECREF(descr);
                    throw;
                }
                return descr;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: .property needs to be converted into a getset descriptor that
        /// returns an unbound descriptor object.  The special binding logic is thus
        /// implemented in the descriptor's call operator.

        /* Attach a function to a type as a getset descriptor.  Accepts a type object
        to attach to, which can be provided by calling this method as a decorator from
        Python, as well as two keyword-only arguments for an optional setter and
        deleter.  The same getter/setter fields are available from the descriptor
        itself via traditional Python `@Type.property.setter` and
        `@Type.property.deleter` decorators. */
        static PyObject* property(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    ArgTraits<typename Sig::template at<0>>::pos() ||
                    ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = ArgTraits<typename Sig::template at<0>>::type;
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    PyObject* cls;
                    if (nargs == 0) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() requires a type object as the sole "
                            "positional argument",
                            self->name
                        );
                        return nullptr;
                    } else if (nargs == 1) {
                        cls = args[0];
                    } else {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() takes exactly one positional "
                            "argument",
                            self->name
                        );
                        return nullptr;
                    }
                    if (!PyType_Check(cls)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "expected a type object, not: %R",
                            cls
                        );
                        return nullptr;
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "class must be a must be a subclass of %R",
                            ptr(Type<T>())
                        );
                        return nullptr;
                    }

                    PyObject* fset = nullptr;
                    PyObject* fdel = nullptr;
                    if (kwnames) {
                        Py_ssize_t kwcount = PyTuple_GET_SIZE(kwnames);
                        if (kwcount > 2) {
                            PyErr_SetString(
                                PyExc_TypeError,
                                "property() takes at most 2 keyword arguments"
                            );
                            return nullptr;
                        } else if (kwcount > 1) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                            key = PyTuple_GET_ITEM(kwnames, 1);
                            rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"deleter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fdel = args[2];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"setter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fset = args[2];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        } else if (kwcount > 0) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        }
                    }
                    /// TODO: validate fset and fdel are callable with the expected
                    /// signatures -> This can be done with the Inspect() helper, which
                    /// will extract all overload keys from the function.  I just have
                    /// to confirm that at least one path through the overload trie
                    /// matches the expected signature.

                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    using Property = impl::Property;
                    Property* descr = reinterpret_cast<Property*>(
                        Property::__type__.tp_alloc(&Property::__type__, 0)
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) Property(cls, self, fset, fdel);
                    } catch (...) {
                        Py_DECREF(descr);
                        Exception::to_python();
                        return nullptr;
                    }
                    int rc = PyObject_SetAttr(cls, self->name, descr);
                    Py_DECREF(descr);
                    if (rc) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Implement the descriptor protocol to generate bound member functions.  Note
        that due to the Py_TPFLAGS_METHOD_DESCRIPTOR flag, this will not be called when
        invoking the function as a method during normal use.  It's only used when the
        method is accessed via the `.` operator and not immediately called. */
        static PyObject* __get__(
            PyFunction* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            try {
                PyObject* cls = reinterpret_cast<PyObject*>(Py_TYPE(self));

                // get the current function's template key and allocate a copy
                Object unbound_key = reinterpret_steal<Object>(PyObject_GetAttr(
                    cls,
                    ptr(impl::template_string<"__template__">())
                ));
                if (unbound_key.is(nullptr)) {
                    return nullptr;
                }
                Py_ssize_t len = PyTuple_GET_SIZE(ptr(unbound_key));
                Object bound_key = reinterpret_steal<Object>(
                    PyTuple_New(len - 1)
                );
                if (bound_key.is(nullptr)) {
                    return nullptr;
                }

                // the first element encodes the unbound function's return type.  All
                // we need to do is replace the first index of the slice with the new
                // type and exclude the first argument from the unbound key
                Object slice = reinterpret_steal<Object>(PySlice_New(
                    type == Py_None ?
                        reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
                    Py_None,
                    reinterpret_cast<PySliceObject*>(
                        PyTuple_GET_ITEM(ptr(unbound_key), 0)
                    )->step
                ));
                if (slice.is(nullptr)) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(ptr(bound_key), 0, release(slice));
                for (size_t i = 2; i < len; ++i) {  // skip return type and first arg
                    PyTuple_SET_ITEM(
                        ptr(bound_key),
                        i - 1,
                        Py_NewRef(PyTuple_GET_ITEM(ptr(unbound_key), i))
                    );
                }

                // once the new key is built, we can index the unbound function type to
                // get the corresponding Python class for the bound function
                Object bound_type = reinterpret_steal<Object>(PyObject_GetItem(
                    cls,
                    ptr(bound_key)
                ));
                if (bound_type.is(nullptr)) {
                    return nullptr;
                }
                PyObject* args[] = {ptr(bound_type), self, obj};
                return PyObject_VectorcallMethod(
                    ptr(impl::template_string<"_capture">()),
                    args,
                    3,
                    nullptr
                );

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<Function>()))
                )) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<__python__*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<Function>()))
                )) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<__python__*>(lhs)->structural_type()),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->structural_type())
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __instancecheck__(PyFunction* self, PyObject* obj) noexcept {
            try {
                int rc = PyObject_IsInstance(
                    obj,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __subclasscheck__(PyFunction* self, PyObject* cls) noexcept {
            try {
                int rc = PyObject_IsSubclass(
                    cls,
                    ptr(self->structural_type())
                );
                if (rc < 0) {
                    return nullptr;
                }
                return PyBool_FromLong(rc);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __name__(PyFunction* self, void*) noexcept {
            return Py_NewRef(ptr(self->name));
        }

        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            if (!self->pysignature.is(None)) {
                return Py_NewRef(ptr(self->pysignature));
            }

            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }

                // if this function captures a Python function, forward to it
                if (!(self->pyfunc.is(None))) {
                    return PyObject_CallOneArg(
                        ptr(getattr<"signature">(inspect)),
                        ptr(self->pyfunc)
                    );
                }

                // otherwise, we need to build a signature object ourselves
                Object Signature = getattr<"Signature">(inspect);
                Object Parameter = getattr<"Parameter">(inspect);

                // build the parameter annotations
                Object tuple = reinterpret_steal<Object>(PyTuple_New(Sig::n));
                if (tuple.is(nullptr)) {
                    return nullptr;
                }
                []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyObject* tuple,
                    PyFunction* self,
                    const Object& Parameter
                ) {
                    (PyTuple_SET_ITEM(  // steals a reference
                        tuple,
                        Is,
                        release(build_parameter<Is>(self, Parameter))
                    ), ...);
                }(
                    std::make_index_sequence<Sig::n>{},
                    ptr(tuple),
                    self,
                    Parameter
                );

                // get the return annotation
                Type<typename Sig::Return> return_type;

                // create the signature object
                return release(Signature(tuple, arg<"return_annotation"_> = return_type));

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                std::string str = "<" + type_name<Function<F>> + " at " +
                    std::to_string(reinterpret_cast<size_t>(self)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* Implements the Python constructor without any safety checks. */
        /// TODO: not sure if this is strictly necessary?  I think it's called from bound
        /// methods to accelerate them?

        static PyObject* _self_type(PyFunction* self, void*) noexcept {
            if constexpr (Sig::n == 0 || !(Sig::has_pos || Sig::has_args)) {
                Py_RETURN_NONE;
            } else {
                using T = ArgTraits<typename Sig::template at<0>>::type;
                return release(Type<T>());
            }
        }

        static PyObject* _return_type(PyFunction* self, void*) noexcept {
            if constexpr (std::is_void_v<typename Sig::Return>) {
                Py_RETURN_NONE;
            } else {
                return release(Type<typename Sig::Return>());
            }
        }

        static PyObject* _bind_method(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            using T = ArgTraits<typename Sig::template at<0>>::type;
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_method() requires exactly two positional arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "method() requires a type object"
                );
                return nullptr;
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class must be a must be a subclass of %R",
                    ptr(Type<T>())
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::Method::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_method() requires a Bertrand method descriptor as "
                    "the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        static PyObject* _bind_classmethod(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            using T = ArgTraits<typename Sig::template at<0>>::type;
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_classmethod() requires exactly two positional arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "classmethod() requires a type object"
                );
                return nullptr;
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class must be a must be a subclass of %R",
                    ptr(Type<T>())
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::ClassMethod::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_classmethod() requires a Bertrand classmethod "
                    "descriptor as the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        static PyObject* _bind_staticmethod(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf
        ) noexcept {
            size_t nargs = PyVectorcall_NARGS(nargsf);
            if (nargs != 2) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_staticmethod() requires exactly two positional "
                    "arguments"
                );
                return nullptr;
            }
            PyObject* cls = args[0];
            if (!PyType_Check(cls)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "staticmethod() requires a type object"
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            PyObject* descr = args[1];
            if (!PyType_IsSubtype(Py_TYPE(descr), &impl::StaticMethod::__type__)) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_staticmethod() requires a Bertrand classmethod "
                    "descriptor as the second argument"
                );
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        /// TODO: bind_property?

        static PyObject* _subtrie_len(PyFunction* self, PyObject* value) noexcept {
            try {
                size_t len = 0;
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(value)
                ) {
                    ++len;
                }
                return PyLong_FromSize_t(len);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_iter(PyFunction* self, PyObject* value) noexcept {
            try {
                return release(Iterator(
                    self->overloads.match(value) | std::views::transform(
                        [](const typename Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_contains(
            PyFunction* self,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            try {
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(args[0])
                ) {
                    if (data.func == args[1]) {
                        Py_RETURN_TRUE;
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: all this constructor crap also has to be reflected for bound methods.

        static PyObject* validate_signature(PyObject* func, const impl::Inspect& signature) {
            // ensure at least one possible return type exactly matches the
            // expected template signature
            Object rtype = std::is_void_v<typename Sig::Return> ?
                reinterpret_borrow<Object>(Py_None) :
                Object(Type<typename Sig::Return>());
            bool match = false;
            for (PyObject* returns : signature.returns()) {
                if (rtype.is(returns)) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    "base function must return " + repr(rtype) + ", not: '" +
                    repr(reinterpret_borrow<Object>(signature.returns()[0])) +
                    "'"
                );
            }

            // ensure at least one complete parameter list exactly matches the
            // expected template signature
            constexpr auto validate = []<size_t... Is>(
                std::index_sequence<Is...>,
                impl::Inspect& signature,
                const auto& key
            ) {
                return (validate_parameter<Is>(key[Is]) && ...);
            };
            match = false;
            for (const auto& key : signature) {
                if (
                    key.size() == Sig::n &&
                    validate(std::make_index_sequence<Sig::n>{}, signature, key)
                ) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    /// TODO: improve this error message by printing out the
                    /// expected signature.  Maybe I can just get the repr of the
                    /// current function type?
                    "no match for parameter list"
                );
            }

            // extract default values from the signature
            return []<size_t... Js>(std::index_sequence<Js...>, impl::Inspect& sig) {
                return typename Sig::Defaults{extract_default<Js>(sig)...};
            }(std::make_index_sequence<Sig::n_opt>{}, signature);
        }

        template <size_t I>
        static bool validate_parameter(const Param& param) {
            using T = Sig::template at<I>;
            return (
                param.name == ArgTraits<T>::name &&
                param.kind == ArgTraits<T>::kind &&
                param.value == ptr(Type<typename ArgTraits<T>::type>())
            );
        }

        template <size_t J>
        static Object extract_default(impl::Inspect& signature) {
            Object default_value = getattr<"default">(
                signature.at(Sig::Defaults::template rfind<J>)
            );
            if (default_value.is(getattr<"empty">(signature.signature))) {
                throw TypeError(
                    "missing default value for parameter '" +
                    ArgTraits<typename Sig::Defaults::template at<J>>::name + "'"
                );
            }
            return default_value;
        }

        static Params<std::vector<Param>> subscript_key(
            const Object& specifier
        ) {
            size_t hash = 0;
            Py_ssize_t size = PyTuple_GET_SIZE(ptr(specifier));
            std::vector<Param> key;
            key.reserve(size);

            std::unordered_set<std::string_view> names;
            Py_ssize_t kw_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* item = PyTuple_GET_ITEM(ptr(specifier), i);

                // slices represent keyword arguments
                if (PySlice_Check(item)) {
                    PySliceObject* slice = reinterpret_cast<PySliceObject*>(item);
                    if (!PyUnicode_Check(slice->start)) {
                        throw TypeError(
                            "expected a keyword argument name as first "
                            "element of slice, not " + repr(
                                reinterpret_borrow<Object>(slice->start)
                            )
                        );
                    }
                    std::string_view name = impl::get_parameter_name(slice->start);
                    if (names.contains(name)) {
                        throw TypeError(
                            "duplicate keyword argument: " + std::string(name)
                        );
                    }
                    if (!PyType_Check(slice->stop)) {
                        throw TypeError(
                            "expected a type as second element of slice, not " +
                            repr(reinterpret_borrow<Object>(slice->stop))
                        );
                    }
                    if (slice->step != Py_None) {
                        throw TypeError(
                            "keyword argument cannot have a third slice element: " +
                            repr(reinterpret_borrow<Object>(slice->step))
                        );
                    }
                    key.emplace_back(
                        name,
                        reinterpret_borrow<Object>(slice->stop),
                        impl::ArgKind::KW
                    );
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                    kw_idx = i;
                    names.insert(name);

                // all other objects are positional arguments
                } else {
                    if (i > kw_idx) {
                        throw TypeError(
                            "positional argument follows keyword argument"
                        );
                    }
                    if (!PyType_Check(item)) {
                        throw TypeError(
                            "expected a type object, not " +
                            repr(reinterpret_borrow<Object>(item))
                        );
                    }
                    key.emplace_back(
                        "",
                        reinterpret_borrow<Object>(item),
                        impl::ArgKind::POS
                    );
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                }
            }

            return {std::move(key), hash};
        }

        Object structural_type() const {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(impl::template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object cls = reinterpret_steal<Object>(_self_type(*this, nullptr));
            if (cls.is(None)) {
                throw TypeError("function must accept at least one positional argument");
            }
            Object key = getattr<"__template_key__">(cls);
            Py_ssize_t len = PyTuple_GET_SIZE(ptr(key));
            Object new_key = reinterpret_steal<Object>(PyTuple_New(len - 1));
            if (new_key.is(nullptr)) {
                Exception::from_python();
            }
            Object rtype = reinterpret_steal<Object>(PySlice_New(
                ptr(cls),
                Py_None,
                reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(key), 0)
                )->step
            ));
            if (rtype.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(new_key), 0, release(rtype));
            for (Py_ssize_t i = 2; i < len; ++i) {
                PyTuple_SET_ITEM(
                    ptr(new_key),
                    i - 1,
                    Py_NewRef(PyTuple_GET_ITEM(ptr(key), i))
                );
            }
            Object specialization = reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(Py_Type(ptr(func)))
            )[new_key];
            Object result = reinterpret_steal<Object>(PySlice_New(
                ptr(name),
                ptr(specialization),
                Py_None
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return getattr<"Intersection">(bertrand)[result];
        }

        template <size_t I>
        static Object build_parameter(PyFunction* self, const Object& Parameter) {
            using T = Sig::template at<I>;
            using Traits = ArgTraits<T>;

            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    Traits::name,
                    Traits::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }

            Object kind;
            if constexpr (Traits::kwonly()) {
                kind = getattr<"KEYWORD_ONLY">(Parameter);
            } else if constexpr (Traits::kw()) {
                kind = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            } else if constexpr (Traits::pos()) {
                kind = getattr<"POSITIONAL_ONLY">(Parameter);
            } else if constexpr (Traits::args()) {
                kind = getattr<"VAR_POSITIONAL">(Parameter);
            } else if constexpr (Traits::kwargs()) {
                kind = getattr<"VAR_KEYWORD">(Parameter);
            } else {
                throw TypeError("unrecognized argument kind");
            }

            Object default_value = self->defaults.template get<I>();
            Type<typename Traits::type> annotation;

            PyObject* args[] = {
                nullptr,
                ptr(name),
                ptr(kind),
                ptr(default_value),
                ptr(annotation),
            };
            Object kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(4,
                    ptr(impl::template_string<"name">()),
                    ptr(impl::template_string<"kind">()),
                    ptr(impl::template_string<"default">()),
                    ptr(impl::template_string<"annotation">())
                )
            );
            Object result = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(Parameter),
                args + 1,
                0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(kwnames)
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(&__bool__),
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "overload",
                reinterpret_cast<PyCFunction>(&overload),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "clear",
                reinterpret_cast<PyCFunction>(&clear),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "flush",
                reinterpret_cast<PyCFunction>(&flush),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "method",
                reinterpret_cast<PyCFunction>(&method),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "_bind_method",
                reinterpret_cast<PyCFunction>(&_bind_method),
                METH_FASTCALL,
                nullptr
            },
            {
                "_bind_classmethod",
                reinterpret_cast<PyCFunction>(&_bind_classmethod),
                METH_FASTCALL,
                nullptr
            },
            {
                "_bind_staticmethod",
                reinterpret_cast<PyCFunction>(&_bind_staticmethod),
                METH_FASTCALL,
                nullptr
            },
            {
                "_subtrie_len",
                reinterpret_cast<PyCFunction>(&_subtrie_len),
                METH_O,
                nullptr
            },
            {
                "_subtrie_iter",
                reinterpret_cast<PyCFunction>(&_subtrie_iter),
                METH_O,
                nullptr
            },
            {
                "_subtrie_contains",
                reinterpret_cast<PyCFunction>(&_subtrie_contains),
                METH_FASTCALL,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "method",
                reinterpret_cast<getter>(&method),
                nullptr,
                PyDoc_STR(
R"doc()doc"
                ),
                nullptr
            },
            {
                "classmethod",
                reinterpret_cast<getter>(&classmethod),
                nullptr,
                PyDoc_STR(
R"doc(Returns a classmethod descriptor for this function.

Returns
-------
classmethod
    A classmethod descriptor that binds the function to a type.

Raises
------
TypeError
    If the function does not accept at least one positional argument which can
    be interpreted as a type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "staticmethod",
                reinterpret_cast<getter>(&staticmethod),
                nullptr,
                PyDoc_STR(
R"doc(Returns a staticmethod descriptor for this function.

Returns
-------
staticmethod
    A staticmethod descriptor that binds the function to a type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "property",
                reinterpret_cast<getter>(&property),
                nullptr,
                PyDoc_STR(
R"doc(Returns a property descriptor that uses this function as a getter.

Returns
-------
property
    A property descriptor that binds the function to a type.

Raises
------
TypeError
    If the function does not accept exactly one positional argument which can
    be bound to the given type.

Notes
-----
The returned descriptor implements a call operator that attaches it to a type,
enabling this property to be called like a normal method/decorator.  The
unbound descriptor provides a convenient place to implement the `&` and `|`
operators for structural typing.)doc"
                ),
                nullptr
            },
            {
                "__signature__",
                reinterpret_cast<getter>(&__signature__),
                nullptr,
                PyDoc_STR(
R"doc(A property that produces an accurate `inspect.Signature` object when a
C++ function is introspected from Python.

Returns
-------
inspect.Signature
    A signature object that describes the function's expected arguments and
    return value.

Notes
-----
Providing this descriptor allows the `inspect` module to be used on C++
functions as if they were implemented in Python itself, reflecting the signature
of their underlying `py::Function` representation.)doc"
                ),
                nullptr
            },
            {
                "_self_type",
                reinterpret_cast<getter>(&_self_type),
                nullptr,
                nullptr,
                nullptr
            },
            {
                "_return_type",
                reinterpret_cast<getter>(&_return_type),
                nullptr,
                nullptr,
                nullptr
            },
            {nullptr}
        };
    };

    /* Bound member function type.  Must be constructed with a corresponding `self`
    parameter, which will be inserted as the first argument to a call according to
    Python style. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr static_str __doc__ =
R"doc(A bound member function descriptor.

Notes
-----
This type is equivalent to Python's internal `types.MethodType`, which
describes the return value of a method descriptor when accessed from an
instance of an enclosing class.  The only difference is that this type is
implemented in C++, and thus has a unique instantiation for each signature.

Additionally, it must be noted that instances of this type must be constructed
with an appropriate `self` parameter, which is inserted as the first argument
to the underlying C++/Python function when called, according to Python style.
As such, it is not possible for an instance of this type to represent an
unbound function object; those are always represented as a non-member function
type instead.  By templating `py::Function<...>` on a member function pointer,
you are directly indicating the presence of the bound `self` parameter, in a
way that encodes this information into the type systems of both languages
simultaneously.

In essence, all this type does is hold a reference to both an equivalent
non-member function, as well as a reference to the `self` object that the
function is bound to.  All operations will be simply forwarded to the
underlying non-member function, including overloads, introspection, and so on,
but with the `self` argument already accounted for.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc call = reinterpret_cast<vectorcallfunc>(__call__);
        PyObject* __wrapped__;
        PyObject* __self__;

        explicit PyFunction(PyObject* __wrapped__, PyObject* __self__) noexcept :
            __wrapped__(Py_NewRef(__wrapped__)), __self__(Py_NewRef(__self__))
        {}

        ~PyFunction() noexcept {
            Py_XDECREF(__wrapped__);
            Py_XDECREF(__self__);
        }

        static void __dealloc__(PyFunction* self) noexcept {
            PyObject_GC_UnTrack(self);
            self->~PyFunction();
            Py_TYPE(self)->tp_free(self);
        }

        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
                if (self == nullptr) {
                    return nullptr;
                }
                self->call = reinterpret_cast<vectorcallfunc>(__call__);
                self->__wrapped__ = nullptr;
                self->__self__ = nullptr;
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs != 2 || kwds != nullptr) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "expected exactly 2 positional-only arguments, but "
                        "received %zd",
                        nargs
                    );
                    return -1;
                }
                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                /// TODO: do everything from the unbound constructor, but also ensure
                /// that the self argument matches the expected type.
                /// -> NOTE: this must assert that the function being passed in has a
                /// `__self__` attribute that matches the expected type, which is true
                /// for both Python bound methods and my own bound methods.

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /// TODO: I'll need a Python-level __init__/__new__ method that
        /// constructs a new instance of this type, which will be called
        /// when the descriptor is accessed.

        template <static_str ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// NOTE: Python includes an optimization of the vectorcall protocol
                /// for bound functions that can temporarily forward the correct `self`
                /// argument without reallocating the underlying array, which we can
                /// take advantage of if possible.
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET) {
                    PyObject** arr = const_cast<PyObject**>(args) - 1;
                    PyObject* temp = arr[0];
                    arr[0] = self->__self__;
                    PyObject* result = PyObject_Vectorcall(
                        self->__wrapped__,
                        arr,
                        nargs + 1,
                        kwnames
                    );
                    arr[0] = temp;
                    return result;
                }

                /// otherwise, we have to heap allocate a new array and copy the arguments
                size_t n = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0);
                PyObject** arr = new PyObject*[n + 1];
                arr[0] = self->__self__;
                for (size_t i = 0; i < n; ++i) {
                    arr[i + 1] = args[i];
                }
                PyObject* result = PyObject_Vectorcall(
                    self->__wrapped__,
                    arr,
                    nargs + 1,
                    kwnames
                );
                delete[] arr;
                return result;

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static Py_ssize_t __len__(PyFunction* self) noexcept {
            PyObject* result = PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_len">()),
                self->__self__
            );
            if (result == nullptr) {
                return -1;
            }
            Py_ssize_t len = PyLong_AsSsize_t(result);
            Py_DECREF(result);
            return len;
        }

        /* Subscripting a bound method will forward to the unbound method, prepending
        the key with the `self` argument. */
        static PyObject* __getitem__(
            PyFunction* self,
            PyObject* specifier
        ) noexcept {
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return nullptr;
                }
            }
            PyObject* result = PyObject_GetItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        /* Deleting an overload from a bound method will forward the deletion to the
        unbound method, prepending the key with the `self` argument. */
        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return -1;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return -1;
                }
            }
            int result = PyObject_DelItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            PyObject* args[] = {
                self->__wrapped__,
                self->__self__,
                func
            };
            PyObject* result = PyObject_VectorcallMethod(
                ptr(impl::template_string<"_subtrie_contains">()),
                args,
                3 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                nullptr
            );
            if (result == nullptr) {
                return -1;
            }
            int contains = PyObject_IsTrue(result);
            Py_DECREF(result);
            return contains;
        }

        static PyObject* __iter__(PyFunction* self) noexcept {
            return PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_iter">()),
                self->__self__
            );
        }

        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }
                Object signature = PyObject_CallOneArg(
                    ptr(getattr<"signature">(inspect)),
                    self->__wrapped__
                );
                if (signature.is(nullptr)) {
                    return nullptr;
                }
                Object values = getattr<"values">(
                    getattr<"parameters">(signature)
                );
                size_t size = len(values);
                Object parameters = reinterpret_steal<Object>(
                    PyTuple_New(size - 1)
                );
                if (parameters.is(nullptr)) {
                    return nullptr;
                }
                auto it = begin(values);
                auto stop = end(values);
                ++it;
                for (size_t i = 0; it != stop; ++it, ++i) {
                    PyTuple_SET_ITEM(
                        ptr(parameters),
                        i,
                        Py_NewRef(ptr(*it))
                    );
                }
                PyObject* args[] = {nullptr, ptr(parameters)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, ptr(impl::template_string<"parameters">()))
                );
                return PyObject_Vectorcall(
                    ptr(getattr<"replace">(signature)),
                    args + 1,
                    0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Default `repr()` reflects Python conventions for bound methods. */
        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                std::string str =
                    "<bound method " +
                    demangle(Py_TYPE(self->__self__)->tp_name) + ".";
                Py_ssize_t len;
                const char* name = PyUnicode_AsUTF8AndSize(
                    self->__wrapped__->name,
                    &len
                );
                if (name == nullptr) {
                    return nullptr;
                }
                str += std::string(name, len) + " of ";
                str += repr(reinterpret_borrow<Object>(self->__self__)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* A private, class-level constructor called internally by the descriptor
        protocol to avoid any superfluous argument validation when binding methods. */
        static PyObject* _capture(
            PyTypeObject* cls,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            PyObject* result = cls->tp_alloc(cls, 0);
            if (result == nullptr) {
                return nullptr;
            }
            try {
                new (result) PyFunction(args[0], args[1]);
            } catch (...) {
                Py_DECREF(result);
                Exception::to_python();
                return nullptr;
            }
            PyObject_GC_Track(result);
            return result;
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&impl::FuncIntersect::__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&impl::FuncUnion::__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "_capture",
                reinterpret_cast<PyCFunction>(&_capture),
                METH_CLASS | METH_FASTCALL,
                nullptr
            },
            {nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                "__signature__",
                reinterpret_cast<getter>(&__signature__),
                nullptr,
                PyDoc_STR(
R"doc(A property that produces an accurate `inspect.Signature` object when a
C++ function is introspected from Python.

Notes
-----
Providing this descriptor allows the `inspect` module to be used on C++
functions as if they were implemented in Python itself, reflecting the signature
of their underlying `py::Function` representation.)doc"
                ),
                nullptr
            },
            {nullptr}
        };

    };

public:
    using __python__ = PyFunction<impl::Signature<F>>;

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Function> requires (__initializer__<T>::enable)
    Function(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

    template <typename... A> requires (implicit_ctor<Function>::template enable<A...>)
    Function(A&&... args) : Object(
        implicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

    template <typename... A> requires (explicit_ctor<Function>::template enable<A...>)
    explicit Function(A&&... args) : Object(
        explicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

};


/// TODO: I would also need some way to disambiguate static functions from member
/// functions when doing CTAD.  This is probably accomplished by providing an extra
/// argument to the constructor which holds the `self` value, and is implicitly
/// convertible to the function's first parameter type.  In that case, the CTAD
/// guide would always deduce to a member function over a static function.  If the
/// extra argument is given and is not convertible to the first parameter type, then
/// we issue a compile error, and if the extra argument is not given at all, then we
/// interpret it as a static function.
/// -> This can be done by specializing the CTAD guides such that if exactly one
/// partial argument is given, the function type deduces to a member function?
/// -> Actually, with the partial binding apparatus, it *might* be possible to
/// eliminate member function types entirely, although I'm not sure if that's
/// appropriate everywhere.  If possible, though, then it would cut down on the
/// number of types I need to generate, bring the template signature into line with
/// `std::function`, and potentially simplify the implementation.

/// TODO: alternatively, I could generalize the member function syntax to account for
/// all pre-bound partial arguments.  So:

/*
    Function<Int(*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a static function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        }
    );

    Function<Int(Int::*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a member function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1
    );

    Function<Int(pack<Int, Int>::*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a simple example function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1,
        2
    );
*/

/// That ensures that no information is lost, and is fully generalizable, but it does
/// restrict conversions a bit.

/// TODO: an alternative is to use the partial mechanism to remove arguments from the
/// signature, rather than further encoding them.

/*
    Function<Int(*)(Arg<"x", Int>, Arg<"y", Int>::opt)> func(
        "subtract",
        "a static function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        }
    );

    Function<Int(*)(Arg<"y", Int>::opt)> func(
        "subtract",
        "a member function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1
    );

    Function<Int(*)()> func(
        "subtract",
        "a simple example function",
        { arg<"y"> = 2 },
        [](Arg<"x", Int> x, Arg<"y", Int>::opt y) {
            return *x - *y;
        },
        1,
        2
    );
*/

/// That's probably better overall, and means the template signature always reflects
/// the actual function signature when the function is called, which is a plus.  It
/// also means I can potentially remove the function pointer type?

/// -> I can't do that because the internal `std::function` has to retain all of the
/// type information for how the function is called, so no arguments can be removed.
/// Instead, I need to go with either the first syntax or introduce a Bound<>
/// annotation that indicates that a parameter has already been bound to a given
/// argument.  Maybe that's another extension of `Arg<>`?




template <impl::inherits<impl::FunctionTag> F>
struct __template__<F> {
    using Func = std::remove_reference_t<F>;

    /* Functions use a special template syntax in Python to reflect C++ signatures as
     * symmetrically as as possible.  Here's an example:
     *
     *      Function[::int, "x": int, "y": int: ...]
     *
     * This describes a function which returns an integer and accepts two integer
     * arguments, `x` and `y`, the second of which is optional (indicated by ellipsis
     * following the type).  The first element describes the return type, as well as
     * the type of a possible `self` argument for member functions, with the following
     * syntax:
     *
     *      Function[Foo::int, "x": int, "y": int: ...]
     *
     * This describes the same function as before, but bound to class `Foo` as an
     * instance method.  Class methods are described by binding to `type[Foo]` instead,
     * and static methods use the same syntax as regular functions.  If the return
     * type is void, it can be replaced with `None`, which is the default for an empty
     * slice:
     *
     *      Function[::, "name": str]
     *
     * It is also possible to omit an argument name, in which case the argument will
     * be anonymous and positional-only:
     *
     *      Function[::int, int, int: ...]
     *
     * Trailing `...` syntax can still be used to mark an optional positional-only
     * argument.  Alternatively, a `"/"` delimiter can be used according to Python
     * syntax, in order to explicitly name positional-only arguments:
     *
     *      Function[::int, "x": int, "/", "y": int: ...]
     *
     * In this case, the `x` argument is positional-only, while `y` can be passed as
     * either a positional or keyword argument.  A `"*"` delimiter can be used to
     * separate positional-or-keyword arguments from keyword-only arguments:
     *
     *      Function[::int, "x": int, "*", "y": int: ...]
     *
     * Lastly, prepending `*` or `**` to an argument name will mark it as a variadic
     * positional or keyword argument, respectively:
     *
     *      Function[::int, "*args": int, "**kwargs": str]
     *
     * Such arguments cannot have default values.
     */

    template <size_t I, size_t PosOnly, size_t KwOnly>
    static void populate(PyObject* tuple, size_t& offset) {
        using T = Func::template at<I>;
        Type<typename ArgTraits<T>::type> type;

        /// NOTE: `/` and `*` argument delimiters must be inserted where necessary to
        /// model positional-only and keyword-only arguments correctly in Python.
        if constexpr (
            (I == PosOnly) ||
            ((I == Func::n - 1) && ArgTraits<T>::posonly())
        ) {
            PyObject* str = PyUnicode_FromStringAndSize("/", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;

        } else if constexpr (I == KwOnly) {
            PyObject* str = PyUnicode_FromStringAndSize("*", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;
        }

        if constexpr (ArgTraits<T>::posonly()) {
            if constexpr (ArgTraits<T>::name.empty()) {
                if constexpr (ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        Type<typename ArgTraits<T>::type>(),
                        Py_Ellipsis,
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyTuple_SET_ITEM(tuple, I + offset, ptr(type));
                }
            } else {
                Object name = reinterpret_steal<Object>(
                    PyUnicode_FromStringAndSize(
                        ArgTraits<T>::name,
                        ArgTraits<T>::name.size()
                    )
                );
                if (name.is(nullptr)) {
                    Exception::from_python();
                }
                if constexpr (ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_Ellipsis
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                }
            }

        } else if constexpr (ArgTraits<T>::kw()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    ArgTraits<T>::name,
                    ArgTraits<T>::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                ArgTraits<T>::opt() ? Py_Ellipsis : Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (ArgTraits<T>::args()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "*" + ArgTraits<T>::name,
                    ArgTraits<T>::name.size() + 1
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (ArgTraits<T>::kwargs()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "**" + ArgTraits<T>::name,
                    ArgTraits<T>::name.size() + 2
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else {
            static_assert(false, "unrecognized argument kind");
        }
    }

    static Object operator()() {
        Object result = reinterpret_steal<Object>(
            PyTuple_New(Func::n + 1 + Func::has_posonly + Func::has_kwonly)
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        Object rtype = std::is_void_v<typename Func::Return> ?
            Object(None) :
            Object(Type<typename ArgTraits<typename Func::Return>::type>());
        if constexpr (Func::has_self) {
            Object slice = reinterpret_steal<Object>(PySlice_New(
                Type<typename ArgTraits<typename Func::Self>::type>(),
                Py_None,
                ptr(rtype)
            ));
            if (slice.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, release(slice));
        } else {
            Object slice = reinterpret_steal<Object>(PySlice_New(
                Py_None,
                Py_None,
                ptr(rtype)
            ));
            if (slice.is(nullptr)) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, release(slice));
        }

        constexpr size_t transition = Func::has_posonly ? 
            std::min({Func::args_idx, Func::kw_idx, Func::kwargs_idx}) :
            Func::n;

        []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* list
        ) {
            size_t offset = 1;
            (populate<Is, transition, Func::kwonly_idx>(list, offset), ...);
        }(std::make_index_sequence<Func::n>{}, ptr(result));
        return result;
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





/// TODO: class methods can be indicated by a member method of Type<T>.  That
/// would allow this mechanism to scale arbitrarily.


// TODO: constructor should fail if the function type is a subclass of my root
// function type, but not a subclass of this specific function type.  This
// indicates a type mismatch in the function signature, which is a violation of
// static type safety.  I can then print a helpful error message with the demangled
// function types which can show their differences.
// -> This can be implemented in the actual call operator itself, but it would be
// better to implement it on the constructor side.  Perhaps including it in the
// isinstance()/issubclass() checks would be sufficient, since those are used
// during implicit conversion anyways.


/// TODO: all of these should be moved to their respective methods:
/// -   assert_matches() is needed in isinstance() + issubclass() to ensure
///     strict type safety.  Maybe also in the constructor, which can be
///     avoided using CTAD.
/// -   assert_satisfies() is needed in .overload()


struct TODO2 {

    template <size_t I, typename Container>
    static bool _matches(const Params<Container>& key) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;
        if (I < key.size()) {
            const Param& param = key[I];
            if constexpr (ArgTraits<at<I>>::kwonly()) {
                return (
                    (param.kwonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kw()) {
                return (
                    (param.kw() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::pos()) {
                return (
                    (param.posonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::args()) {
                return (
                    param.args() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                return (
                    param.kwargs() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (param.value == ptr(Type<T>()))
                );
            } else {
                static_assert(false, "unrecognized parameter kind");
            }
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_matches(const Params<Container>& key) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + static_str<>::from_int<I>
                );
            }
            const Param& param = key[I];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + static_str<>::from_int<I> + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + static_str<>::from_int<I>
                );
            }
            const Param& param = key[I];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + static_str<>::from_int<I> +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-only argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + static_str<>::from_int<I>
                );
            }
            const Param& param = key[I];
            if (!param.posonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' at index " + static_str<>::from_int<I> +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic positional argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + static_str<>::from_int<I>
                );
            }
            const Param& param = key[I];
            if (!param.args()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' at index " + static_str<>::from_int<I> +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + static_str<>::from_int<I>
                );
            }
            const Param& param = key[I];
            if (!param.kwargs()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + static_str<>::from_int<I> +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                param.value,
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

    template <size_t I, typename... Ts>
    static constexpr bool _satisfies() { return true; };
    template <size_t I, typename T, typename... Ts>
    static constexpr bool _satisfies() {
        if constexpr (ArgTraits<at<I>>::kwonly()) {
            return (
                (
                    ArgTraits<T>::kwonly() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            return (
                (
                    ArgTraits<T>::kw() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            return (
                (
                    ArgTraits<T>::pos() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if constexpr ((ArgTraits<T>::pos() || ArgTraits<T>::args())) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if constexpr (ArgTraits<T>::kw()) {
                if constexpr (
                    !has<ArgTraits<T>::name> &&
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            } else if constexpr (ArgTraits<T>::kwargs()) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else {
            static_assert(false, "unrecognized parameter type");
        }

        return false;
    }

    template <size_t I, typename Container>
    static bool _satisfies(const Params<Container>& key, size_t& idx) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        /// NOTE: if the original argument in the enclosing signature is required,
        /// then the new argument cannot be optional.  Otherwise, it can be either
        /// required or optional.

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kwonly() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kw() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.pos() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(param.value)))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];            
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw()) {
                    if (
                        /// TODO: check to see if the argument is present
                        // !callback(param->name) &&
                        !issubclass<T>(reinterpret_borrow<Object>(param->value))
                    ) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else {
            static_assert(false, "unrecognized parameter type");
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_satisfies(const Params<Container>& key, size_t& idx) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(idx) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.pos()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(param.value))) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(param.value)) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected variadic positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(param->value))) {
                        throw TypeError(
                            "expected variadic keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" +
                            repr(reinterpret_borrow<Object>(param->value)) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

    /* Check to see if a compile-time function signature exactly matches the
    enclosing parameter list. */
    template <typename... Params>
    static constexpr bool matches() {
        return (std::same_as<Params, Args> && ...);
    }

    /* Check to see if a dynamic function signature exactly matches the enclosing
    parameter list. */
    template <typename Container>
    static bool matches(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            return key.size() == n && (_matches<Is>(key) && ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Validate a dynamic function signature, raising an error if it does not
    exactly match the enclosing parameter list. */
    template <typename Container>
    static void assert_matches(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_matches<Is>(key), ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Check to see if a compile-time function signature can be bound to the
    enclosing parameter list, meaning that it could be registered as a viable
    overload. */
    template <typename... Params>
    static constexpr bool satisfies() {
        return _satisfies<0, Params...>();
    }

    /* Check to see if a dynamic function signature can be bound to the enclosing
    parameter list, meaning that it could be registered as a viable overload. */
    template <typename Container>
    static bool satisfies(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            return key.size() == n && (_satisfies<Is>(key, idx) && ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

    /* Validate a Python function signature, raising an error if it cannot be
    bound to the enclosing parameter list. */
    template <typename Container>
    static void assert_satisfies(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_satisfies<Is>(key, idx), ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

};




template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>>                 : Returns<bool> {
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

    // template <typename T>
    // concept is_callable_any = 
    //     std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
    //     std::is_member_function_pointer_v<std::decay_t<T>> ||
    //     has_call_operator<T>;


template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>>                 : Returns<bool> {
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


/* Call the function with the given arguments.  If the wrapped function is of the
coupled Python type, then this will be translated into a raw C++ call, bypassing
Python entirely. */
template <impl::inherits<impl::FunctionTag> Self, typename... Args>
    requires (std::remove_reference_t<Self>::bind<Args...>)
struct __call__<Self, Args...> : Returns<typename std::remove_reference_t<Self>::Return> {
    using Func = std::remove_reference_t<Self>;
    static Func::Return operator()(Self&& self, Args&&... args) {
        if (!self->overloads.data.empty()) {
            /// TODO: generate an overload key from the C++ arguments
            /// -> This can be implemented in Arguments<...>::Bind<...>::key()
            PyObject* overload = self->overloads.search(/* overload key */);
            if (overload) {
                return Func::call(overload, std::forward<Args>(args)...);
            }
        }
        return Func::call(self->defaults, self->func, std::forward<Args>(args)...);
    }
};


/// TODO: __getitem__, __contains__, __iter__, __len__, __bool__


template <typename F>
template <typename Self, typename Func>
    requires (
        !std::is_const_v<std::remove_reference_t<Self>> &&
        compatible<Func>
    )
void interface<Function<F>>::overload(this Self&& self, const Function<Func>& func) {
    /// TODO: C++ side of function overloading
}


template <typename F>
template <typename T>
void interface<Function<F>>::method(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void interface<Function<F>>::classmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void interface<Function<F>>::staticmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void interface<Function<F>>::property(
    this const auto& self,
    Type<T>& type,
    /* setter */,
    /* deleter */
) {
    /// TODO: C++ side of method binding
}


namespace impl {

    /* A convenience function that calls a named method of a Python object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <static_str Name, typename Self, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_method(Self&& self, Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(self),
            ptr(template_string<Name>())
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /* A convenience function that calls a named method of a Python type object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <typename Self, static_str Name, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(Self::type),
            ptr(template_string<Name>())
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /// NOTE: the type returned by `std::mem_fn()` is implementation-defined, so we
    /// have to do some template magic to trick the compiler into deducing the correct
    /// type during template specializations.

    template <typename T>
    struct respecialize { static constexpr bool enable = false; };
    template <template <typename...> typename T, typename... Ts>
    struct respecialize<T<Ts...>> {
        static constexpr bool enable = true;
        template <typename... New>
        using type = T<New...>;
    };
    template <typename Sig>
    using std_mem_fn_type = respecialize<
        decltype(std::mem_fn(std::declval<void(Object::*)()>()))
    >::template type<Sig>;

};


#define NON_MEMBER_FUNC(IN, OUT) \
    template <typename R, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define MEMBER_FUNC(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define STD_MEM_FN(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<impl::std_mem_fn_type<IN>> : Returns<Function<OUT>> {};


NON_MEMBER_FUNC(R(A...), R(*)(A...))
NON_MEMBER_FUNC(R(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&&, R(*)(A...))
MEMBER_FUNC(R(C::*)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)


#undef NON_MEMBER_FUNC
#undef MEMBER_FUNC
#undef STD_MEM_FN


}  // namespace bertrand


#endif
