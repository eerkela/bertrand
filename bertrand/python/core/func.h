#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "arg.h"
#include "iter.h"


namespace py {


namespace impl {

    /* Parse a C++ string that represents an argument name, throwing an error if it
    is invalid. */
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

    /* Parse a Python string that represents an argument name, throwing an error if
    it is invalid. */
    inline std::string_view get_parameter_name(PyObject* str) {
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(str, &len);
        if (data == nullptr) {
            Exception::from_python();
        }
        return get_parameter_name({data, static_cast<size_t>(len)});
    }

    /* A simple, trivially-destructible representation of a single parameter in a
    function signature or call site. */
    struct Param {
        std::string_view name;
        Object value;  // may be a type or instance
        ArgKind kind;

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
        size_t hash(size_t seed, size_t prime) const noexcept {
            return hash_combine(
                fnv1a(name.data(), seed, prime),
                reinterpret_cast<size_t>(ptr(value)),
                static_cast<size_t>(kind)
            );
        }
    };

    /* A read-only container of `Param` objects that also holds a combined hash
    suitable for cache optimization when searching a function's overload trie.  The
    underlying container type is flexible, and will generally be either a `std::array`
    (if the number of arguments is known ahead of time) or a `std::vector` (if they
    must be dynamic), but any container that supports read-only iteration, item access,
    and `size()` queries is supported. */
    template <yields<const Param&> T>
        requires (has_size<T> && lookup_yields<T, const Param&, size_t>)
    struct Params {
        T value;
        size_t hash = 0;

        const Param& operator[](size_t i) const noexcept { return value[i]; }
        size_t size() const noexcept { return std::ranges::size(value); }
        bool empty() const noexcept { return std::ranges::empty(value); }
        auto begin() const noexcept { return std::ranges::begin(value); }
        auto cbegin() const noexcept { return std::ranges::cbegin(value); }
        auto end() const noexcept { return std::ranges::end(value); }
        auto cend() const noexcept { return std::ranges::cend(value); }
    };

    /* Inspect an annotated Python function and extract its inline type hints so that
    they can be translated into corresponding parameter lists. */
    struct Inspect {
    private:

        static Object import_typing() {
            PyObject* typing = PyImport_Import(ptr(template_string<"typing">()));
            if (typing == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(typing);
        }

        static Object import_types() {
            PyObject* types = PyImport_Import(ptr(template_string<"types">()));
            if (types == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(types);
        }

        Object get_signature() const {
            // signature = inspect.signature(func)
            // hints = typing.get_type_hints(func)
            // signature = signature.replace(
            //      return_annotation=hints.get("return", inspect.Parameter.empty),
            //      parameters=[
            //         p if p.annotation is inspect.Parameter.empty else
            //         p.replace(annotation=hints[p.name])
            //         for p in signature.parameters.values()
            //     ]
            // )
            Object signature = reinterpret_steal<Object>(PyObject_CallOneArg(
                ptr(getattr<"signature">(inspect)),
                ptr(func)
            ));
            if (signature.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* get_type_hints_args[] = {nullptr, ptr(func), Py_True};
            Object get_type_hints_kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(1, ptr(template_string<"include_extras">()))
            );
            Object hints = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"get_type_hints">(typing)),
                get_type_hints_args + 1,
                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(get_type_hints_kwnames)
            ));
            if (hints.is(nullptr)) {
                Exception::from_python();
            }
            Object empty = getattr<"empty">(getattr<"Parameter">(inspect));
            Object parameters = reinterpret_steal<Object>(PyObject_CallMethodNoArgs(
                ptr(getattr<"parameters">(signature)),
                ptr(template_string<"values">())
            ));
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object new_params = reinterpret_steal<Object>(
                PyList_New(len)
            );
            Py_ssize_t idx = 0;
            for (Object param : parameters) {
                Object annotation = getattr<"annotation">(param);
                if (!annotation.is(empty)) {
                    annotation = reinterpret_steal<Object>(PyDict_GetItemWithError(
                        ptr(hints),
                        ptr(getattr<"name">(param))
                    ));
                    if (annotation.is(nullptr)) {
                        if (PyErr_Occurred()) {
                            Exception::from_python();
                        } else {
                            throw KeyError(
                                "no type hint for parameter: " + repr(param)
                            );
                        }
                    }
                    PyObject* replace_args[] = {nullptr, ptr(annotation)};
                    Object replace_kwnames = reinterpret_steal<Object>(
                        PyTuple_Pack(1, ptr(template_string<"annotation">()))
                    );
                    if (replace_kwnames.is(nullptr)) {
                        Exception::from_python();
                    }
                    param = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(getattr<"replace">(param)),
                        replace_args + 1,
                        0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        ptr(replace_kwnames)
                    ));
                    if (param.is(nullptr)) {
                        Exception::from_python();
                    }
                }
                // steals a reference
                PyList_SET_ITEM(ptr(new_params), idx++, release(param));
            }
            Object return_annotation = reinterpret_steal<Object>(PyDict_GetItem(
                ptr(hints),
                ptr(template_string<"return">())
            ));
            if (return_annotation.is(nullptr)) {
                return_annotation = empty;
            }
            PyObject* replace_args[] = {
                nullptr,
                ptr(return_annotation),
                ptr(new_params)
            };
            Object replace_kwnames = reinterpret_steal<Object>(PyTuple_Pack(
                2,
                ptr(template_string<"return_annotation">()),
                ptr(template_string<"parameters">())
            ));
            if (replace_kwnames.is(nullptr)) {
                Exception::from_python();
            }
            signature = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"replace">(signature)),
                replace_args + 1,
                0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(replace_kwnames)
            ));
            if (signature.is(nullptr)) {
                Exception::from_python();
            }
            return signature;
        }

        Object get_parameters() const {
            Object values = reinterpret_steal<Object>(PyObject_CallMethodNoArgs(
                ptr(getattr<"parameters">(signature)),
                ptr(template_string<"values">())
            ));
            if (values.is(nullptr)) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(
                PySequence_Tuple(ptr(values))
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        static Object to_union(std::set<Object>& keys, const Object& Union) {
            Object key = reinterpret_steal<Object>(
                PyTuple_New(keys.size())
            );
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            size_t i = 0;
            for (const Object& type : keys) {
                PyTuple_SET_ITEM(ptr(key), i++, Py_NewRef(ptr(type)));
            }
            Object specialization = reinterpret_steal<Object>(
                PyObject_GetItem(
                    ptr(Union),
                    ptr(key)
                )
            );
            if (specialization.is(nullptr)) {
                Exception::from_python();
            }
            return specialization;
        }

    public:
        Object bertrand = [] {
            PyObject* bertrand = PyImport_Import(
                ptr(template_string<"bertrand">())
            );
            if (bertrand == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(bertrand);
        }();
        Object inspect = [] {
            PyObject* inspect = PyImport_Import(ptr(template_string<"inspect">()));
            if (inspect == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(inspect);
        }();
        Object typing = import_typing();

        Object func;
        Object signature;
        Object parameters;
        size_t seed;
        size_t prime;

        Inspect(
            const Object& func,
            size_t seed,
            size_t prime
        ) : func(func),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(
            Object&& func,
            size_t seed,
            size_t prime
        ) : func(std::move(func)),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(const Inspect& other) = delete;
        Inspect(Inspect&& other) = delete;
        Inspect& operator=(const Inspect& other) = delete;
        Inspect& operator=(Inspect&& other) noexcept = delete;

        /* Get the `inspect.Parameter` object at a particular index of the introspected
        function signature. */
        Object at(size_t i) const {
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            } else if (i >= len) {
                throw IndexError("index out of range");
            }
            return reinterpret_borrow<Object>(
                PyTuple_GET_ITEM(ptr(parameters), i)
            );
        }

        /* A callback function to use when parsing inline type hints within a Python
        function declaration. */
        struct Callback {
            std::string id;
            std::function<bool(Object, std::set<Object>&)> func;
            bool operator()(Object hint, std::set<Object>& out) const {
                return func(hint, out);
            }
        };

        /* Initiate a search of the callback map in order to parse a Python-style type
        hint.  The search stops at the first callback that returns true, otherwise the
        hint is interpreted as either a single type if it is a Python class, or a
        generic `object` type otherwise. */
        static void parse(Object hint, std::set<Object>& out) {
            for (const Callback& cb : callbacks) {
                if (cb(hint, out)) {
                    return;
                }
            }

            // Annotated types are unwrapped and reprocessed if not handled by a callback
            Object typing = import_typing();
            Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                ptr(getattr<"get_origin">(typing)),
                ptr(hint)
            ));
            if (origin.is(nullptr)) {
                Exception::from_python();
            } else if (origin.is(getattr<"Annotated">(typing))) {
                Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                    ptr(getattr<"get_args">(typing)),
                    ptr(hint)
                ));
                if (args.is(nullptr)) {
                    Exception::from_python();
                }
                parse(reinterpret_borrow<Object>(
                    PyTuple_GET_ITEM(ptr(args), 0)
                ), out);
                return;
            }

            // unrecognized hints are assumed to implement `issubclass()`
            out.emplace(std::move(hint));
        }

        /* In order to provide custom handlers for Python type hints, each annotation
        will be passed through a series of callbacks that convert it into a flat list
        of Python types, which will be used to generate the final overload keys.

        Each callback is tested in order and expected to return true if it can handle
        the hint, in which case the search terminates and the final state of the `out`
        vector will be pushed into the set of possible overload keys.  If no callback
        can handle a given hint, then it is interpreted as a single type if it is a
        Python class, or as a generic `object` type otherwise, which is equivalent to
        Python's `typing.Any`.  Some type hints, such as `Union` and `Optional`, will
        recursively search the callback map in order to split the hint into its
        constituent types, which will be registered as unique overloads.

        Note that `inspect.get_type_hints(include_extras=True)` is used to extract the
        type hints from the function signature, meaning that stringized annotations and
        forward references will be normalized before any callbacks are invoked.  The
        `include_extras` flag is used to ensure that `typing.Annotated` hints are
        preserved, so that they can be interpreted by the callback map if necessary.
        The default behavior in this case is to simply extract the underlying type,
        but custom callbacks can be added to interpret these annotations as needed. */
        inline static std::vector<Callback> callbacks {
            /// NOTE: Callbacks are linearly searched, so more common constructs should
            /// be generally placed at the front of the list for performance reasons.
            {
                /// TODO: handling GenericAlias types is going to be fairly complicated, 
                /// and will require interactions with the global type map, and thus a
                /// forward declaration here.
                "types.GenericAlias",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"GenericAlias">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        Object typing = import_typing();
                        Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_origin">(typing)),
                            ptr(hint)
                        ));
                        /// TODO: search in type map or fall back to Object
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"UnionType">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(types)),
                            ptr(hint)
                        ));
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Union">(typing))) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Any">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeAliasType",
                [](Object hint, std::set<Object>& out) -> bool {
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Literal">(typing))) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
                        if (args.is(nullptr)) {
                            Exception::from_python();
                        }
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            out.emplace(reinterpret_borrow<Object>(
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"LiteralString">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyUnicode_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.AnyStr",
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"AnyStr">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyUnicode_Type)
                        ));
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBytes_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.NoReturn",
                [](Object hint, std::set<Object>& out) -> bool {
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
                [](Object hint, std::set<Object>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"TypeGuard">(typing))) {
                        out.emplace(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(&PyBool_Type)
                        ));
                        return true;
                    }
                    return false;
                }
            }
        };

        /* Get the return type of the function, using the same callback handlers as
        the parameters.  May return a specialization of `Union` if multiple return
        types are valid, or `NoneType` for void and noreturn functions.  The result is
        always assumed to implement python-style `isinstance()` and `issubclass()`
        checks. */
        const Object& returns() const {
            if (!_returns.is(nullptr)) {
                return _returns;
            }
            std::set<Object> keys;
            Object hint = getattr<"return_annotation">(signature);
            if (hint.is(getattr<"empty">(signature))) {
                keys.insert(reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                ));
            } else {
                parse(hint, keys);
            }
            if (keys.empty()) {
                _returns = reinterpret_borrow<Object>(
                    reinterpret_cast<PyObject*>(Py_TYPE(Py_None))
                );
            } else if (keys.size() == 1) {
                _returns = std::move(*keys.begin());
            } else {
                _returns = to_union(
                    keys,
                    getattr<"Union">(bertrand)
                );
            }
            return _returns;
        }

        /* Convert the introspected signature into a lightweight C++ template key,
        suitable for insertion into a function's overload trie. */
        const Params<std::vector<Param>>& key() const {
            if (key_initialized) {
                return _key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);
            Object VAR_KEYWORD = getattr<"VAR_KEYWORD">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            _key.value.reserve(len);
            for (Object param : parameters) {
                std::string_view name = get_parameter_name(
                    ptr(getattr<"name">(param)
                ));

                ArgKind category;
                Object kind = getattr<"kind">(param);
                if (kind.is(POSITIONAL_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS :
                        ArgKind::POS | ArgKind::OPT;
                } else if (kind.is(POSITIONAL_OR_KEYWORD)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS | ArgKind::KW :
                        ArgKind::POS | ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(KEYWORD_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::KW :
                        ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(VAR_POSITIONAL)) {
                    category = ArgKind::POS | ArgKind::VARIADIC;
                } else if (kind.is(VAR_KEYWORD)) {
                    category = ArgKind::KW | ArgKind::VARIADIC;
                } else {
                    throw TypeError("unrecognized parameter kind: " + repr(kind));
                }

                std::set<Object> types;
                Object hint = getattr<"annotation">(param);
                if (hint.is(empty)) {
                    types.emplace(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(&PyBaseObject_Type)
                    ));
                } else {
                    parse(hint, types);
                }
                if (types.empty()) {
                    throw TypeError(
                        "invalid type hint for parameter '" + std::string(name) +
                        "': " + repr(getattr<"annotation">(param))
                    );
                } else if (types.size() == 1) {
                    _key.value.emplace_back(name, std::move(*types.begin()), category);
                    _key.hash = hash_combine(
                        _key.hash,
                        _key.value.back().hash(seed, prime)
                    );
                } else {
                    _key.value.emplace_back(
                        name,
                        to_union(types, getattr<"Union">(bertrand)),
                        category
                    );
                    _key.hash = hash_combine(
                        _key.hash,
                        _key.value.back().hash(seed, prime)
                    );
                }
            }
            key_initialized = true;
            return _key;
        }

        /* Convert the inspected signature into a valid template key for the
        `bertrand.Function` class on the Python side. */
        const Object& template_key() const {
            if (!_template_key.is(nullptr)) {
                return _template_key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(
                PyTuple_New(len + 1)
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }

            // first element lists type of bound `self` argument, descriptor type, and
            // return type as a slice
            Object returns = this->returns();
            if (returns.is(reinterpret_cast<PyObject*>(Py_TYPE(Py_None)))) {
                returns = None;
            }
            Object self = getattr<"__self__">(func, None);
            if (PyType_Check(ptr(self))) {
                PyObject* slice = PySlice_New(
                    ptr(self),
                    reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type),
                    ptr(returns)
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            } else {
                PyObject* slice = PySlice_New(
                    reinterpret_cast<PyObject*>(Py_TYPE(ptr(self))),
                    Py_None,
                    ptr(returns)
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            }

            /// remaining elements are parameters, with slices, '/', '*', etc.
            const Params<std::vector<Param>>& key = this->key();
            Py_ssize_t offset = 1;
            Py_ssize_t posonly_idx = std::numeric_limits<Py_ssize_t>::max();
            Py_ssize_t kwonly_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < len; ++i) {
                const Param& param = key[i];
                if (param.posonly()) {
                    posonly_idx = i;
                    if (!param.opt()) {
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            Py_NewRef(ptr(param.value))
                        );
                    } else {
                        PyObject* slice = PySlice_New(
                            ptr(param.value),
                            Py_Ellipsis,
                            Py_None
                        );
                        if (slice == nullptr) {
                            Exception::from_python();
                        }
                        PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                    }
                } else {
                    // insert '/' delimiter if there are any posonly arguments
                    if (i > posonly_idx) {
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"/">())
                        );
                        ++offset;

                    // insert '*' delimiter if there are any kwonly arguments
                    } else if (
                        param.kwonly() &&
                        kwonly_idx == std::numeric_limits<Py_ssize_t>::max()
                    ) {
                        kwonly_idx = i;
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"*">())
                        );
                        ++offset;
                    }

                    // insert parameter identifier
                    Object name = reinterpret_steal<Object>(
                        PyUnicode_FromStringAndSize(
                            param.name.data(),
                            param.name.size()
                        )
                    );
                    if (name.is(nullptr)) {
                        Exception::from_python();
                    }
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(param.value),
                        param.opt() ? Py_Ellipsis : Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                }
            }
            _template_key = result;
            return _template_key;
        }

    private:
        mutable bool key_initialized = false;
        mutable Params<std::vector<Param>> _key = {std::vector<Param>{}, 0};
        mutable Object _returns = reinterpret_steal<Object>(nullptr);
        mutable Object _template_key = reinterpret_steal<Object>(nullptr);
    };

    /* Inspect an annotated C++ parameter list at compile time and extract metadata
    that allows a corresponding function to be called with Python-style arguments from
    C++. */
    template <typename... Args>
    struct Arguments : BertrandTag {
    private:

        template <typename...>
        static constexpr size_t _n_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_posonly<T, Ts...> =
            _n_posonly<Ts...> + ArgTraits<T>::posonly();

        template <typename...>
        static constexpr size_t _n_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_pos<T, Ts...> =
            _n_pos<Ts...> + ArgTraits<T>::pos();

        template <typename...>
        static constexpr size_t _n_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kw<T, Ts...> =
            _n_kw<Ts...> + ArgTraits<T>::kw();

        template <typename...>
        static constexpr size_t _n_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kwonly<T, Ts...> =
            _n_kwonly<Ts...> + ArgTraits<T>::kwonly();

        template <typename...>
        static constexpr size_t _n_opt = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt<T, Ts...> =
            _n_opt<Ts...> + ArgTraits<T>::opt();

        template <typename...>
        static constexpr size_t _n_opt_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_posonly<T, Ts...> =
            _n_opt_posonly<Ts...> + (ArgTraits<T>::posonly() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_opt_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_pos<T, Ts...> =
            _n_opt_pos<Ts...> + (ArgTraits<T>::pos() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_opt_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_kw<T, Ts...> =
            _n_opt_kw<Ts...> + (ArgTraits<T>::kw() && ArgTraits<T>::opt());

        template <typename...>
        static constexpr size_t _n_opt_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_kwonly<T, Ts...> =
            _n_opt_kwonly<Ts...> + (ArgTraits<T>::kwonly() && ArgTraits<T>::opt());

        template <StaticStr, typename...>
        static constexpr size_t _idx = 0;
        template <StaticStr Name, typename T, typename... Ts>
        static constexpr size_t _idx<Name, T, Ts...> =
            ArgTraits<T>::name == Name ? 0 : _idx<Name, Ts...> + 1;

        template <typename...>
        static constexpr size_t _args_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _args_idx<T, Ts...> =
            ArgTraits<T>::args() ? 0 : _args_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            ArgTraits<T>::kw() ? 0 : _kw_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() ? 0 : _kwonly_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _kwargs_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwargs_idx<T, Ts...> =
            ArgTraits<T>::kwargs() ? 0 : _kwargs_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _opt_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_idx<T, Ts...> =
            ArgTraits<T>::opt() ? 0 : _opt_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _opt_posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_posonly_idx<T, Ts...> =
            ArgTraits<T>::posonly() && ArgTraits<T>::opt() ?
                0 : _opt_posonly_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _opt_pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_pos_idx<T, Ts...> =
            ArgTraits<T>::pos() && ArgTraits<T>::opt() ?
                0 : _opt_pos_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _opt_kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kw_idx<T, Ts...> =
            ArgTraits<T>::kw() && ArgTraits<T>::opt() ?
                0 : _opt_kw_idx<Ts...> + 1;

        template <typename...>
        static constexpr size_t _opt_kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() && ArgTraits<T>::opt() ?
                0 : _opt_kwonly_idx<Ts...> + 1;

    public:
        static constexpr size_t n                   = sizeof...(Args);
        static constexpr size_t n_posonly           = _n_posonly<Args...>;
        static constexpr size_t n_pos               = _n_pos<Args...>;
        static constexpr size_t n_kw                = _n_kw<Args...>;
        static constexpr size_t n_kwonly            = _n_kwonly<Args...>;
        static constexpr size_t n_opt               = _n_opt<Args...>;
        static constexpr size_t n_opt_posonly       = _n_opt_posonly<Args...>;
        static constexpr size_t n_opt_pos           = _n_opt_pos<Args...>;
        static constexpr size_t n_opt_kw            = _n_opt_kw<Args...>;
        static constexpr size_t n_opt_kwonly        = _n_opt_kwonly<Args...>;

        template <StaticStr Name>
        static constexpr bool has                   = _idx<Name, Args...> != n;
        static constexpr bool has_posonly           = n_posonly > 0;
        static constexpr bool has_pos               = n_pos > 0;
        static constexpr bool has_kw                = n_kw > 0;
        static constexpr bool has_kwonly            = n_kwonly > 0;
        static constexpr bool has_opt               = n_opt > 0;
        static constexpr bool has_opt_posonly       = n_opt_posonly > 0;
        static constexpr bool has_opt_pos           = n_opt_pos > 0;
        static constexpr bool has_opt_kw            = n_opt_kw > 0;
        static constexpr bool has_opt_kwonly        = n_opt_kwonly > 0;
        static constexpr bool has_args              = _args_idx<Args...> != n;
        static constexpr bool has_kwargs            = _kwargs_idx<Args...> != n;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx                 = _idx<Name, Args...>;
        static constexpr size_t kw_idx              = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>;
        static constexpr size_t opt_idx             = _opt_idx<Args...>;
        static constexpr size_t opt_posonly_idx     = _opt_posonly_idx<Args...>;
        static constexpr size_t opt_pos_idx         = _opt_pos_idx<Args...>;
        static constexpr size_t opt_kw_idx          = _opt_kw_idx<Args...>;
        static constexpr size_t opt_kwonly_idx      = _opt_kwonly_idx<Args...>;
        static constexpr size_t args_idx            = _args_idx<Args...>;
        static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>;

        template <size_t I> requires (I < n)
        using at = unpack_type<I, Args...>;

    private:

        template <size_t, typename...>
        static constexpr bool _proper_argument_order = true;
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _proper_argument_order<I, T, Ts...> = (
                ArgTraits<T>::posonly() &&
                (I > kw_idx || I > args_idx || I > kwargs_idx) ||
                (!ArgTraits<T>::opt() && I > opt_idx)
            ) || (
                ArgTraits<T>::pos() && (
                    (I > args_idx || I > kwonly_idx || I > kwargs_idx) ||
                    (!ArgTraits<T>::opt() && I > opt_idx)
                )
            ) || (
                ArgTraits<T>::args() && (I > kwonly_idx || I > kwargs_idx)
            ) || (
                ArgTraits<T>::kwonly() && (I > kwargs_idx)
            ) ?
                false : _proper_argument_order<I + 1, Ts...>;

        template <size_t, typename...>
        static constexpr bool _no_duplicate_arguments = true;
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _no_duplicate_arguments<I, T, Ts...> =
            (ArgTraits<T>::name != "" && I != idx<ArgTraits<T>::name>) ||
            (ArgTraits<T>::args() && I != args_idx) ||
            (ArgTraits<T>::kwargs() && I != kwargs_idx) ?
                false : _no_duplicate_arguments<I + 1, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr bool _compatible() {
            return
                I == n ||
                (I == args_idx && args_idx == n - 1) ||
                (I == kwargs_idx && kwargs_idx == n - 1);
        };
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _compatible() {
            if constexpr (ArgTraits<at<I>>::posonly()) {
                return
                    ArgTraits<T>::posonly() &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::pos()) {
                return
                    (ArgTraits<T>::pos() && ArgTraits<T>::kw()) &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::kw()) {
                return
                    (ArgTraits<T>::kw() && ArgTraits<T>::pos()) &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::args()) {
                if constexpr (ArgTraits<T>::pos() || ArgTraits<T>::args()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >()) {
                        return false;
                    }
                    return _compatible<I, Ts...>();
                }
                return _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                if constexpr (ArgTraits<T>::kw() || ArgTraits<T>::kwargs()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >()) {
                        return false;
                    }
                    return _compatible<I, Ts...>();
                }
                return _compatible<I + 1, Ts...>();

            } else {
                static_assert(false, "unrecognized parameter type");
                return false;
            }
        }

        template <size_t I>
        static constexpr uint64_t _required =
            ArgTraits<unpack_type<I, Args...>>::opt() ||
            ArgTraits<unpack_type<I, Args...>>::variadic() ?
                0ULL : 1ULL << I;

    public:

        static constexpr bool args_are_convertible_to_python =
            (std::convertible_to<typename ArgTraits<Args>::type, Object> && ...);
        static constexpr bool proper_argument_order =
            _proper_argument_order<0, Args...>;
        static constexpr bool no_duplicate_arguments =
            _no_duplicate_arguments<0, Args...>;
        static constexpr bool no_qualified_arg_annotations =
            !((is_arg<Args> && (
                std::is_reference_v<Args> ||
                std::is_const_v<std::remove_reference_t<Args>> ||
                std::is_volatile_v<std::remove_reference_t<Args>>
            )) || ...);
        static constexpr bool no_qualified_args =
            !((std::is_reference_v<Args> ||
                std::is_const_v<std::remove_reference_t<Args>> ||
                std::is_volatile_v<std::remove_reference_t<Args>> ||
                std::is_reference_v<typename ArgTraits<Args>::type> ||
                std::is_const_v<std::remove_reference_t<typename ArgTraits<Args>::type>> ||
                std::is_volatile_v<std::remove_reference_t<typename ArgTraits<Args>::type>>
            ) || ...);
        static constexpr bool args_are_python =
            (inherits<typename ArgTraits<Args>::type, Object> && ...);

        /* A template constraint that evaluates true if another signature represents a
        viable overload of a function with this signature. */
        template <typename... Ts>
        static constexpr bool compatible = _compatible<0, Ts...>(); 

    private:

        /* Get the size of the perfect hash table required to efficiently validate
        keyword arguments as a power of 2. */
        static constexpr size_t keyword_table_size = next_power_of_two(2 * n_kw);

        /* Take the modulus with respect to the keyword table by exploiting the power
        of 2 table size. */
        static constexpr size_t keyword_modulus(size_t hash) {
            return hash & (keyword_table_size - 1);
        }

        /* Given a precomputed keyword index into the hash table, check to see if any
        subsequent arguments in the parameter list hash to the same index. */
        template <typename...>
        struct _collisions {
            static constexpr bool operator()(size_t, size_t, size_t) { return false; }
        };
        template <typename T, typename... Ts>
        struct _collisions<T, Ts...> {
            static constexpr bool operator()(size_t idx, size_t seed, size_t prime) {
                if constexpr (ArgTraits<T>::kw()) {
                    size_t hash = fnv1a(
                        ArgTraits<T>::name,
                        seed,
                        prime
                    );
                    return
                        (keyword_modulus(hash) == idx) ||
                        _collisions<Ts...>{}(idx, seed, prime);
                } else {
                    return _collisions<Ts...>{}(idx, seed, prime);
                }
            }
        };

        /* Check to see if the candidate seed and prime produce any collisions for the
        observed keyword arguments. */
        template <typename...>
        struct collisions {
            static constexpr bool operator()(size_t, size_t) { return false; }
        };
        template <typename T, typename... Ts>
        struct collisions<T, Ts...> {
            static constexpr bool operator()(size_t seed, size_t prime) {
                if constexpr (ArgTraits<T>::kw()) {
                    size_t hash = fnv1a(
                        ArgTraits<T>::name,
                        seed,
                        prime
                    );
                    return _collisions<Ts...>{}(
                        keyword_modulus(hash),
                        seed,
                        prime
                    ) || collisions<Ts...>{}(seed, prime);
                } else {
                    return collisions<Ts...>{}(seed, prime);
                }
            }
        };

        /* Find an FNV-1a seed and prime that produces perfect hashes with respect to
        the keyword table size. */
        static constexpr auto hash_components = [] -> std::pair<size_t, size_t> {
            constexpr size_t recursion_limit = fnv1a_seed + 100'000;
            size_t seed = fnv1a_seed;
            size_t prime = fnv1a_prime;
            size_t i = 0;
            while (collisions<Args...>{}(seed, prime)) {
                if (++seed > recursion_limit) {
                    if (++i == 10) {
                        std::cerr << "error: unable to find a perfect hash seed "
                                  << "after 10^6 iterations.  Consider increasing the "
                                  << "recursion limit or reviewing the keyword "
                                  << "argument names for potential issues.\n";
                        std::exit(1);
                    }
                    seed = fnv1a_seed;
                    prime = fnv1a_fallback_primes[i];
                }
            }
            return {seed, prime};
        }();

    public:

        /* A seed for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t seed = hash_components.first;

        /* A prime for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t prime = hash_components.second;

        /* Hash a byte string according to the FNV-1a algorithm using the seed and
        prime that were found at compile time to perfectly hash the keyword
        arguments. */
        static constexpr size_t hash(const char* str) noexcept {
            return fnv1a(str, seed, prime);
        }
        static constexpr size_t hash(std::string_view str) noexcept {
            return fnv1a(str.data(), seed, prime);
        }
        static constexpr size_t hash(const std::string& str) noexcept {
            return fnv1a(str.data(), seed, prime);
        }

        /* A single entry in a callback table, storing the argument name, a one-hot
        encoded bitmask specifying this argument's position, a function that can be
        used to validate the argument, and a lazy function that can be used to retrieve
        its corresponding Python type. */
        struct Callback {
            std::string_view name;
            uint64_t mask = 0;
            bool(*isinstance)(const Object&) = nullptr;
            bool(*issubclass)(const Object&) = nullptr;
            Object(*type)() = nullptr;
            explicit operator bool() const noexcept { return isinstance != nullptr; }
        };

        /* A bitmask with a 1 in the position of all of the required arguments in the
        parameter list.

        Each callback stores a one-hot encoded mask that is joined into a single
        bitmask as each argument is processed.  The resulting mask can then be compared
        to this constant to determine if all required arguments have been provided.  If
        that comparison evaluates to false, then further bitwise inspection can be done
        to determine exactly which arguments were missing, as well as their names.

        Note that this mask effectively limits the number of arguments that a function
        can accept to 64, which is a reasonable limit for most functions.  The
        performance benefits justify the limitation, and if you need more than 64
        arguments, you should probably be using a different design pattern. */
        static constexpr uint64_t required = []<size_t... Is>(std::index_sequence<Is...>) {
            return (_required<Is> | ...);
        }(std::make_index_sequence<n>{});

    private:
        static constexpr Callback null_callback;

        /* Populate the positional argument table with an appropriate callback for
        each argument in the parameter list. */
        template <size_t I>
        static consteval Callback populate_positional_table() {
            using T = at<I>;
            return {
                .name = ArgTraits<T>::name,
                .mask = ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                .isinstance = [](const Object& value) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (has_python<U>) {
                        return isinstance<std::remove_cvref_t<python_type<U>>>(value);
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(U).name())
                        );
                    }
                },
                .issubclass = [](const Object& type) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (has_python<U>) {
                        return issubclass<std::remove_cvref_t<python_type<U>>>(type);
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(U).name())
                        );
                    }
                },
                .type = []() -> Object {
                    using U = ArgTraits<T>::type;
                    if constexpr (has_python<U>) {
                        return Type<std::remove_cvref_t<python_type<U>>>();
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(U).name())
                        );
                    }
                }
            };
        }

        /* An array of positional arguments to callbacks and bitmasks that can be used
        to validate the argument dynamically at runtime. */
        static constexpr auto positional_table = []<size_t... Is>(
            std::index_sequence<Is...>
        ) -> std::array<Callback, n> {
            return {populate_positional_table<Is>()...};
        }(std::make_index_sequence<n>{});

        /* Populate the keyword table with an appropriate callback for each keyword
        argument in the parameter list. */
        template <size_t I>
        static constexpr void populate_keyword_table(
            std::array<Callback, keyword_table_size>& table,
            size_t seed,
            size_t prime
        ) {
            using T = at<I>;
            if constexpr (ArgTraits<T>::kw()) {
                table[keyword_modulus(hash(ArgTraits<T>::name.data()))] = {
                    .name = ArgTraits<T>::name,
                    .mask = ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                    .isinstance = [](const Object& value) -> bool {
                        using U = ArgTraits<T>::type;
                        if constexpr (has_python<U>) {
                            return isinstance<std::remove_cvref_t<python_type<U>>>(value);
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(U).name())
                            );
                        }
                    },
                    .issubclass = [](const Object& type) -> bool {
                        using U = ArgTraits<T>::type;
                        if constexpr (has_python<U>) {
                            return issubclass<std::remove_cvref_t<python_type<U>>>(type);
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(U).name())
                            );
                        }
                    },
                    .type = []() -> Object {
                        using U = ArgTraits<T>::type;
                        if constexpr (has_python<U>) {
                            return Type<std::remove_cvref_t<python_type<U>>>();
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(U).name())
                            );
                        }
                    }
                };
            }
        }

        /* The keyword table itself.  Each entry holds the expected keyword name for
        validation as well as a callback that can be used to validate its type
        dynamically at runtime. */
        static constexpr auto keyword_table = []<size_t... Is>(
            std::index_sequence<Is...>,
            size_t seed,
            size_t prime
        ) {
            std::array<Callback, keyword_table_size> table;
            (populate_keyword_table<Is>(table, seed, prime), ...);
            return table;
        }(std::make_index_sequence<n>{}, seed, prime);

        template <size_t I>
        static Param _key(size_t& hash) {
            using T = at<I>;
            constexpr Callback& callback = positional_table[I];
            Param param = {
                .name = ArgTraits<T>::name,
                .value = callback.type(),
                .kind = ArgTraits<T>::kind
            };
            hash = hash_combine(hash, param.hash(seed, prime));
            return param;
        }

    public:

        /* Look up a positional argument, returning a callback object that can be used
        to efficiently validate it.  If the index does not correspond to a recognized
        positional argument, a null callback will be returned that evaluates to false
        under boolean logic.  If the parameter list accepts variadic positional
        arguments, then the variadic argument's callback will be returned instead. */
        static constexpr Callback& callback(size_t i) noexcept {
            if constexpr (has_args) {
                return i < args_idx ? positional_table[i] : positional_table[args_idx];
            } else if constexpr (has_kwonly) {
                return i < kwonly_idx ? positional_table[i] : null_callback;
            } else {
                return i < kwargs_idx ? positional_table[i] : null_callback;
            }
        }

        /* Look up a keyword argument, returning a callback object that can be used to
        efficiently validate it.  If the argument name is not recognized, a null
        callback will be returned that evaluates to false under boolean logic.  If the
        parameter list accepts variadic keyword arguments, then the variadic argument's
        callback will be returned instead. */
        static constexpr Callback& callback(std::string_view name) noexcept {
            const Callback& callback = keyword_table[
                keyword_modulus(hash(name.data()))
            ];
            if (callback.name == name) {
                return callback;
            } else {
                if constexpr (has_kwargs) {
                    return keyword_table[kwargs_idx];
                } else {
                    return null_callback;
                }
            }
        }

        /* Produce an overload key that matches the enclosing parameter list. */
        static Params<std::array<Param, n>> key() {
            size_t hash = 0;
            return {
                .value = []<size_t... Is>(std::index_sequence<Is...>, size_t& hash) {
                    return std::array<Param, n>{_key<Is>(hash)...};
                }(std::make_index_sequence<n>{}, hash),
                .hash = hash
            };
        }

    private:

        /* Conditionally convert a value to an Arg<> annotation when calling the
        function. */
        template <size_t I, typename T>
        static constexpr Arguments::at<I> to_arg(T&& value) {
            if constexpr (is_arg<Arguments::at<I>>) {
                return {std::forward<T>(value)};
            } else {
                return std::forward<T>(value);
            }
        };

        /* Encapsulates a pair of iterators over a positional argument pack and
        validates that they are fully consumed. */
        template <typename Signature, typename Pack>
        struct PositionalPack {
            std::ranges::iterator_t<const Pack&> begin;
            std::ranges::sentinel_t<const Pack&> end;
            size_t size;
            size_t consumed = 0;

            PositionalPack(const Pack& pack) :
                begin(std::ranges::begin(pack)),
                end(std::ranges::end(pack)),
                size(std::ranges::size(pack))
            {}

            void validate() {
                if constexpr (!Signature::has_args) {
                    if (begin != end) {
                        std::string message =
                            "too many arguments in positional parameter pack: ['" +
                            repr(*begin);
                        while (++begin != end) {
                            message += "', '" + repr(*begin);
                        }
                        message += "']";
                        throw TypeError(message);
                    }
                }
            }

            bool has_value() const { return begin != end; }
            decltype(auto) value() {
                decltype(auto) result = *begin;
                ++begin;
                ++consumed;
                return result;
            }
        };

        /* Encapsulates a map of keyword arguments drawn from a keyword argument pack
        and validates that they are fully consumed. */
        template <typename Signature, typename Pack>
        struct KeywordPack {
            using Map = std::unordered_map<std::string, typename Pack::mapped_type>;
            Map map;

            KeywordPack(const Pack& pack) :
                map([]<size_t... Is>(std::index_sequence<Is...>, const Pack& pack) {
                    Map map;
                    map.reserve(pack.size());
                    for (auto&& [key, value] : pack) {
                        auto [_, inserted] = map.emplace(key, value);
                        if (!inserted) {
                            throw TypeError(
                                "duplicate keyword argument: '" + repr(key) +
                                "'"
                            );
                        }
                        return map;
                    }
                }(std::make_index_sequence<Signature::n>{}, pack))
            {}

            void validate() {
                if constexpr (!Signature::has_kwargs) {
                    if (!map.empty()) {
                        auto it = map.begin();
                        auto end = map.end();
                        std::string message =
                            "unexpected keyword arguments: ['" + it->first;
                        while (++it != end) {
                            message += "', '" + it->first;
                        }
                        message += "']";
                        throw TypeError(message);
                    }
                }
            }

            auto size() const { return map.size(); }
            template <typename T>
            auto extract(T&& key) { return map.extract(std::forward<T>(key)); }
            auto begin() { return map.begin(); }
            auto end() { return map.end(); }
        };

        /* Extract a positional parameter pack from the given arguments and return a
        helper struct that encapsulates the validated arguments. */
        template <typename Signature, size_t J, typename... Values>
            requires (J < sizeof...(Values))
        static auto positional_pack(Values&&... values) {
            using Pack = std::remove_cvref_t<unpack_type<J, Values...>>;
            return PositionalPack<Signature, Pack>{
                unpack_arg<J>(std::forward<Values>(values)...)
            };
        }

        /* Extract a keyword parameter pack from the given arguments and return a
        helper struct that encapsulates the validated arguments.  */
        template <typename Signature, size_t J, typename... Values>
            requires (J < sizeof...(Values))
        static auto keyword_pack(Values&&... values) {
            using Pack = std::remove_cvref_t<unpack_type<J, Values...>>;
            return KeywordPack<Signature, Pack>{
                unpack_arg<J>(std::forward<Values>(values)...)
            };
        }

    public:

        /* A tuple holding the default value for every argument in the enclosing
        parameter list that is marked as optional. */
        struct Defaults {
        private:

            /* The type of a single value in the defaults tuple.  The templated index
            is used to correlate the default value with its corresponding argument in
            the enclosing signature. */
            template <size_t I>
            struct Value {
                using type = ArgTraits<Arguments::at<I>>::type;
                static constexpr StaticStr name = ArgTraits<Arguments::at<I>>::name;
                static constexpr size_t index = I;
                std::remove_reference_t<type> value;

                type get(this auto& self) {
                    if constexpr (std::is_lvalue_reference_v<type>) {
                        return self.value;
                    } else {
                        return std::remove_cvref_t<type>(self.value);
                    }
                }
            };

            /* Build a sub-signature holding only the arguments marked as optional from
            the enclosing signature.  This will be a specialization of the enclosing
            class, which is used to bind arguments to this class's constructor using
            the same semantics as the function's call operator. */
            template <typename out, typename...>
            struct _Inner { using type = out; };
            template <typename... out, typename T, typename... Ts>
            struct _Inner<Arguments<out...>, T, Ts...> {
                template <typename>
                struct sub_signature {
                    using type = _Inner<Arguments<out...>, Ts...>::type;
                };
                template <typename U> requires (ArgTraits<U>::opt())
                struct sub_signature<U> {
                    using type =_Inner<
                        Arguments<out..., typename ArgTraits<U>::no_opt>,
                        Ts...
                    >::type;
                };
                using type = sub_signature<T>::type;
            };
            using Inner = _Inner<Arguments<>, Args...>::type;

            /* Build a std::tuple of Value<I> instances to hold the default values
            themselves. */
            template <typename out, size_t, typename...>
            struct _Tuple { using type = out; };
            template <typename... out, size_t I, typename T, typename... Ts>
            struct _Tuple<std::tuple<out...>, I, T, Ts...> {
                template <typename U>
                struct tuple {
                    using type = _Tuple<std::tuple<out...>, I + 1, Ts...>::type;
                };
                template <typename U> requires (ArgTraits<U>::opt())
                struct tuple<U> {
                    using type = _Tuple<std::tuple<out..., Value<I>>, I + 1, Ts...>::type;
                };
                using type = tuple<T>::type;
            };
            using Tuple = _Tuple<std::tuple<>, 0, Args...>::type;

            template <size_t, typename>
            static constexpr size_t _find = 0;
            template <size_t I, typename T, typename... Ts>
            static constexpr size_t _find<I, std::tuple<T, Ts...>> =
                (I == T::index) ? 0 : 1 + _find<I, std::tuple<Ts...>>;

        public:
            static constexpr size_t n               = Inner::n;
            static constexpr size_t n_posonly       = Inner::n_posonly;
            static constexpr size_t n_pos           = Inner::n_pos;
            static constexpr size_t n_kw            = Inner::n_kw;
            static constexpr size_t n_kwonly        = Inner::n_kwonly;
            static constexpr size_t n_opt           = 0;
            static constexpr size_t n_opt_posonly   = 0;
            static constexpr size_t n_opt_pos       = 0;
            static constexpr size_t n_opt_kw        = 0;
            static constexpr size_t n_opt_kwonly    = 0;

            template <StaticStr Name>
            static constexpr bool has               = Inner::template has<Name>;
            static constexpr bool has_posonly       = Inner::has_posonly;
            static constexpr bool has_pos           = Inner::has_pos;
            static constexpr bool has_kw            = Inner::has_kw;
            static constexpr bool has_kwonly        = Inner::has_kwonly;
            static constexpr bool has_opt           = false;
            static constexpr bool has_opt_posonly   = false;
            static constexpr bool has_opt_pos       = false;
            static constexpr bool has_opt_kw        = false;
            static constexpr bool has_opt_kwonly    = false;
            static constexpr bool has_args          = false;
            static constexpr bool has_kwargs        = false;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Inner::template idx<Name>;
            static constexpr size_t kw_idx          = Inner::kw_idx;
            static constexpr size_t kwonly_idx      = Inner::kwonly_idx;
            static constexpr size_t opt_idx         = n;
            static constexpr size_t opt_posonly_idx = n;
            static constexpr size_t opt_pos_idx     = n;
            static constexpr size_t opt_kw_idx      = n;
            static constexpr size_t opt_kwonly_idx  = n;
            static constexpr size_t args_idx        = n;
            static constexpr size_t kwargs_idx      = n;

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;

            /* Bind an argument list to the default values tuple using the
            sub-signature's normal Bind<> machinery. */
            template <typename... Values>
            using Bind = Inner::template Bind<Values...>;

            static_assert(Bind<Arg<"y", double>>::enable);

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if that index corresponds to a default value. */
            template <size_t I> requires (ArgTraits<typename Arguments::at<I>>::opt())
            static constexpr size_t find = _find<I, Tuple>;

            /* Given an index into the  */
            template <size_t I> requires (I < n)
            static constexpr size_t rfind = std::tuple_element<I, Tuple>::type::index;

        private:

            template <size_t I, typename... Values>
            static constexpr decltype(auto) translate(Values&&... values) {
                using Bound = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::posonly()) {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::pos()) {
                    if constexpr (I < Bound::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        return impl::unpack_arg<Bound::template idx<name>>(
                            std::forward<Values>(values)...
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    return impl::unpack_arg<Bound::template idx<name>>(
                        std::forward<Values>(values)...
                    );

                } else {
                    static_assert(false, "unrecognized parameter type");
                    std::unreachable();
                }
            }

            template <size_t I, typename Pos, typename... Values>
            static constexpr decltype(auto) translate(
                PositionalPack<Inner, Pos>& positional,
                Values&&... values
            ) {
                using Bound = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::posonly()) {
                    if constexpr (I < Bound::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (positional.has_value()) {
                            return positional.value();
                        }
                        throw TypeError(
                            "no match for positional-only parmater at index " +
                            std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::pos()) {
                    if constexpr (I < Bound::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (positional.has_value()) {
                            if constexpr (Bound::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                return positional.value();
                            }
                        }
                        if constexpr (Bound::template has<name>) {
                            return impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            );
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    return impl::unpack_arg<Bound::template idx<name>>(
                        std::forward<Values>(values)...
                    );

                } else {
                    static_assert(false, "unrecognized parameter type");
                    std::unreachable();
                }
            }

            template <size_t I, typename Kw, typename... Values>
            static constexpr decltype(auto) translate(
                KeywordPack<Inner, Kw>& keyword,
                Values&&... values
            ) {
                using Bound = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::posonly()) {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::pos()) {
                    auto node = keyword.extract(name);
                    if (node) {
                        if constexpr (Bound::template has<name>) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        } else if constexpr (std::is_lvalue_reference_v<
                            typename ArgTraits<T>::type
                        >) {
                            return node.mapped();
                        } else {
                            return std::move(node.mapped());
                        }
                    }
                    if constexpr (I < Bound::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Bound::template has<name>) {
                        return impl::unpack_arg<Bound::template idx<name>>(
                            std::forward<Values>(values)...
                        );
                    } else {
                        throw TypeError(
                            "no match for parameter '" + name +
                            "' at index " + std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto node = keyword.extract(name);
                    if (node) {
                        if constexpr (Bound::template has<name>) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        } else if constexpr (std::is_lvalue_reference_v<
                            typename ArgTraits<T>::type
                        >) {
                            return node.mapped();
                        } else {
                            return std::move(node.mapped());
                        }
                    }
                    if constexpr (Bound::template has<name>) {
                        return impl::unpack_arg<Bound::template idx<name>>(
                            std::forward<Values>(values)...
                        );
                    } else {
                        throw TypeError(
                            "no match for parameter '" + name +
                            "' at index " + std::to_string(I)
                        );
                    }

                } else {
                    static_assert(false, "unrecognized parameter type");
                    std::unreachable();
                }
            }

            template <size_t I, typename Pos, typename Kw, typename... Values>
            static constexpr decltype(auto) translate(
                PositionalPack<Inner, Pos>& positional,
                KeywordPack<Inner, Kw>& keyword,
                Values&&... values
            ) {
                using Bound = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::posonly()) {
                    return translate<I>(positional, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::pos()) {
                    auto node = keyword.extract(name);
                    if constexpr (I < Bound::kw_idx) {
                        if (node) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Bound::template has<name>) {
                        if (node || positional.has_value()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<Bound::template idx<name>>(
                            std::forward<Values>(values)...
                        );
                    } else {
                        if (positional.has_value()) {
                            if (node) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                            return positional.value();
                        } else if (node) {
                            if constexpr (std::is_lvalue_reference_v<
                                typename ArgTraits<T>::type
                            >) {
                                return node.mapped();
                            } else {
                                return std::move(node.mapped());
                            }
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    return translate<I>(keyword, std::forward<Values>(values)...);

                } else {
                    static_assert(false, "unrecognized parameter type");
                    std::unreachable();
                }
            }

            template <size_t... Is, typename... Values>
            static constexpr Tuple build(
                std::index_sequence<Is...>,
                Values&&... values
            ) {
                using Bound = Arguments<Values...>;

                /// NOTE: Because we're using a braced initializer to construct the
                /// tuple, the translate() helper will always be called with strict
                /// left-to-right evaluation order, which accounts for side effects
                /// in the unpacking process.
                if constexpr (Bound::has_args && Bound::has_kwargs) {
                    auto positional = positional_pack<Inner, Bound::args_idx>(
                        std::forward<Values>(values)...
                    );
                    auto keyword = keyword_pack<Inner, Bound::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    Tuple result = {{
                        translate<Is>(positional, keyword, std::forward<Values>(values)...)
                    }...};
                    positional.validate();
                    keyword.validate();
                    return result;

                } else if constexpr (Bound::has_args) {
                    auto positional = positional_pack<Inner, Bound::args_idx>(
                        std::forward<Values>(values)...
                    );
                    Tuple result = {{
                        translate<Is>(positional, std::forward<Values>(values)...)
                    }...};
                    positional.validate();
                    return result;

                } else if constexpr (Bound::has_kwargs) {
                    auto keyword = keyword_pack<Inner, Bound::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    Tuple result = {{
                        translate<Is>(keyword, std::forward<Values>(values)...)
                    }...};
                    keyword.validate();
                    return result;

                } else {
                    return {{translate<Is>(std::forward<Values>(values)...)}...};
                }
            }

        public:
            Tuple values;

            /* The default values' constructor takes Python-style arguments just like
            the call operator, and is only enabled if the call signature is well-formed
            and all optional arguments have been accounted for. */
            template <typename... Values> requires (Bind<Values...>::enable)
            constexpr Defaults(Values&&... values) : values(build(
                std::index_sequence_for<Values...>{},
                std::forward<Values>(values)...
            )) {}
            constexpr Defaults(const Defaults& other) = default;
            constexpr Defaults(Defaults&& other) = default;

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() {
                return std::get<I>(values).get();
            }

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() const {
                return std::get<I>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() {
                return std::get<idx<Name>>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() const {
                return std::get<idx<Name>>(values).get();
            }
        };

        /// TODO: maybe the index operator has to take types instead of values?  Values
        /// could be converted into types by using the `bertrand[x]`, which will either
        /// directly map input types or convert values into their equivalent bertrand
        /// type by calling `bertrand(x)` and then indexing with the result.  It would
        /// always return a properly-normalized Bertrand type, which is then used to
        /// index the type map.

        /* A Trie-based data structure that describes a collection of dynamic overloads
        for a `py::Function` object, which will be dispatched to when called from
        either Python or C++. */
        struct Overloads {
        private:
            struct BoundView;

            static bool instance_check(PyObject* obj, PyObject* cls) {
                int rc = PyObject_IsInstance(obj, cls);
                if (rc < 0) {
                    Exception::from_python();
                }
                return rc;
            }

        public:
            struct Metadata;
            struct Edge;
            struct Edges;
            struct Node;

            /* An encoded representation of a function that has been inserted into the
            overload trie, which includes the function itself, a hash of the key that
            it was inserted under, a bitmask of the required arguments that must be
            satisfied to invoke the function, and a canonical path of edges starting
            from the root node that leads to the terminal function.

            These are stored in an associative set rather than a hash set in order to
            ensure address stability over the lifetime of the trie, so that it doesn't
            need to manage any memory itself. */
            struct Metadata {
                size_t hash;
                uint64_t required;
                Object func;
                std::vector<Edge> path;
                friend bool operator<(const Metadata& lhs, const Metadata& rhs) {
                    return lhs.hash < rhs.hash;
                }
                friend bool operator<(const Metadata& lhs, size_t rhs) {
                    return lhs.hash < rhs;
                }
                friend bool operator<(size_t lhs, const Metadata& rhs) {
                    return lhs < rhs.hash;
                }
            };

            /* A single link between two nodes in the trie, which describes how to
            traverse from one to the other.  Multiple edges may share the same target
            node, and a unique edge will be created for each parameter in a key when it
            is inserted, such that the original key can be unambiguously identified
            from a simple search of the trie structure. */
            struct Edge {
                size_t hash;
                uint64_t mask;
                std::string name;
                Object type;
                ArgKind kind;
                std::shared_ptr<Node> node;
            };

            /* A sorted collection of outgoing edges linking a node to its descendants.
            Edges are topologically sorted by their expected type, with subclasses
            coming before their parent classes. */
            struct Edges {
            private:
                friend BoundView;

                /* `issubclass()` checks are used to sort the edge map, with ties
                being broken by address. */
                struct TopoSort {
                    static bool operator()(PyObject* lhs, PyObject* rhs) {
                        int rc = PyObject_IsSubclass(lhs, rhs);
                        if (rc < 0) {
                            Exception::from_python();
                        }
                        return rc || lhs < rhs;
                    }
                };

                /* Edges are stored indirectly to simplify memory management, and are
                sorted based on kind, with required arguments coming before optional,
                which come before variadic, with ties broken by hash.  Each one refers
                to the contents of a `Metadata::path` sequence, which is guaranteed to
                have a stable address for the lifetime of the overload. */
                struct EdgePtr {
                    Edge* edge;
                    EdgePtr(const Edge* edge = nullptr) : edge(edge) {}
                    operator const Edge*() const { return edge; }
                    const Edge& operator*() const { return *edge; }
                    const Edge* operator->() const { return edge; }
                    friend bool operator<(const EdgePtr& lhs, const EdgePtr& rhs) {
                        return
                            lhs.edge->kind < rhs.edge->kind ||
                            lhs.edge->hash < rhs.edge->hash;
                    }
                    friend bool operator<(const EdgePtr& lhs, size_t rhs) {
                        return lhs.edge->hash < rhs;
                    }
                    friend bool operator<(size_t lhs, const EdgePtr& rhs) {
                        return lhs < rhs.edge->hash;
                    }
                };

                /* Edge pointers are stored in another associative set to achieve
                the nested sorting.  By definition, each edge within the set points
                to the same destination node. */
                struct EdgeKinds {
                    using Set = std::set<const EdgePtr, std::less<>>;
                    std::shared_ptr<Node> node;
                    Set set;
                };

                /* The types stored in the edge map are also borrowed references to a
                `Metadata::path` sequence to simplify memory management. */
                using Map = std::map<PyObject*, EdgeKinds, TopoSort>;
                Map map;

                /* A range adaptor that only yields edges matching a particular key,
                identified by its hash. */
                struct HashView {
                    const Edges& self;
                    Object value;
                    size_t hash;

                    struct Iterator {
                        using iterator_category = std::input_iterator_tag;
                        using difference_type = std::ptrdiff_t;
                        using value_type = const Edge*;
                        using pointer = value_type*;
                        using reference = value_type&;

                        Map::iterator it;
                        Map::iterator end;
                        Object value;
                        size_t hash;
                        const Edge* curr;

                        Iterator(
                            Map::iterator&& it,
                            Map::iterator&& end,
                            const Object& value,
                            size_t hash
                        ) : it(std::move(it)), end(std::move(end)), value(value),
                            hash(hash), curr(nullptr)
                        {
                            while (this->it != this->end) {
                                if (instance_check(
                                    ptr(value),
                                    this->it->first
                                )) {
                                    auto lookup = this->it->second.set.find(hash);
                                    if (lookup != this->it->second.set.end()) {
                                        curr = *lookup;
                                        break;
                                    }
                                }
                                ++it;
                            }
                        }

                        Iterator& operator++() {
                            ++it;
                            while (it != end) {
                                if (instance_check(
                                    ptr(value),
                                    it->first
                                )) {
                                    auto lookup = it->second.set.find(hash);
                                    if (lookup != it->second.set.end()) {
                                        curr = *lookup;
                                        break;
                                    }
                                }
                                ++it;
                            }
                            return *this;
                        }

                        const Edge* operator*() const {
                            return curr;
                        }

                        friend bool operator==(
                            const Iterator& iter,
                            const Sentinel& sentinel
                        ) {
                            return iter.it == iter.end;
                        }

                        friend bool operator==(
                            const Sentinel& sentinel,
                            const Iterator& iter
                        ) {
                            return iter.it == iter.end;
                        }

                        friend bool operator!=(
                            const Iterator& iter,
                            const Sentinel& sentinel
                        ) {
                            return iter.it != iter.end;
                        }

                        friend bool operator!=(
                            const Sentinel& sentinel,
                            const Iterator& iter
                        ) {
                            return iter.it != iter.end;
                        }
                    };

                    Iterator begin() const {
                        return {self.begin(), self.end(), value, hash};
                    }

                    Sentinel end() const {
                        return {};
                    }
                };

                /* A range adaptor that yields edges in order, regardless of key. */
                struct OrderedView {
                    const Edges& self;
                    Object value;

                    struct Iterator {
                        using iterator_category = std::input_iterator_tag;
                        using difference_type = std::ptrdiff_t;
                        using value_type = const Edge*;
                        using pointer = value_type*;
                        using reference = value_type&;

                        Map::iterator it;
                        Map::iterator end;
                        EdgeKinds::Set::iterator edge_it;
                        EdgeKinds::Set::iterator edge_end;
                        Object value;

                        Iterator(
                            Map::iterator&& it,
                            Map::iterator&& end,
                            const Object& value
                        ) : it(std::move(it)), end(std::move(end)), value(value)
                        {
                            while (this->it != this->end) {
                                if (instance_check(
                                    ptr(value),
                                    this->it->first
                                )) {
                                    edge_it = this->it->second.set.begin();
                                    edge_end = this->it->second.set.end();
                                    break;
                                }
                                ++it;
                            }
                        }

                        Iterator& operator++() {
                            ++edge_it;
                            if (edge_it == edge_end) {
                                ++it;
                                while (it != end) {
                                    if (instance_check(
                                        ptr(value),
                                        it->first
                                    )) {
                                        edge_it = it->second.set.begin();
                                        edge_end = it->second.set.end();
                                        break;
                                    }
                                    ++it;
                                }
                            }
                            return *this;
                        }

                        const Edge* operator*() const {
                            return *edge_it;
                        }

                        friend bool operator==(
                            const Iterator& iter,
                            const Sentinel& sentinel
                        ) {
                            return iter.it == iter.end;
                        }

                        friend bool operator==(
                            const Sentinel& sentinel,
                            const Iterator& iter
                        ) {
                            return iter.it == iter.end;
                        }

                        friend bool operator!=(
                            const Iterator& iter,
                            const Sentinel& sentinel
                        ) {
                            return iter.it != iter.end;
                        }

                        friend bool operator!=(
                            const Sentinel& sentinel,
                            const Iterator& iter
                        ) {
                            return iter.it != iter.end;
                        }

                    };

                    Iterator begin() const {
                        return {self.begin(), self.end(), value};
                    }

                    Sentinel end() const {
                        return {};
                    }
                };

            public:
                auto size() const { return map.size(); }
                auto empty() const { return map.empty(); }
                auto begin() const { return map.begin(); }
                auto cbegin() const { return map.cbegin(); }
                auto end() const { return map.end(); }
                auto cend() const { return map.cend(); }

                /* Insert an edge into this map and initialize its node pointer.
                Returns true if the insertion resulted in the creation of a new node,
                or false if the edge references an existing node. */
                [[maybe_unused]] bool insert(Edge& edge) {
                    auto [outer, inserted] = map.try_emplace(
                        ptr(edge.type),
                        EdgeKinds{}
                    );
                    auto [_, success] = outer->second.set.emplace(&edge);
                    if (!success) {
                        if (inserted) {
                            map.erase(outer);
                        }
                        throw TypeError(
                            "overload trie already contains an edge for type: " +
                            repr(edge.type)
                        );
                    }
                    if (inserted) {
                        outer->second.node = std::make_shared<Node>();
                    }
                    edge.node = outer->second.node;
                    return inserted;
                }

                /* Insert an edge into this map using an explicit node pointer.
                Returns true if the insertion created a new table in the map, or false
                if it was added to an existing one.  Does NOT initialize the edge's
                node pointer, and a false return value does NOT guarantee that the
                existing table references the same node. */
                [[maybe_unused]] bool insert(Edge& edge, std::shared_ptr<Node> node) {
                    auto [outer, inserted] = map.try_emplace(
                        ptr(edge.type),
                        EdgeKinds{node}
                    );
                    auto [_, success] = outer->second.set.emplace(&edge);
                    if (!success) {
                        if (inserted) {
                            map.erase(outer);
                        }
                        throw TypeError(
                            "overload trie already contains an edge for type: " +
                            repr(edge.type)
                        );
                    }
                    return inserted;
                }

                /* Remove any outgoing edges that match the given hash. */
                void remove(size_t hash) noexcept {
                    std::vector<PyObject*> dead;
                    for (auto& [type, table] : map) {
                        table.set.erase(hash);
                        if (table.set.empty()) {
                            dead.emplace_back(type);
                        }
                    }
                    for (PyObject* type : dead) {
                        map.erase(type);
                    }
                }

                /* Return a range adaptor that iterates over the topologically-sorted
                types and yields individual edges for those that match against an
                observed type.  If multiple edges exist for a given type, then the
                range will yield them in order based on kind, with required arguments
                coming before optional, which come before variadic.  There is no
                guarantee that the edges come from a single key, just that they match
                the observed type. */
                OrderedView match(const Object& value) const {
                    return {*this, value};
                }

                /* Return a range adaptor that iterates over the topologically-sorted
                types, and yields individual edges for those that match against an
                observed type and originate from the specified key, identified by its
                unique hash.  Rather than matching all possible edges, this view will
                essentially trace out the specified key, only checking edges that are
                contained within it. */
                HashView match(const Object& value, size_t hash) const {
                    return {*this, value, hash};
                }
            };

            /* A single node in the overload trie, which holds the topologically-sorted
            edge maps necessary for traversal, insertion, and deletion of candidate
            functions, as well as a (possibly null) terminal function to call if this
            node is the last in a given argument list. */
            struct Node {
                PyObject* func = nullptr;

                /* A sorted map of outgoing edges for positional arguments that can be
                given immediately after this node. */
                Edges positional;

                /* A map of keyword argument names to sorted maps of outgoing edges for
                the arguments that can follow this node.  A special empty string will
                be used to represent variadic keyword arguments, which can match any
                unrecognized names. */
                std::unordered_map<std::string_view, Edges> keyword;

                /* Recursively search for a matching function in this node's sub-trie.
                Returns a borrowed reference to a terminal function in the case of a
                match, or null if no match is found, which causes the algorithm to
                backtrack one level and continue searching.

                This method is only called after the first argument has been processed,
                which means the hash will remain stable over the course of the search.
                The mask, however, is a mutable out parameter that will be updated with
                all the edges that were followed to get here, so that the result can be
                easily compared to the required bitmask of the candidate hash, and
                keyword argument order can be normalized. */
                template <typename Container>
                [[nodiscard]] PyObject* search(
                    const Params<Container>& key,
                    size_t idx,
                    size_t hash,
                    uint64_t& mask
                ) const {
                    if (idx >= key.size()) {
                        return func;
                    }
                    const Param& param = key[idx];

                    // positional arguments have empty names
                    if (param.name.empty()) {
                        for (const Edge* edge : positional.match(param.value, hash)) {
                            size_t i = idx + 1;
                            if constexpr (Arguments::has_args) {
                                if (edge->kind.variadic()) {
                                    const Param* curr;
                                    while (
                                        i < key.size() &&
                                        (curr = &key[i])->pos() &&
                                        instance_check(
                                            curr->value,
                                            ptr(edge->type)
                                        )
                                    ) {
                                        ++i;
                                    }
                                    if (i < key.size() && curr->pos()) {
                                        continue;  // failed type check
                                    }
                                }
                            }
                            uint64_t temp_mask = mask | edge->mask;
                            PyObject* result = edge->node->search(
                                key,
                                i,
                                hash,
                                temp_mask
                            );
                            if (result) {
                                mask = temp_mask;
                                return result;
                            }
                        }

                    // keyword argument names must be looked up in the keyword map.  If
                    // the keyword name is not recognized, check for a variadic keyword
                    // argument under an empty string, and continue with that.
                    } else {
                        auto it = keyword.find(param.name);
                        if (
                            it != keyword.end() ||
                            (it = keyword.find("")) != keyword.end()
                        ) {
                            for (const Edge* edge : it->second.match(param.value, hash)) {
                                uint64_t temp_mask = mask | edge->mask;
                                PyObject* result = edge->node->search(
                                    key,
                                    idx + 1,
                                    hash,
                                    temp_mask
                                );
                                if (result) {
                                    // Keyword arguments can be given in any order, so
                                    // the return value may not always reflect the
                                    // deepest node.  To fix this, we compare the
                                    // incoming mask to the outgoing mask, and
                                    // substitute the result if this node comes later
                                    // in the original argument list.
                                    if (mask > edge->mask) {
                                        result = func;
                                    }
                                    mask = temp_mask;
                                    return result;
                                }
                            }
                        }
                    }

                    // return nullptr to backtrack
                    return nullptr;
                }

                /* Remove all outgoing edges that match a particular hash. */
                void remove(size_t hash) {
                    positional.remove(hash);

                    std::vector<std::string_view> dead_kw;
                    for (auto& [name, edges] : keyword) {
                        edges.remove(hash);
                        if (edges.empty()) {
                            dead_kw.emplace_back(name);
                        }
                    }
                    for (std::string_view name : dead_kw) {
                        keyword.erase(name);
                    }
                }

                /* Check to see if this node has any outgoing edges. */
                bool empty() const {
                    return positional.empty() && keyword.empty();
                }
            };

            std::shared_ptr<Node> root;
            std::set<const Metadata, std::less<>> data;
            mutable std::unordered_map<size_t, PyObject*> cache;

            /* Search the overload trie for a matching signature, as if calling the
            function.  An `isinstance()` check is performed on each parameter when
            searching the trie.

            This will recursively backtrack until a matching node is found or the trie
            is exhausted, returning nullptr on a failed search.  The results will be
            cached for subsequent invocations.  An error will be thrown if the key does
            not fully satisfy the enclosing parameter list.  Note that variadic
            parameter packs must be expanded prior to calling this function.

            The Python-level call and index operators for `py::Function<>` will
            immediately delegate to this function after constructing a key from the
            input arguments, so it will be called every time a C++ function is invoked
            from Python.  If it returns null, then the fallback implementation will be
            used instead.

            Returns a borrowed reference to the terminal function if a match is
            found within the trie, or null otherwise. */
            template <typename Container>
            [[nodiscard]] PyObject* search(const Params<Container>& key) const {
                auto it = cache.find(key.hash);
                if (it != cache.end()) {
                    return it->second;
                }
                assert_valid_args(key);
                size_t hash;
                PyObject* result = recursive_search(key, hash);
                cache[key.hash] = result;
                return result;
            }

            /* Search the overload trie for a matching signature, as if calling the
            function, suppressing errors caused by the signature not satisfying the
            enclosing parameter list.  An `isinstance()` check is performed on each
            parameter when searching the trie.

            This is equivalent to calling `search()` in a try/catch, but without any
            error handling overhead.  Errors are converted into a null optionals,
            separate from the null status of the wrapped pointer, which retains the
            same semantics as `search()`.

            This is used by the Python-level `__getitem__` and `__contains__` operators
            for `py::Function<>` instances, which converts a null optional result into
            `None` on the Python side.  Otherwise, it will return the function that
            would be invoked if the function were to be called with the given
            arguments, which may be a self-reference if the fallback implementation is
            selected.

            Returns a borrowed reference to the terminal function if a match is
            found. */
            template <typename Container>
            [[nodiscard]] std::optional<PyObject*> get(const Params<Container>& key) const {
                auto it = cache.find(key.hash);
                if (it != cache.end()) {
                    return it->second;
                }

                uint64_t mask = 0;
                for (size_t i = 0, n = key.size(); i < n; ++i) {
                    const Param& param = key[i];
                    if (param.name.empty()) {
                        const Callback& callback = Arguments::callback(i);
                        if (!callback || !callback.isinstance(param.value)) {
                            return std::nullopt;
                        }
                        mask |= callback.mask;
                    } else {
                        const Callback& callback = Arguments::callback(param.name);
                        if (
                            !callback ||
                            (mask & callback.mask) ||
                            !callback.isinstance(param.value)
                        ) {
                            return std::nullopt;
                        }
                        mask |= callback.mask;
                    }
                }
                if ((mask & required) != required) {
                    return std::nullopt;
                }

                size_t hash;
                PyObject* result = recursive_search(key, hash);
                cache[key.hash] = result;
                return result;
            }

            /* Filter the overload trie for a given first positional argument, which
            represents the an implicit `self` parameter for a bound member function.
            Returns a range adaptor that extracts only the matching functions from the
            metadata set, with extra information encoding their full path through the
            overload trie. */
            [[nodiscard]] BoundView match(const Object& value) const {
                return {*this, value};
            }

            /* Insert a function into the overload trie, throwing a TypeError if it
            does not conform to the enclosing parameter list or if it conflicts with
            another node in the trie. */
            template <typename Container>
            void insert(const Params<Container>& key, const Object& func) {
                // assert the key minimally satisfies the enclosing parameter list
                []<size_t... Is>(std::index_sequence<Is...>, const Params<Container>& key) {
                    size_t idx = 0;
                    (assert_viable_overload<Is>(key, idx), ...);
                }(std::make_index_sequence<Arguments::n>{}, key);

                // construct the root node if it doesn't already exist
                if (root == nullptr) {
                    root = std::make_shared<Node>();
                }

                // if the key is empty, then the root node is the terminal node
                if (key.empty()) {
                    if (root->func) {
                        throw TypeError("overload already exists");
                    }
                    root->func = ptr(func);
                    data.emplace(key.hash, 0, func, {});
                    cache.clear();
                    return;
                }

                // insert an edge linking each parameter in the key
                std::vector<Edge> path;
                path.reserve(key.size());
                Node* curr = root.get();
                int first_keyword = -1;
                int last_required = 0;
                uint64_t required = 0;
                for (int i = 0, end = key.size(); i < end; ++i) {
                    try {
                        const Param& param = key[i];
                        path.emplace_back(
                            key.hash,
                            1ULL << i,
                            param.name,
                            param.value,
                            param.kind,
                            nullptr
                        );
                        if (param.posonly()) {
                            curr->positional.insert(path.back());
                            if (!param.opt()) {
                                ++first_keyword;
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.pos()) {
                            curr->positional.insert(path.back());
                            auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                            it->second.insert(path.back(), path.back().node);
                            if (!param.opt()) {
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.kw()) {
                            auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                            it->second.insert(path.back());
                            if (!param.opt()) {
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.args()) {
                            curr->positional.insert(path.back());
                        } else if (param.kwargs()) {
                            auto [it, _] = curr->keyword.try_emplace("", Edges{});
                            it->second.insert(path.back());
                        } else {
                            throw ValueError("invalid argument kind");
                        }
                        curr = path.back().node.get();

                    } catch (...) {
                        curr = root.get();
                        for (int j = 0; j < i; ++j) {
                            const Edge& edge = path[j];
                            curr->remove(edge.hash);
                            curr = edge.node.get();
                        }
                        if (root->empty()) {
                            root.reset();
                        }
                        throw;
                    }
                }

                // backfill the terminal functions and full keyword maps for each node
                try {
                    std::string_view name;
                    int start = key.size() - 1;
                    for (int i = start; i > first_keyword; --i) {
                        Edge& edge = path[i];
                        if (i >= last_required) {
                            if (edge.node->func) {
                                throw TypeError("overload already exists");
                            }
                            edge.node->func = ptr(func);
                        }
                        for (int j = first_keyword; j < key.size(); ++j) {
                            Edge& kw = path[j];
                            if (
                                kw.posonly() ||
                                kw.args() ||
                                kw.name == edge.name ||  // incoming edge
                                (i < start && kw.name == name)  // outgoing edge
                            ) {
                                continue;
                            }
                            auto& [it, _] = edge.node->keyword.try_emplace(
                                kw.name,
                                Edges{}
                            );
                            it->second.insert(kw, kw.node);
                        }
                        name = edge.name;
                    }

                    // extend backfill to the root node
                    if (!required) {
                        if (root->func) {
                            throw TypeError("overload already exists");
                        }
                        root->func = ptr(func);
                    }
                    bool extend_keywords = true;
                    for (Edge& edge : path) {
                        if (!edge.posonly()) {
                            break;
                        } else if (!edge.opt()) {
                            extend_keywords = false;
                            break;
                        }
                    }
                    if (extend_keywords) {
                        for (int j = first_keyword; j < key.size(); ++j) {
                            Edge& kw = path[j];
                            if (kw.posonly() || kw.args()) {
                                continue;
                            }
                            auto& [it, _] = root->keyword.try_emplace(
                                kw.name,
                                Edges{}
                            );
                            it->second.insert(kw, kw.node);
                        }
                    }

                } catch (...) {
                    Node* curr = root.get();
                    for (int i = 0, end = key.size(); i < end; ++i) {
                        const Edge& edge = path[i];
                        curr->remove(edge.hash);
                        if (i >= last_required) {
                            edge.node->func = nullptr;
                        }
                        curr = edge.node.get();
                    }
                    if (root->empty()) {
                        root.reset();
                    }
                    throw;
                }

                // track the function and required arguments for the inserted key
                data.emplace(key.hash, required, func, std::move(path));
                cache.clear();
            }

            /* Remove a node from the overload trie and prune any dead-ends that lead
            to it.  Returns the function that was removed, or None if no matching
            function was found. */
            template <typename Container>
            [[maybe_unused]] Object remove(const Params<Container>& key) {
                // assert the key minimally satisfies the enclosing parameter list
                assert_valid_args(key);

                size_t hash;
                Object result = reinterpret_borrow<Object>(
                    recursive_search(key, hash)
                );
                if (result.is(nullptr)) {
                    return None;
                }
                if (key.empty()) {
                    for (const Metadata& data : this->data) {
                        if (!data.required) {
                            hash = data.hash;
                            break;
                        }
                    }
                }

                const Metadata& metadata = *(data.find(hash));
                Node* curr = root.get();
                for (const Edge& edge : metadata.path) {
                    curr->remove(hash);
                    if (edge.node->func == ptr(result)) {
                        edge.node->func = nullptr;
                    }
                    curr = edge.node.get();
                }
                if (root->func == ptr(result)) {
                    root->func = nullptr;
                }
                data.erase(hash);
                if (data.empty()) {
                    root.reset();
                }
                return result;
            }

            /* Clear the overload trie, removing all tracked functions. */
            void clear() {
                cache.clear();
                root.reset();
                data.clear();
            }

            /* Manually reset the function's overload cache, forcing overload paths to
            be recalculated on subsequent calls. */
            void flush() {
                cache.clear();
            }

        private:

            /* A range adaptor that iterates over the space of overloads that follow a
            given `self` argument, which is used to prune the trie.  When a bound
            method is created, it will use one of these views to correctly forward the
            overload interface. */
            struct BoundView {
                const Overloads& self;
                Object value;

                struct Iterator {
                    using iterator_category = std::input_iterator_tag;
                    using difference_type = std::ptrdiff_t;
                    using value_type = const Metadata;
                    using pointer = value_type*;
                    using reference = value_type&;

                    const Overloads& self;
                    const Metadata* curr;
                    Edges::OrderedView view;
                    std::ranges::iterator_t<typename Edges::OrderedView> it;
                    std::ranges::sentinel_t<typename Edges::OrderedView> end;
                    std::unordered_set<size_t> visited;

                    Iterator(const Overloads& self, const Object& value) :
                        self(self),
                        curr(nullptr),
                        view(self.root->positional.match(value)),
                        it(std::ranges::begin(this->view)),
                        end(std::ranges::end(this->view))
                    {
                        if (it != end) {
                            curr = self.data.find((*it)->hash);
                            visited.emplace(curr->hash);
                        }
                    }

                    Iterator& operator++() {
                        while (++it != end) {
                            const Edge* edge = *it;
                            auto lookup = visited.find(edge->hash);
                            if (lookup == visited.end()) {
                                visited.emplace(edge->hash);
                                curr = &*(self.data.find(edge->hash));
                                return *this;
                            }
                        }
                        return *this;
                    }

                    const Metadata& operator*() const {
                        return *curr;
                    }

                    friend bool operator==(
                        const Iterator& iter,
                        const Sentinel& sentinel
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator==(
                        const Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it == iter.end;
                    }

                    friend bool operator!=(
                        const Iterator& iter,
                        const Sentinel& sentinel
                    ) {
                        return iter.it != iter.end;
                    }

                    friend bool operator!=(
                        const Sentinel& sentinel,
                        const Iterator& iter
                    ) {
                        return iter.it != iter.end;
                    }
                };

                Iterator begin() const {
                    return {self, value};
                }

                Sentinel end() const {
                    return {};
                }
            };

            template <typename Container>
            static void assert_valid_args(const Params<Container>& key) {
                uint64_t mask = 0;
                for (size_t i = 0, n = key.size(); i < n; ++i) {
                    const Param& param = key[i];
                    if (param.name.empty()) {
                        const Callback& callback = Arguments::callback(i);
                        if (!callback) {
                            throw TypeError(
                                "received unexpected positional argument at index " +
                                std::to_string(i)
                            );
                        }
                        if (!callback.isinstance(param.value)) {
                            throw TypeError(
                                "expected positional argument at index " +
                                std::to_string(i) + " to be a subclass of '" +
                                repr(callback.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                        mask |= callback.mask;
                    } else if (param.kw()) {
                        const Callback& callback = Arguments::callback(param.name);
                        if (!callback) {
                            throw TypeError(
                                "received unexpected keyword argument: '" +
                                std::string(param.name) + "'"
                            );
                        }
                        if (mask & callback.mask) {
                            throw TypeError(
                                "received multiple values for argument '" +
                                std::string(param.name) + "'"
                            );
                        }
                        if (!callback.isinstance(param.value)) {
                            throw TypeError(
                                "expected argument '" + std::string(param.name) +
                                "' to be a subclass of '" +
                                repr(callback.type()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                        mask |= callback.mask;
                    }
                }
                if ((mask & Arguments::required) != Arguments::required) {
                    uint64_t missing = Arguments::required & ~(mask & Arguments::required);
                    std::string msg = "missing required arguments: [";
                    size_t i = 0;
                    while (i < n) {
                        if (missing & (1ULL << i)) {
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
                    while (i < n) {
                        if (missing & (1ULL << i)) {
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

            template <size_t I, typename Container>
            static void assert_viable_overload(const Params<Container>& key, size_t& idx) {
                using T = at<I>;
                using Expected = std::remove_cvref_t<python_type<
                    typename ArgTraits<at<I>>::type
                >>;
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

                if constexpr (ArgTraits<T>::posonly()) {
                    if (idx >= key.size()) {
                        if (ArgTraits<T>::name.empty()) {
                            throw TypeError(
                                "missing positional-only argument at index " +
                                std::to_string(idx)
                            );
                        } else {
                            throw TypeError(
                                "missing positional-only argument '" +
                                ArgTraits<T>::name + "' at index " +
                                std::to_string(idx)
                            );
                        }
                    }
                    const Param& param = key[idx];
                    if (!param.posonly()) {
                        if (ArgTraits<T>::name.empty()) {
                            throw TypeError(
                                "expected positional-only argument at index " +
                                std::to_string(idx) + ", not " + description(param)
                            );
                        } else {
                            throw TypeError(
                                "expected argument '" + ArgTraits<T>::name +
                                "' at index " + std::to_string(idx) +
                                " to be positional-only, not " + description(param)
                            );
                        }
                    }
                    if (!ArgTraits<T>::name.empty() && param.name != ArgTraits<T>::name) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<T>::opt() && param.opt()) {
                        if (ArgTraits<T>::name.empty()) {
                            throw TypeError(
                                "required positional-only argument at index " +
                                std::to_string(idx) + " must not have a default "
                                "value"
                            );
                        } else {
                            throw TypeError(
                                "required positional-only argument '" +
                                ArgTraits<T>::name + "' at index " +
                                std::to_string(idx) + " must not have a default "
                                "value"
                            );
                        }
                    }
                    if (!issubclass<Expected>(param.value)) {
                        if (ArgTraits<T>::name.empty()) {
                            throw TypeError(
                                "expected positional-only argument at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(Type<Expected>()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        } else {
                            throw TypeError(
                                "expected positional-only argument '" +
                                ArgTraits<T>::name + "' at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(Type<Expected>()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                    }
                    ++idx;

                } else if constexpr (ArgTraits<T>::pos()) {
                    if (idx >= key.size()) {
                        throw TypeError(
                            "missing positional-or-keyword argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx)
                        );
                    }
                    const Param& param = key[idx];
                    if (!param.pos() || !param.kw()) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) +
                            " to be positional-or-keyword, not " + description(param)
                        );
                    }
                    if (param.name != ArgTraits<T>::name) {
                        throw TypeError(
                            "expected positional-or-keyword argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<T>::opt() && param.opt()) {
                        throw TypeError(
                            "required positional-or-keyword argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx) + " must not have a default value"
                        );
                    }
                    if (!issubclass<Expected>(param.value)) {
                        throw TypeError(
                            "expected positional-or-keyword argument '" +
                            ArgTraits<T>::name + "' at index " +
                            std::to_string(idx) + " to be a subclass of '" +
                            repr(Type<Expected>()) + "', not: '" +
                            repr(param.value) + "'"
                        );
                    }
                    ++idx;

                } else if constexpr (ArgTraits<T>::kw()) {
                    if (idx >= key.size()) {
                        throw TypeError(
                            "missing keyword-only argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx)
                        );
                    }
                    const Param& param = key[idx];
                    if (!param.kwonly()) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) +
                            " to be keyword-only, not " + description(param)
                        );
                    }
                    if (param.name != ArgTraits<T>::name) {
                        throw TypeError(
                            "expected keyword-only argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<T>::opt() && param.opt()) {
                        throw TypeError(
                            "required keyword-only argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) + " must not have a "
                            "default value"
                        );
                    }
                    if (!issubclass<Expected>(param.value)) {
                        throw TypeError(
                            "expected keyword-only argument '" + ArgTraits<T>::name +
                            "' at index " + std::to_string(idx) +
                            " to be a subclass of '" +
                            repr(Type<Expected>()) + "', not: '" +
                            repr(param.value) + "'"
                        );
                    }
                    ++idx;

                } else if constexpr (ArgTraits<T>::args()) {
                    while (idx < key.size()) {
                        const Param& param = key[idx];
                        if (!(param.pos() || param.args())) {
                            break;
                        }
                        if (!issubclass<Expected>(param.value)) {
                            if (param.name.empty()) {
                                throw TypeError(
                                    "expected variadic positional argument at index " +
                                    std::to_string(idx) + " to be a subclass of '" +
                                    repr(Type<Expected>()) + "', not: '" +
                                    repr(param.value) + "'"
                                );
                            } else {
                                throw TypeError(
                                    "expected variadic positional argument '" +
                                    std::string(param.name) + "' at index " +
                                    std::to_string(idx) + " to be a subclass of '" +
                                    repr(Type<Expected>()) + "', not: '" +
                                    repr(param.value) + "'"
                                );
                            }
                        }
                        ++idx;
                    }

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    while (idx < key.size()) {
                        const Param& param = key[idx];
                        if (!(param.kw() || param.kwargs())) {
                            break;
                        }
                        if (!issubclass<Expected>(param.value)) {
                            throw TypeError(
                                "expected variadic keyword argument '" +
                                std::string(param.name) + "' at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(Type<Expected>()) + "', not: '" +
                                repr(param.value) + "'"
                            );
                        }
                        ++idx;
                    }

                } else {
                    static_assert(false, "invalid argument kind");
                }
            }

            template <typename Container>
            PyObject* recursive_search(const Params<Container>& key, size_t& hash) const {
                // account for empty root node and/or key
                if (!root) {
                    return nullptr;
                } else if (key.empty()) {
                    return root->func;  // may be null
                }

                // The hash is ambiguous for the first argument, so we need to test all
                // edges in order to find a matching key. Otherwise, we already know
                // which key we're tracing, so we can restrict our search to exact
                // matches.  This maintains consistency in the final bitmasks, since
                // each recursive call will only search along a single path after the
                // first edge has been identified.
                const Param& param = key[0];

                // positional arguments have empty names
                if (param.name.empty()) {
                    for (const Edge* edge : root->positional.match(param.value)) {
                        size_t i = 1;
                        size_t candidate = edge->hash;
                        uint64_t mask = edge->mask;
                        if constexpr (Arguments::has_args) {
                            if (edge->kind.variadic()) {
                                const Param* curr;
                                while (
                                    i < key.size() &&
                                    (curr = &key[i])->pos() &&
                                    instance_check(
                                        curr->value,
                                        ptr(edge->type)
                                    )
                                ) {
                                    ++i;
                                }
                                if (i < key.size() && curr->pos()) {
                                    continue;  // failed type check on positional arg
                                }
                            }
                        }
                        PyObject* result = edge->node->search(key, i, candidate, mask);
                        if (result) {
                            const Metadata& metadata = *(data.find(candidate));
                            if ((mask & metadata.required) == metadata.required) {
                                hash = candidate;
                                return result;
                            }
                        }
                    }

                // keyword argument names must be looked up in the keyword map.  If
                // the keyword name is not recognized, check for a variadic keyword
                // argument under an empty string, and continue with that.
                } else {
                    auto it = root->keyword.find(param.name);
                    if (
                        it != root->keyword.end() ||
                        (it = root->keyword.find("")) != root->keyword.end()
                    ) {
                        for (const Edge* edge : it->second.match(param.value)) {
                            size_t candidate = edge->hash;
                            uint64_t mask = edge->mask;
                            PyObject* result = edge->node->search(
                                key,
                                1,
                                candidate,
                                mask
                            );
                            if (result) {
                                const Metadata& metadata = *(data.find(candidate));
                                if ((mask & metadata.required) == metadata.required) {
                                    hash = candidate;
                                    return result;
                                }
                            }
                        }
                    }
                }

                // if all matching edges have been exhausted, then there is no match
                return nullptr;
            }
        };

        /* A helper that binds C++ arguments to the enclosing signature and performs
        the necessary translation to invoke a matching C++ or Python function. */
        template <typename... Values>
        struct Bind {
        private:
            using Bound = Arguments<Values...>;

            template <size_t I> requires (I < Arguments::n)
            using Target = Arguments::template at<I>;
            template <size_t J> requires (J < Bound::n)
            using Source = Bound::template at<J>;

            /* Upon encountering a variadic positional pack in the target signature,
            recursively traverse the remaining source positional arguments and ensure
            that each is convertible to the target type. */
            template <size_t, typename>
            static constexpr bool consume_target_args = true;
            template <size_t J, typename T>
                requires (J < Bound::kw_idx && J < Bound::kwargs_idx)
            static constexpr bool consume_target_args<J, T> = std::convertible_to<
                typename ArgTraits<Source<J>>::type,
                typename ArgTraits<T>::type
            > && consume_target_args<J + 1, T>;

            /* Upon encountering a variadic keyword pack in the target signature,
            recursively traverse the source keyword arguments and extract any that
            aren't present in the target signature, ensuring they are convertible to
            the target type. */
            template <size_t, typename>
            static constexpr bool consume_target_kwargs = true;
            template <size_t J, typename T> requires (J < Bound::n)
            static constexpr bool consume_target_kwargs<J, T> = (
                Arguments::template has<ArgTraits<Source<J>>::name> ||
                std::convertible_to<
                    typename ArgTraits<Source<J>>::type,
                    typename ArgTraits<T>::type
                >
            ) && consume_target_kwargs<J + 1, T>;

            /* Upon encountering a variadic positional pack in the source signature,
            recursively traverse the target positional arguments and ensure that the source
            type is convertible to each target type. */
            template <size_t, typename>
            static constexpr bool consume_source_args = true;
            template <size_t I, typename S> requires (I < kwonly_idx && I < kwargs_idx)
            static constexpr bool consume_source_args<I, S> = std::convertible_to<
                typename ArgTraits<S>::type,
                typename ArgTraits<Target<I>>::type
            > && consume_source_args<I + 1, S>;

            /* Upon encountering a variadic keyword pack in the source signature,
            recursively traverse the target keyword arguments and extract any that aren't
            present in the source signature, ensuring that the source type is convertible
            to each target type. */
            template <size_t, typename>
            static constexpr bool consume_source_kwargs = true;
            template <size_t I, typename S> requires (I < Arguments::n)
            static constexpr bool consume_source_kwargs<I, S> = (
                Bound::template has<ArgTraits<Target<I>>::name> ||
                std::convertible_to<
                    typename ArgTraits<S>::type,
                    typename ArgTraits<Target<I>>::type
                >
            ) && consume_source_kwargs<I + 1, S>;

            /* Recursively check whether the source arguments conform to Python calling
            conventions (i.e. no positional arguments after a keyword, no duplicate
            keywords, etc.), fully satisfy the target signature, and are convertible to
            the expected types, after accounting for parameter packs in both
            signatures. */
            template <size_t I, size_t J>
            static consteval bool enable_recursive() {
                // both lists are satisfied
                if constexpr (I >= Arguments::n && J >= Bound::n) {
                    return true;

                // there are extra source arguments
                } else if constexpr (I >= Arguments::n) {
                    return false;

                // there are extra target arguments
                } else if constexpr (J >= Bound::n) {
                    using T = Target<I>;
                    if constexpr (ArgTraits<T>::opt() || ArgTraits<T>::args()) {
                        return enable_recursive<I + 1, J>();
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<Bound::kw_idx, T>;
                    } else {
                        return false;  // a required argument is missing
                    }

                // both lists have arguments remaining
                } else {
                    using T = Target<I>;
                    using S = Source<J>;

                    // ensure target arguments are present and do not conflict + expand
                    // parameter packs over source arguments
                    if constexpr (ArgTraits<T>::posonly()) {
                        constexpr bool duplicate_name = (
                            ArgTraits<T>::name != "" &&
                            Bound::template has<ArgTraits<T>::name>
                        );
                        constexpr bool missing = (
                            !ArgTraits<T>::opt() &&
                            (ArgTraits<S>::kw() || ArgTraits<S>::kwargs())
                        );
                        if constexpr (duplicate_name || missing) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::pos()) {
                        constexpr bool duplicate_name = (
                            ArgTraits<T>::name != "" &&
                            (ArgTraits<S>::posonly() || ArgTraits<S>::args()) &&
                            Bound::template has<ArgTraits<T>::name>
                        );
                        constexpr bool missing = (
                            !ArgTraits<T>::opt() &&
                            (ArgTraits<S>::kw() || ArgTraits<S>::kwargs()) &&
                            !Bound::template has<ArgTraits<T>::name>
                        );
                        if constexpr (duplicate_name || missing) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::kw()) {
                        constexpr bool extra_positional = (
                            ArgTraits<S>::posonly() || ArgTraits<S>::args()
                        );
                        constexpr bool missing = (
                            !ArgTraits<T>::opt() &&
                            !Bound::template has<ArgTraits<T>::name>
                        );
                        if constexpr (extra_positional || missing) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return consume_target_args<J, T> && enable_recursive<
                            I + 1,
                            Bound::has_kw ? Bound::kw_idx : Bound::kwargs_idx
                        >();  // skip ahead to source keywords
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<
                            Bound::has_kw ? Bound::kw_idx : Bound::kwargs_idx,
                            T
                        >;  // end of expression
                    } else {
                        static_assert(false);
                        return false;  // not reachable
                    }

                    // ensure source arguments match targets & expand parameter packs
                    // over target arguments.
                    if constexpr (ArgTraits<S>::posonly()) {
                        constexpr bool not_convertible = !std::convertible_to<
                            typename ArgTraits<S>::type,
                            typename ArgTraits<T>::type
                        >;
                        if constexpr (not_convertible) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<S>::kw()) {
                        constexpr StaticStr name = ArgTraits<S>::name;
                        if constexpr (Arguments::template has<name>) {
                            constexpr size_t idx = Arguments::template idx<name>;
                            constexpr bool not_convertible = !std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<Target<idx>>::type
                            >;
                            if constexpr (not_convertible) {
                                return false;
                            }
                        } else if constexpr (Arguments::has_kwargs) {
                            constexpr bool not_convertible = !std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<Target<Arguments::kwargs_idx>>::type
                            >;
                            if constexpr (not_convertible) {
                                return false;
                            }
                        } else {
                            return false;
                        }
                    } else if constexpr (ArgTraits<S>::args()) {
                        return consume_source_args<I, S> && enable_recursive<
                            Arguments::has_kwonly ?
                                Arguments::kwonly_idx : Arguments::kwargs_idx,
                            J + 1
                        >();  // skip to target keywords
                    } else if constexpr (ArgTraits<S>::kwargs()) {
                        return consume_source_kwargs<I, S>;  // end of expression
                    } else {
                        static_assert(false);
                        return false;  // not reachable
                    }

                    // advance to next argument pair
                    return enable_recursive<I + 1, J + 1>();
                }
            }

            /* Recursively check whether the source arguments conform to Python calling
            conventions (i.e. no positional arguments after a keyword, no duplicate
            keywords, etc.), partially match the target signature, and are convertible
            to the expected types, after accounting for parameter packs in both
            signatures. */
            template <size_t I, size_t J>
            static consteval bool partial_recursive() {
                // both lists are satisfied
                if constexpr (I >= Arguments::n && J >= Bound::n) {
                    return true;

                // there are extra source arguments
                } else if constexpr (I >= Arguments::n) {
                    return false;

                // there are extra unmatched target arguments
                } else if constexpr (J >= Bound::n) {
                    using T = Target<I>;
                    if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<Bound::kw_idx, T>;
                    } else {
                        return partial_recursive<I + 1, J>();
                    }

                // both lists have arguments remaining
                } else {
                    using T = Target<I>;
                    using S = Source<J>;

                    // ensure target arguments do not conflict + expand parameter packs
                    // over source arguments
                    if constexpr (ArgTraits<T>::posonly()) {
                        constexpr bool duplicate_name = (
                            ArgTraits<T>::name != "" &&
                            Bound::template has<ArgTraits<T>::name>
                        );
                        if constexpr (duplicate_name) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::pos()) {
                        constexpr bool duplicate_name = (
                            ArgTraits<T>::name != "" &&
                            (ArgTraits<S>::posonly() || ArgTraits<S>::args()) &&
                            Bound::template has<ArgTraits<T>::name>
                        );
                        if constexpr (duplicate_name) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::kw()) {
                        constexpr bool extra_positional = (
                            ArgTraits<S>::posonly() || ArgTraits<S>::args()
                        );
                        if constexpr (extra_positional) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return consume_target_args<J, T> && partial_recursive<
                            I + 1,
                            Bound::has_kw ? Bound::kw_idx : Bound::kwargs_idx
                        >();  // skip ahead to source keywords
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<
                            Bound::has_kw ? Bound::kw_idx : Bound::kwargs_idx,
                            T
                        >;  // end of expression
                    } else {
                        static_assert(false);
                        return false;  // not reachable
                    }

                    // ensure source arguments match targets & expand parameter packs
                    // over target arguments.
                    if constexpr (ArgTraits<S>::posonly()) {
                        constexpr bool not_convertible = !std::convertible_to<
                            typename ArgTraits<S>::type,
                            typename ArgTraits<T>::type
                        >;
                        if constexpr (not_convertible) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::kw()) {
                        constexpr StaticStr name = ArgTraits<S>::name;
                        if constexpr (Arguments::template has<name>) {
                            constexpr size_t idx = Arguments::template idx<name>;
                            constexpr bool not_convertible = !std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<Target<idx>>::type
                            >;
                            if constexpr (not_convertible) {
                                return false;
                            }
                        } else if constexpr (Arguments::has_kwargs) {
                            constexpr bool not_convertible = !std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<Target<Arguments::kwargs_idx>>::type
                            >;
                            if constexpr (not_convertible) {
                                return false;
                            }
                        } else {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return consume_source_args<I, S> && partial_recursive<
                            Arguments::has_kwonly ?
                                Arguments::kwonly_idx : Arguments::kwargs_idx,
                            J + 1
                        >();  // skip to target keywords
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_source_kwargs<I, S>;  // end of expression
                    } else {
                        static_assert(false);
                        return false;  // not reachable
                    }

                    // advance to next argument pair
                    return partial_recursive<I + 1, J + 1>();
                }
            }

            /* Produce an overload key from the bound arguments, converting them to
            Python. */
            template <size_t J>
            static Param _key(size_t& hash, Values... values) {
                using S = Source<J>;
                Param param = {
                    .name = ArgTraits<S>::name,
                    .value = to_python(
                        impl::unpack_arg<J>(std::forward<Values>(values)...)
                    ),
                    .kind = ArgTraits<S>::kind
                };
                hash = hash_combine(hash, param.hash(seed, prime));
                return param;
            }

            template <typename...>
            static constexpr size_t _n_foreign_kwargs = 0;
            template <typename T, typename... Ts>
            static constexpr size_t _n_foreign_kwargs<T, Ts...> =
                _n_foreign_kwargs<Ts...>;
            template <typename T, typename... Ts> requires (ArgTraits<T>::kw())
            static constexpr size_t _n_foreign_kwargs<T, Ts...> =
                _n_foreign_kwargs<Ts...> +
                (Arguments::template has<ArgTraits<T>::name>);

            /* Get the base size of a **kwargs map by counting the C++ keyword
            arguments that are not in the target signature at compile time. */
            static constexpr size_t n_foreign_kwargs = _n_foreign_kwargs<Values...>;

            /* Initialize a **kwargs map in the target arguments by collecting all
            keyword arguments that don't appear in the target parameter list.  The
            result can then be moved into the **kwargs argument annotation. */
            template <size_t J, typename T>
            static constexpr void build_kwargs(
                std::unordered_map<std::string, T>& map,
                Values... args
            ) {
                constexpr size_t idx = Bound::kw_idx + J;
                if constexpr (!Arguments::template has<ArgTraits<Source<idx>>::name>) {
                    map.emplace(
                        ArgTraits<Source<idx>>::name,
                        impl::unpack_arg<idx>(std::forward<Source>(args)...)
                    );
                }
            }

            /* The cpp() method is used to convert an index sequence over the enclosing
             * signature into the corresponding values pulled from either the call site
             * or the function's defaults.  It is complicated by the presence of
             * variadic parameter packs in both the target signature and the call
             * arguments, which have to be handled as a cross product of possible
             * combinations.
             *
             * Unless a variadic parameter pack is given at the call site, all of these
             * are resolved entirely at compile time by reordering the arguments using
             * template recursion.  However, because the size of a variadic parameter
             * pack cannot be determined at compile time, calls that use these will have
             * to extract values at runtime, and may therefore raise an error if a
             * corresponding value does not exist in the parameter pack, or if there are
             * extras that are not included in the target signature.
             */

            template <size_t I>
            static constexpr Target<I> cpp(const Defaults& defaults, Values... values) {
                using T = Target<I>;
                constexpr StaticStr name = ArgTraits<T>::name;
                constexpr size_t transition = std::min(Bound::kw_idx, Bound::kwargs_idx);

                if constexpr (ArgTraits<T>::posonly()) {
                    if constexpr (I < transition) {
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else {
                        return to_arg<I>(
                            defaults.template get<I>()
                        );
                    }

                } else if constexpr (ArgTraits<T>::pos()) {
                    if constexpr (I < transition) {
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else if constexpr (Bound::template has<name>) {
                        return to_arg<I>(
                            impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            )
                        );
                    } else {
                        return to_arg<I>(defaults.template get<I>());
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (Bound::template has<name>) {
                        return to_arg<I>(
                            impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            )
                        );
                    } else {
                        return to_arg<I>(defaults.template get<I>());
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    constexpr size_t diff = I < transition ? transition - I : 0;
                    return []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Values&&... args
                    ) {
                        std::vector<typename ArgTraits<T>::type> vec;
                        vec.reserve(diff);
                        (vec.emplace_back(impl::unpack_arg<I + Js>(
                            std::forward<Values>(args)...
                        )), ...);
                        return to_arg<I>(std::move(vec));
                    }(
                        std::make_index_sequence<diff>{},
                        std::forward<Values>(values)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Values&&... args
                    ) {
                        std::unordered_map<std::string, typename ArgTraits<T>::type> map;
                        map.reserve(n_foreign_kwargs);
                        (build_kwargs<Js>(map, std::forward<Values>(args)...), ...);
                        return to_arg<I>(std::move(map));
                    }(
                        std::make_index_sequence<Bound::n - Bound::kw_idx>{},
                        std::forward<Values>(values)...
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <size_t I, typename Pos>
            static constexpr Target<I> cpp(
                const Defaults& defaults,
                PositionalPack<Arguments, Pos>& positional,
                Values... values
            ) {
                using T = Target<I>;
                constexpr StaticStr name = ArgTraits<T>::name;
                constexpr size_t transition = std::min(Bound::kw_idx, Bound::kwargs_idx);

                if constexpr (ArgTraits<T>::posonly()) {
                    if constexpr (I < transition) {
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else {
                        if (positional.has_value()) {
                            return to_arg<I>(positional.value());
                        }
                        if constexpr (ArgTraits<T>::opt()) {
                            return to_arg<I>(defaults.template get<I>());
                        } else {
                            throw TypeError(
                                "no match for positional-only parameter at index " +
                                std::to_string(I)
                            );
                        }
                    }

                } else if constexpr (ArgTraits<T>::pos()) {
                    if constexpr (I < transition) {
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else {
                        if (positional.has_value()) {
                            if constexpr (Bound::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                return to_arg<I>(positional.value());
                            }
                        }
                        if constexpr (Bound::template has<name>) {
                            return to_arg<I>(
                                impl::unpack_arg<Bound::template idx<name>>(
                                    std::forward<Values>(values)...
                                )
                            );
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return to_arg<I>(defaults.template get<I>());
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    return cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::args()) {
                    constexpr size_t diff = I < transition ? transition - I : 0;
                    return []<size_t... Js>(
                        std::index_sequence<Js...>,
                        PositionalPack<Arguments, Pos>& positional,
                        Values&&... args
                    ) {
                        std::vector<typename ArgTraits<T>::type> vec;
                        vec.reserve(diff + (positional.size - positional.consumed));
                        (vec.emplace_back(impl::unpack_arg<I + Js>(
                            std::forward<Values>(args)...
                        )), ...);
                        vec.insert(vec.end(), positional.begin, positional.end);
                        return to_arg<I>(std::move(vec));
                    }(
                        std::make_index_sequence<diff>{},
                        positional,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return cpp<I>(defaults, std::forward<Values>(values)...);

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <size_t I, typename Kw>
            static constexpr Target<I> cpp(
                const Defaults& defaults,
                KeywordPack<Arguments, Kw>& keyword,
                Values... values
            ) {
                using T = Target<I>;
                constexpr StaticStr name = ArgTraits<T>::name;
                constexpr size_t transition = std::min(Bound::kw_idx, Bound::kwargs_idx);

                if constexpr (ArgTraits<T>::posonly()) {
                    return cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::pos()) {
                    auto node = keyword.extract(name);
                    if (node) {
                        if constexpr (I < transition || Bound::template has<name>) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        } else if constexpr (std::is_lvalue_reference_v<
                            typename ArgTraits<T>::type
                        >) {
                            return to_arg<I>(node.mapped());
                        } else {
                            return to_arg<I>(std::move(node.mapped()));
                        }
                    }
                    if constexpr (I < transition) {
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else if constexpr (Bound::template has<name>) {
                        return to_arg<I>(
                            impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            )
                        );
                    } else if constexpr (ArgTraits<T>::opt()) {
                        return to_arg<I>(defaults.template get<I>());
                    } else {
                        throw TypeError(
                            "no match for parameter '" + name +
                            "' at index " + std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto node = keyword.extract(name);
                    if (node) {
                        if constexpr (Bound::template has<name>) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        } else if constexpr (std::is_lvalue_reference_v<
                            typename ArgTraits<T>::type
                        >) {
                            return to_arg<I>(node.mapped());
                        } else {
                            return to_arg<I>(std::move(node.mapped()));
                        }
                    }
                    if constexpr (Bound::template has<name>) {
                        return to_arg<I>(
                            impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            )
                        );
                    } else if constexpr (ArgTraits<T>::opt()) {
                        return to_arg<I>(defaults.template get<I>());
                    } else {
                        throw TypeError(
                            "no match for parameter '" + name +
                            "' at index " + std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    return cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return []<size_t... Js>(
                        std::index_sequence<Js...>,
                        KeywordPack<Arguments, Kw>& keyword,
                        Values&&... values
                    ) {
                        std::unordered_map<std::string, typename ArgTraits<T>::type> map;
                        map.reserve(n_foreign_kwargs + keyword.size());
                        (build_kwargs<Js>(map, std::forward<Values>(values)...), ...);
                        /// NOTE: since the pack's .extract() method is destructive,
                        /// the only remaining elements are those that were not
                        /// consumed, which can be directly moved into the result
                        for (auto it = keyword.begin(); it != keyword.end();) {
                            // postfix ++ required to increment before invalidation
                            auto node = keyword.extract(it++);
                            auto rc = map.insert(node);
                            if (!rc.inserted) {
                                throw TypeError(
                                    "duplicate value for parameter '" + key + "'"
                                );
                            }
                        }
                        return to_arg<I>(std::move(map));
                    }(
                        std::make_index_sequence<Bound::n - Bound::kw_idx>{},
                        keyword,
                        std::forward<Values>(values)...
                    );

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            template <size_t I, typename Pos, typename Kw>
            static constexpr Target<I> cpp(
                const Defaults& defaults,
                PositionalPack<Arguments, Pos>& positional,
                KeywordPack<Arguments, Kw>& keyword,
                Values... values
            ) {
                using T = Target<I>;
                constexpr StaticStr name = ArgTraits<T>::name;
                constexpr size_t transition = std::min(Bound::kw_idx, Bound::kwargs_idx);

                if constexpr (ArgTraits<T>::posonly()) {
                    return cpp<I>(defaults, positional, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::pos()) {
                    auto node = keyword.extract(name);
                    if constexpr (I < transition) {
                        if (node) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return to_arg<I>(
                            impl::unpack_arg<I>(std::forward<Values>(values)...)
                        );
                    } else if constexpr (Bound::template has<name>) {
                        if (node || positional.has_value()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return to_arg<I>(
                            impl::unpack_arg<Bound::template idx<name>>(
                                std::forward<Values>(values)...
                            )
                        );
                    } else {
                        if (positional.has_value()) {
                            if (node) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                            return to_arg<I>(positional.value());
                        } else if (node) {
                            if constexpr (std::is_lvalue_reference_v<
                                typename ArgTraits<T>::type
                            >) {
                                return to_arg<I>(node.mapped());
                            } else {
                                return to_arg<I>(std::move(node.mapped()));
                            }
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return to_arg<I>(defaults.template get<I>());
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    return cpp<I>(defaults, keyword, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::args()) {
                    return cpp<I>(defaults, positional, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return cpp<I>(defaults, keyword, std::forward<Values>(values)...);

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

            /* The python() method is used to populate a Python vectorcall argument
             * array according to a C++ call site.  Such an array can then be used to
             * efficiently call a Python function using the vectorcall protocol, which
             * is the fastest way to call a Python function from C++.  Here's the basic
             * layout:
             * 
             *                          ( kwnames tuple )
             *      -------------------------------------
             *      | x | p | p | p |...| k | k | k |...|
             *      -------------------------------------
             *            ^             ^
             *            |             nargs ends here
             *            *args starts here
             * 
             * Where 'x' is an optional first element that can be temporarily written to
             * in order to efficiently forward the `self` argument for bound methods,
             * etc.  The presence of this argument is determined by the
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

            template <size_t J>
            static void python(
                PyObject** args,
                PyObject* kwnames,
                Values... values
            ) {
                using S = Source<J>;
                constexpr StaticStr name = ArgTraits<S>::name;
                constexpr size_t transition = std::min(Bound::kw_idx, Bound::kwargs_idx);

                if constexpr (ArgTraits<S>::posonly()) {
                    try {
                        args[J] = release(to_python(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 0; i < J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (ArgTraits<S>::kw()) {
                    try {
                        PyTuple_SET_ITEM(
                            kwnames,
                            J - transition,
                            release(template_string<name>())
                        );
                        args[J] = release(to_python(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 0; i < J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (ArgTraits<S>::args()) {
                    size_t curr = J;
                    try {
                        const auto& var_args = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& value : var_args) {
                            args[curr] = release(to_python(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 0; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (ArgTraits<S>::kwargs()) {
                    size_t curr = J;
                    try {
                        const auto& var_kwargs = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& [key, value] : var_kwargs) {
                            PyObject* name = PyUnicode_FromStringAndSize(
                                key.data(),
                                key.size()
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            }
                            PyTuple_SET_ITEM(kwnames, curr - transition, name);
                            args[curr] = release(to_python(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 0; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

        public:
            static constexpr size_t n               = sizeof...(Values);
            static constexpr size_t n_pos           = Bound::n_pos;
            static constexpr size_t n_kw            = Bound::n_kw;

            template <StaticStr Name>
            static constexpr bool has               = Bound::template has<Name>;
            static constexpr bool has_pos           = Bound::has_pos;
            static constexpr bool has_args          = Bound::has_args;
            static constexpr bool has_kw            = Bound::has_kw;
            static constexpr bool has_kwargs        = Bound::has_kwargs;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Bound::template idx<Name>;
            static constexpr size_t args_idx        = Bound::args_idx;
            static constexpr size_t kw_idx          = Bound::kw_idx;
            static constexpr size_t kwargs_idx      = Bound::kwargs_idx;

            template <size_t I> requires (I < n)
            using at = Bound::template at<I>;

            static constexpr bool proper_argument_order = Bound::proper_argument_order;
            static constexpr bool no_duplicate_arguments = Bound::no_duplicate_arguments;
            static constexpr bool no_qualified_arg_annotations =
                Bound::no_qualified_arg_annotations;

            /* Call operator is only enabled if source arguments are well-formed and
            match the target signature. */
            static constexpr bool enable = enable_recursive<0, 0>();

            /* A separate constraint is needed for `py::partial()`. */
            static constexpr bool partial = partial_recursive<0, 0>();

            /* Produce an overload key from the bound C++ arguments, which can be used
            to search the overload trie and invoke a resulting function. */
            static Params<std::array<Param, n>> key(Values... values) {
                size_t hash = 0;
                return {
                    .value = []<size_t... Js>(
                        std::index_sequence<Js...>,
                        size_t& hash,
                        Values... values
                     ) {
                        return Params<std::array<Param, n>>{
                            _key<Js>(hash, std::forward<Values>(values))...
                        };
                    }(
                        std::make_index_sequence<n>{},
                        hash,
                        std::forward<Values>(values)...
                    ),
                    .hash = hash
                };
            }

            /* Invoke a C++ function from C++ using Python-style arguments. */
            template <typename Func>
                requires (
                    std::is_invocable_v<Func, Args...> &&
                    proper_argument_order &&
                    no_duplicate_arguments &&
                    no_qualified_arg_annotations &&
                    enable
                )
            static decltype(auto) operator()(
                const Defaults& defaults,
                Func&& func,
                Values... values
            ) {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    const Defaults& defaults,
                    Func func,
                    Values... values
                ) {
                    /// NOTE: If parameter packs are present, the cpp() helper must be
                    /// expanded into a Pack<> using a braced initializer to force
                    /// strict left-to-right evaluation of the arguments, due to side
                    /// effects in the unpacking logic.  This also gives an opportunity
                    /// to check whether the parameter pack is well-formed before
                    /// calling the underlying function, which promotes a fail-fast
                    /// approach.
                    if constexpr (Bound::has_args && Bound::has_kwargs) {
                        auto positional = positional_pack<Arguments, Bound::args_idx>(
                            std::forward<Values>(values)...
                        );
                        auto keyword = keyword_pack<Arguments, Bound::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        Pack<Args...> pack {cpp<Is>(
                            defaults,
                            positional,
                            keyword,
                            std::forward<Values>(values)...
                        )...};
                        positional.validate();
                        keyword.validate();
                        return std::move(pack)(func);
                    } else if constexpr (Bound::has_args) {
                        auto positional = positional_pack<Arguments, Bound::args_idx>(
                            std::forward<Values>(values)...
                        );
                        Pack<Args...> pack {cpp<Is>(
                            defaults,
                            positional,
                            std::forward<Values>(values)...
                        )...};
                        positional.validate();
                        return std::move(pack)(func);
                    } else if constexpr (Bound::has_kwargs) {
                        auto keyword = keyword_pack<Arguments, Bound::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        Pack<Args...> pack {cpp<Is>(
                            defaults,
                            keyword,
                            std::forward<Values>(values)...
                        )...};
                        keyword.validate();
                        return std::move(pack)(func);
                    } else {
                        /// NOTE: left-to-right evaluation order is not required if
                        /// runtime parameter packs are not given.
                        return func(cpp<Is>(
                            defaults,
                            std::forward<Values>(values)...
                        )...);
                    }
                }(
                    std::make_index_sequence<Arguments::n>{},
                    defaults,
                    std::forward<Func>(func),
                    std::forward<Values>(values)...
                );
            }

            /* Invoke a Python function from C++ using Python-style arguments.  This
            will always return a new reference to a raw Python object, or throw a
            runtime error if the arguments are malformed in some way. */
            template <typename = void>
                requires (
                    proper_argument_order &&
                    no_duplicate_arguments &&
                    no_qualified_arg_annotations &&
                    enable
                )
            static PyObject* operator()(
                PyObject* func,
                Values... values
            ) {
                return []<size_t... Js>(
                    std::index_sequence<Js...>,
                    PyObject* func,
                    Values... values
                ) {
                    PyObject* result;

                    /// NOTE: because populating a vectorcall array is done in-place,
                    /// a simple comma operator will always force left-to-right
                    /// evaluation of the arguments, which is necessary to account for
                    /// side effects related to parameter packs.
                    if constexpr (Bound::has_args && Bound::has_kwargs) {
                        const auto& args = impl::unpack_arg<Bound::args_idx>(
                            std::forward<Values>(values)...
                        );
                        const auto& kwargs = impl::unpack_arg<Bound::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs =
                            Bound::n +
                            std::ranges::size(args) +
                            std::ranges::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        ++array;
                        size_t n_kw = Bound::n_kw + std::ranges::size(kwargs);
                        PyObject* kwnames = n_kw ? PyTuple_New(n_kw) : nullptr;
                        (python<Js>(
                            array,
                            kwnames,
                            std::forward<Values>(values)...
                        ), ...);
                        size_t npos = Bound::n - Bound::n_kw + std::ranges::size(args);
                        result = PyObject_Vectorcall(
                            func,
                            --array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Bound::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    } else if constexpr (Bound::has_args) {
                        const auto& args = impl::unpack_arg<Bound::args_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Bound::n + std::ranges::size(args);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        ++array;
                        PyObject* kwnames;
                        if constexpr (Bound::has_kw) {
                            kwnames = PyTuple_New(Bound::n_kw);
                        } else {
                            kwnames = nullptr;
                        }
                        (python<Js>(
                            array,
                            kwnames,
                            std::forward<Values>(values)...
                        ), ...);
                        result = PyObject_Vectorcall(
                            func,
                            --array,
                            (nargs - Bound::n_kw) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        for (size_t i = 1; i <= Bound::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    } else if constexpr (Bound::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Bound::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Bound::n + std::ranges::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        ++array;
                        size_t n_kw = Bound::n_kw + std::ranges::size(kwargs);
                        PyObject* kwnames = n_kw ? PyTuple_New(n_kw) : nullptr;
                        (python<Js>(
                            array,
                            kwnames,
                            std::forward<Values>(values)...
                        ), ...);
                        result = PyObject_Vectorcall(
                            func,
                            --array,
                            (Bound::n - Bound::n_kw) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Bound::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    } else {
                        PyObject* array[Bound::n + 1];
                        array[0] = nullptr;
                        ++array;
                        PyObject* kwnames;
                        if constexpr (Bound::has_kw) {
                            kwnames = PyTuple_New(Bound::n_kw);
                        } else {
                            kwnames = nullptr;
                        }
                        (python<Js>(
                            array,
                            kwnames,
                            std::forward<Values>(values)...
                        ), ...);
                        result = PyObject_Vectorcall(
                            func,
                            --array,
                            (Bound::n - Bound::n_kw) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Bound::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                    }

                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return result;
                }(
                    std::make_index_sequence<Bound::n>{},
                    func,
                    std::forward<Values>(values)...
                );
            }
        };

        /* A helper that binds a Python vectorcall array to the enclosing signature
        and performs the necessary translation to invoke a matching C++ function. */
        struct Vectorcall {
        private:

            /// TODO: rewrite documentation

            /* The py_to_cpp() method is used to convert an index sequence over the
            * enclosing signature into the corresponding values pulled from either an
            * array of Python vectorcall arguments or the function's defaults.  This
            * requires us to parse an array of PyObject* pointers with a binary layout
            * that looks something like this:
            *
            *                          ( kwnames tuple )
            *      -------------------------------------
            *      | x | p | p | p |...| k | k | k |...|
            *      -------------------------------------
            *            ^             ^
            *            |             nargs ends here
            *            *args starts here
            *
            * Where 'x' is an optional first element that can be temporarily written to
            * in order to efficiently forward the `self` argument for bound methods,
            * etc.  The presence of this argument is determined by the
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

            /// TODO: pass in a `std::unordered_map<std::string_view, PyObject*>` that
            /// represents the keyword arguments, and might be destructively searched,
            /// because this is also going to have to handle argument validation.
            /// in a sane way, and allow kwargs packs to be easily constructed.

            template <size_t I>
            at<I> translate(const Defaults& defaults) const {
                using T = at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::posonly()) {
                    if (I < nargs) {
                        return to_arg<I>(reinterpret_borrow<Object>(args[I]));
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return to_arg<I>(defaults.template get<I>());
                    } else {
                        throw TypeError(
                            "missing required positional-only argument at index " +
                            std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::pos()) {
                    if (I < nargs) {
                        return to_arg<I>(reinterpret_borrow<Object>(args[I]));
                    } else if (kwnames != nullptr) {
                        /// TODO: rather than iterating over the kwnames tuple, I should
                        /// take a second to build a
                        /// `std::unordered_map<std::string_view, PyObject*>`, which
                        /// can be passed to this method and efficiently searched.

                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return to_arg<I>(
                                    reinterpret_borrow<Object>(args[nargs + i])
                                );
                            }
                        }
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return to_arg<I>(defaults.template get<I>());
                    } else {
                        throw TypeError(
                            "missing required argument '" + name + "' at index " +
                            std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    if (kwnames != nullptr) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return to_arg<I>(
                                    reinterpret_borrow<Object>(args[i])
                                );
                            }
                        }
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return to_arg<I>(defaults.template get<I>());
                    } else {
                        throw TypeError(
                            "missing required keyword-only argument '" + name + "'"
                        );
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    std::vector<typename ArgTraits<T>::type> vec;
                    vec.reserve(nargs - I);
                    for (size_t i = I; i < nargs; ++i) {
                        vec.emplace_back(reinterpret_borrow<Object>(args[i]));
                    }
                    return to_arg<I>(std::move(vec));

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    std::unordered_map<std::string, typename ArgTraits<T>::type> map;
                    if (kwnames != nullptr) {
                        auto sequence = std::make_index_sequence<Arguments::n_kw>{};
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t length;
                            const char* kwname = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &length
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (!Arguments::callback(kwname)) {
                                map.emplace(
                                    std::string(kwname, length),
                                    reinterpret_borrow<Object>(args[nargs + i])
                                );
                            }
                        }
                    }
                    return to_arg<I>(std::move(map));

                } else {
                    static_assert(false, "invalid argument kind");
                    std::unreachable();
                }
            }

        public:
            PyObject* const* args;
            size_t nargs;
            PyObject* kwnames;
            size_t kwcount;
            size_t offset;

            Vectorcall(PyObject* const* args, size_t nargsf, PyObject* kwnames) :
                args(args),
                nargs(PyVectorcall_NARGS(nargsf)),
                kwnames(kwnames),
                kwcount(kwnames ? PyTuple_GET_SIZE(kwnames) : 0),
                offset(nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET)
            {}

            /// TODO: basically, this class implements the Python call operator when
            /// there are no overloads.  The key() will be generated and if an overload
            /// is found, the vectorcall argument array held here will be perfectly
            /// forwarded.  Otherwise, we will invoke the call operator on the default
            /// implementation, and return the result directly to Python.

            /// TODO: in the Python case, I probably shouldn't implement error-handling
            /// after the fact, but rather while the arguments are being unpacked.  It
            /// might be possible to initialize into a pack, then do the validation,
            /// and then use the pack to call the function.
            /// -> In fact, that might be the only way to avoid undefined behavior,
            /// and force strict left->right evaluation of arguments.

            /* Produce an overload key from the Python arguments, which can be used to
            search the overload trie and invoke a resulting function. */
            Params<std::vector<Param>> key() const {
                size_t hash = 0;
                std::vector<Param> vec;
                vec.reserve(nargs + kwcount);
                for (size_t i = 0; i < nargs; ++i) {
                    Param param = {
                        .name = "",
                        .value = reinterpret_borrow<Object>(args[i]),
                        .kind = ArgKind::POS
                    };
                    hash = hash_combine(hash, param.hash(seed, prime));
                    vec.emplace_back(std::move(param));
                }
                for (size_t i = 0; i < kwcount; ++i) {
                    Py_ssize_t len;
                    const char* name = PyUnicode_AsUTF8AndSize(
                        PyTuple_GET_ITEM(kwnames, i),
                        &len
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    Param param = {
                        .name = std::string_view(name, len),
                        .value = reinterpret_borrow<Object>(args[nargs + i]),
                        .kind = ArgKind::KW
                    };
                    hash = hash_combine(hash, param.hash(seed, prime));
                    vec.emplace_back(std::move(param));
                }
                return {
                    .value = std::move(vec),
                    .hash = hash
                };
            }

            template <typename Func>
                requires (
                    std::is_invocable_v<Func, Args...> &&
                    std::convertible_to<std::invoke_result_t<Func, Args...>, Object>
                )
            auto operator()(const Defaults& defaults, Func&& func) -> Object {
                return [&]<size_t... Is>(
                    std::index_sequence<Is...>,
                    const Defaults& defaults,
                    Func func
                ) {
                    if (kwnames) {
                        /// TODO: build a std::unordered_map of the keyword arguments
                        /// before calling the function, which destructively iterates
                        /// over it.  Afterwards, assert that it is empty, and print
                        /// out any conflicting arguments if not.
                    }

                    Pack<Args...> pack {translate<Is>(defaults)...};
                    /// TODO: validate the pack here, before calling the function
                    /// and after evaluating the translate<Is> calls.  I might also
                    /// need to use an out parameter to track the maximum positional
                    /// index, so that I can validate that no extra positional
                    /// arguments are given before calling the function.

                    if constexpr (std::is_void_v<std::invoke_result_t<Func, Args...>>) {

                        func((..., translate<Is>(defaults)));
                        return None;
                    } else {
                        return to_python(func(translate<Is>(defaults)...));
                    }
                }(
                    std::make_index_sequence<Arguments::n>{},
                    defaults,
                    std::forward<Func>(func)
                );
            }
        };
    };

    /* Convert a non-member function pointer into a member function pointer of the
    given, cvref-qualified type.  Passing void as the enclosing class will return the
    non-member function pointer as-is. */
    template <typename, typename>
    struct to_member_func { static constexpr bool enable = false; };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct to_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...);
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct to_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...);
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile && noexcept;
    };

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer. */
    template <typename T>
    struct Signature : BertrandTag {
        using type = T;
        static constexpr bool enable = false;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...)> : Arguments<A...> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = void;
        using to_ptr = Signature;
        using to_value = Signature<R(A...)>;
        template <typename R2>
        using with_return = Signature<R2(*)(A...)>;
        template <typename C> requires (can_make_member<C>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C>::type>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...)>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, A...>;
        static std::function<R(A...)> capture(PyObject* obj) {
            return [obj](A... args) -> R {
                using Call = typename Arguments<A...>::template Bind<A...>;
                PyObject* result = Call{}(obj, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...) noexcept> : Signature<R(*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...)> : Arguments<C&, A...> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool unqualified_return = !(
            std::is_reference_v<R> ||
            std::is_const_v<std::remove_reference_t<R>> ||
            std::is_volatile_v<std::remove_reference_t<R>>
        );
        static constexpr bool return_is_python = inherits<R, Object>;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...)>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...)>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) noexcept> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) &> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) & noexcept> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const> : Arguments<const C&, A...> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool unqualified_return = !(
            std::is_reference_v<R> ||
            std::is_const_v<std::remove_reference_t<R>> ||
            std::is_volatile_v<std::remove_reference_t<R>>
        );
        static constexpr bool return_is_python = inherits<R, Object>;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = const C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const noexcept> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const &> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const & noexcept> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile> : Arguments<volatile C&, A...> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool unqualified_return = !(
            std::is_reference_v<R> ||
            std::is_const_v<std::remove_reference_t<R>> ||
            std::is_volatile_v<std::remove_reference_t<R>>
        );
        static constexpr bool return_is_python = inherits<R, Object>;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile noexcept> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile &> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile & noexcept> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile> : Arguments<const volatile C&, A...> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool unqualified_return = !(
            std::is_reference_v<R> ||
            std::is_const_v<std::remove_reference_t<R>> ||
            std::is_volatile_v<std::remove_reference_t<R>>
        );
        static constexpr bool return_is_python = inherits<R, Object>;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = const volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> : Signature<R(C::*)(A...) const volatile> {}; 

    template <typename T>
    struct GetSignature {
        static constexpr bool enable = false;
        using type = void;
    };
    template <typename T> requires (Signature<std::remove_cvref_t<T>>::enable)
    struct GetSignature<T> {
        static constexpr bool enable = true;
        using type = Signature<std::remove_cvref_t<T>>::template with_self<void>;
    };
    template <has_call_operator T>
    struct GetSignature<T> {
    private:
        using U = decltype(&std::remove_cvref_t<T>::operator());

    public:
        static constexpr bool enable = GetSignature<U>::enable;
        using type = GetSignature<U>::type;
    };

    template <typename T>
    concept has_signature = GetSignature<T>::enable;
    template <has_signature T>
    using get_signature = GetSignature<T>::type;

    template <typename Sig>
    concept function_pointer_like = Sig::enable;
    template <typename Sig>
    concept args_fit_within_bitset = Sig::n <= 64;
    template <typename Sig>
    concept args_are_python = Sig::args_are_python;
    template <typename Sig>
    concept args_are_convertible_to_python = Sig::args_are_convertible_to_python;
    template <typename Sig>
    concept unqualified_return = Sig::unqualified_return;
    template <typename Sig>
    concept return_is_python = Sig::return_is_python;
    template <typename Sig>
    concept return_is_convertible_to_python = Sig::return_is_convertible_to_python;
    template <typename Sig>
    concept proper_argument_order = Sig::proper_argument_order;
    template <typename Sig>
    concept no_duplicate_arguments = Sig::no_duplicate_arguments;
    template <typename Sig>
    concept no_qualified_arg_annotations = Sig::no_qualified_arg_annotations;
    template <typename Sig>
    concept no_qualified_args = Sig::no_qualified_args;

}


/* A template constraint that controls whether the `py::call()` operator is enabled
for a given C++ function and argument list. */
template <typename Func, typename... Args>
concept callable =
    impl::has_signature<Func> &&
    impl::args_fit_within_bitset<impl::get_signature<Func>> &&
    impl::proper_argument_order<impl::get_signature<Func>> &&
    impl::no_duplicate_arguments<impl::get_signature<Func>> &&
    impl::no_qualified_arg_annotations<impl::get_signature<Func>> &&
    impl::get_signature<Func>::template Bind<Args...>::proper_argument_order &&
    impl::get_signature<Func>::template Bind<Args...>::no_duplicate_arguments &&
    impl::get_signature<Func>::template Bind<Args...>::no_qualified_arg_annotations &&
    impl::get_signature<Func>::template Bind<Args...>::enable;


/* Introspect a function signature to retrieve a tuple capable of storing default
values for all argument annotations that are marked as `::opt`.  An object of this
type can be passed to the `call` function to provide default values for arguments that
are not present at the call site.  The tuple itself can be constructed using the same
keyword argument and parameter pack semantics as the `call()` operator itself. */
template <impl::has_signature Func>
using Defaults = impl::get_signature<Func>::Defaults;


/// TODO: these extra forms with no/movable defaults have to be implemented in Bind<>.


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename Func, typename... Args>
    requires (
        callable<Func, Args...> &&
        impl::get_signature<Func>::n_opt == 0
    )
decltype(auto) call(Func&& func, Args&&... args) {
    return typename impl::get_signature<Func>::template Bind<Args...>{}(
        {},
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename Func, typename... Args> requires (callable<Func, Args...>)
decltype(auto) call(const Defaults<Func>& defaults, Func&& func, Args&&... args) {
    return typename impl::get_signature<Func>::template Bind<Args...>{}(
        defaults,
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );
}


/* Invoke a C++ function with Python-style calling conventions, including keyword
arguments and/or parameter packs, which are resolved at compile time.  Note that the
function signature cannot contain any template parameters (including auto arguments),
as the function signature must be known unambiguously at compile time to implement the
required matching. */
template <typename Func, typename... Args> requires (callable<Func, Args...>)
decltype(auto) call(Defaults<Func>&& defaults, Func&& func, Args&&... args) {
    return typename impl::get_signature<Func>::template Bind<Args...>{}(
        std::move(defaults),
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );
}


namespace impl {

    template <has_signature Func, typename... Args>
    struct Partial {
    private:

        template <typename... Values>
        struct complete {
            template <typename out, size_t, size_t>
            struct reorder { using type = out; };
            template <typename... out, size_t I, size_t J>
                requires (I < sizeof...(Args))
            struct reorder<Pack<out...>, I, J> {
                using type = reorder<
                    Pack<out..., unpack_type<I, Args...>>,
                    I + 1,
                    J
                >::type;
            };
            template <typename... out, size_t I, size_t J>
                requires (J < sizeof...(Values))
            struct reorder<Pack<out...>, I, J> {
                using type = reorder<
                    Pack<out..., unpack_type<J, Values...>>,
                    I,
                    J + 1
                >::type;
            };
            template <typename... out, size_t I, size_t J>
                requires (I < sizeof...(Args) && J < sizeof...(Values))
            struct reorder<Pack<out...>, I, J> {
                template <typename L, typename R>
                struct merge {
                    static constexpr size_t new_I = I + 1;
                    static constexpr size_t new_J = J;
                    using type = L;
                    static constexpr decltype(auto) operator()(
                        const std::tuple<Args...>& parts,
                        Values... args
                    ) {
                        if constexpr (std::is_lvalue_reference_v<L>) {
                            return std::get<I>(parts);
                        } else {
                            return std::remove_reference_t<L>(std::get<I>(parts));
                        }
                    }
                    static constexpr decltype(auto) operator()(
                        std::tuple<Args...>&& parts,
                        Values... args
                    ) {
                        if constexpr (std::is_lvalue_reference_v<L>) {
                            return std::get<I>(parts);
                        } else {
                            return std::remove_reference_t<L>(std::move(
                                std::get<I>(parts)
                            ));
                        }
                    }
                };
                template <typename L, typename R>
                    requires (ArgTraits<L>::args() && ArgTraits<R>::posonly())
                struct merge<L, R> {
                    static constexpr size_t new_I = I;
                    static constexpr size_t new_J = J + 1;
                    using type = R;
                    static constexpr decltype(auto) operator()(
                        const std::tuple<Args...>& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                    static constexpr decltype(auto) operator()(
                        std::tuple<Args...>&& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                };
                template <typename L, typename R>
                    requires (ArgTraits<L>::kw() && (
                        ArgTraits<R>::posonly() || ArgTraits<R>::args()
                    ))
                struct merge<L, R> {
                    static constexpr size_t new_I = I;
                    static constexpr size_t new_J = J + 1;
                    using type = R;
                    static constexpr decltype(auto) operator()(
                        const std::tuple<Args...>&& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                    static constexpr decltype(auto) operator()(
                        std::tuple<Args...>& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                };
                template <typename L, typename R>
                    requires (ArgTraits<L>::kwargs() && (
                        ArgTraits<R>::posonly() || ArgTraits<R>::args() || ArgTraits<R>::kw()
                    ))
                struct merge<L, R> {
                    static constexpr size_t new_I = I;
                    static constexpr size_t new_J = J + 1;
                    using type = R;
                    static constexpr decltype(auto) operator()(
                        const std::tuple<Args...>& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                    static constexpr decltype(auto) operator()(
                        std::tuple<Args...>&& parts,
                        Values... args
                    ) {
                        return unpack_arg<J>(std::forward<Values>(args)...);
                    }
                };

                using L = unpack_type<I, Args...>;
                using R = unpack_type<J, Values...>;
                using type = reorder<
                    Pack<out..., typename merge<L, R>::type>,
                    merge<L, R>::new_I,
                    merge<L, R>::new_J
                >::type;

                static constexpr decltype(auto) operator()(
                    const std::tuple<Args...>& parts,
                    Values... args
                ) {
                    return merge<L, R>{}(parts, std::forward<Values>(args)...);
                }
                static constexpr decltype(auto) operator()(
                    std::tuple<Args...>&& parts,
                    Values... args
                ) {
                    return merge<L, R>{}(std::move(parts), std::forward<Values>(args)...);
                }
            };
            using type = reorder<Pack<>, 0, 0>::type;
            template <typename>
            static constexpr bool _enable = false;
            template <typename... Ordered>
            static constexpr bool _enable<Pack<Ordered...>> = callable<Func, Ordered...>;
            static constexpr bool enable = _enable<type>;
        };

    public:
        static constexpr size_t n = sizeof...(Args);
        /// TODO: other introspection methods

        impl::get_signature<Func>::Defaults defaults;
        std::remove_cvref_t<Func> func;
        std::tuple<std::remove_cvref_t<Args>...> parts;

        [[nodiscard]] std::remove_cvref_t<Func>& operator*() {
            return func;
        }

        [[nodiscard]] const std::remove_cvref_t<Func>& operator*() const {
            return func;
        }

        [[nodiscard]] std::remove_cvref_t<Func>* operator->() {
            return &func;
        }

        [[nodiscard]] const std::remove_cvref_t<Func>* operator->() const {
            return &func;
        }

        template <size_t I> requires (I < sizeof...(Args))
        [[nodiscard]] decltype(auto) get() const {
            return std::get<I>(parts);
        }

        template <size_t I> requires (I < sizeof...(Args))
        [[nodiscard]] decltype(auto) get() && {
            return std::move(std::get<I>(parts));
        }

        template <typename T> requires (std::same_as<T, std::remove_cvref_t<Args>> || ...)
        [[nodiscard]] decltype(auto) get() const {
            return std::get<std::remove_cvref_t<T>>(parts);
        }

        template <typename T> requires (std::same_as<T, std::remove_cvref_t<Args>> || ...)
        [[nodiscard]] decltype(auto) get() && {
            return std::move(std::get<std::remove_cvref_t<T>>(parts));
        }

        template <typename... Values> requires (complete<Values...>::enable)
        [[nodiscard]] decltype(auto) operator()(Values&&... values) const {
            using call = complete<Values...>::template reorder<Pack<>, 0, 0>;
            return call{}(parts, std::forward<Values>(values)...);
        }

        template <typename... Values> requires (complete<Values...>::enable)
        [[nodiscard]] decltype(auto) operator()(Values&&... values) && {
            using call = complete<Values...>::template reorder<Pack<>, 0, 0>;
            return call{}(std::move(parts), std::forward<Values>(values)...);
        }
    };

}


/* A template constraint that controls whether the `py::partial()` operator is enabled
for a given C++ function and argument list. */
template <typename Func, typename... Args>
concept partially_callable =
    impl::has_signature<Func> &&
    impl::args_fit_within_bitset<impl::get_signature<Func>> &&
    impl::proper_argument_order<impl::get_signature<Func>> &&
    impl::no_duplicate_arguments<impl::get_signature<Func>> &&
    impl::no_qualified_arg_annotations<impl::get_signature<Func>> &&
    impl::get_signature<Func>::template Bind<Args...>::proper_argument_order &&
    impl::get_signature<Func>::template Bind<Args...>::no_duplicate_arguments &&
    impl::get_signature<Func>::template Bind<Args...>::no_qualified_arg_annotations &&
    impl::get_signature<Func>::template Bind<Args...>::partial;


/* Construct a partial function object that captures a C++ function and a subset of its
arguments, which can be used to invoke the function later with the remaining arguments.
Arguments are given in the same style as `call()`, and will be stored internally
within the partial object, forcing a copy in the case of lvalue inputs.  When the
partial is called, an additional copy may be made if the function expects a temporary
or rvalue reference, so as not to modify the stored arguments.  If the partial is
called as an rvalue (by moving it, for example), then the second copy can be avoided,
and the stored arguments will be moved directly into the function call.

Note that the function signature cannot contain any template parameters (including auto
arguments), as the function signature must be known unambiguously at compile time to
implement the required matching.

The returned partial is a thin proxy that only implements the call operator and a
handful of introspection methods.  It also allows transparent access to the decorated
function via the `*` and `->` operators. */
template <typename Func, typename... Args> requires (partially_callable<Func, Args...>)
auto partial(Func&& func, Args&&... args) -> impl::Partial<Func, Args...> {
    return {
        {},
        std::forward<Func>(func),
        std::forward<Args>(args)...
    };
}


/* Construct a partial function object that captures a C++ function and a subset of its
arguments, which can be used to invoke the function later with the remaining arguments.
Arguments are given in the same style as `call()`, and will be stored internally
within the partial object, forcing a copy in the case of lvalue inputs.  When the
partial is called, an additional copy may be made if the function expects a temporary
or rvalue reference, so as not to modify the stored arguments.  If the partial is
called as an rvalue (by moving it, for example), then the second copy can be avoided,
and the stored arguments will be moved directly into the function call.

Note that the function signature cannot contain any template parameters (including auto
arguments), as the function signature must be known unambiguously at compile time to
implement the required matching.

The returned partial is a thin proxy that only implements the call operator and a
handful of introspection methods.  It also allows transparent access to the decorated
function via the `*` and `->` operators. */
template <typename Func, typename... Args> requires (partially_callable<Func, Args...>)
auto partial(const Defaults<Func>& defaults, Func&& func, Args&&... args)
    -> impl::Partial<Func, Args...>
{
    return {
        defaults,
        std::forward<Func>(func),
        std::forward<Args>(args)...
    };
}


/* Construct a partial function object that captures a C++ function and a subset of its
arguments, which can be used to invoke the function later with the remaining arguments.
Arguments are given in the same style as `call()`, and will be stored internally
within the partial object, forcing a copy in the case of lvalue inputs.  When the
partial is called, an additional copy may be made if the function expects a temporary
or rvalue reference, so as not to modify the stored arguments.  If the partial is
called as an rvalue (by moving it, for example), then the second copy can be avoided,
and the stored arguments will be moved directly into the function call.

Note that the function signature cannot contain any template parameters (including auto
arguments), as the function signature must be known unambiguously at compile time to
implement the required matching.

The returned partial is a thin proxy that only implements the call operator and a
handful of introspection methods.  It also allows transparent access to the decorated
function via the `*` and `->` operators. */
template <typename Func, typename... Args> requires (partially_callable<Func, Args...>)
auto partial(Defaults<Func>&& defaults, Func&& func, Args&&... args)
    -> impl::Partial<Func, Args...>
{
    return {
        std::move(defaults),
        std::forward<Func>(func),
        std::forward<Args>(args)...
    };
}


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
                impl::has_cpp<Self> &&
                std::is_invocable_r_v<
                    typename __call__<Self, Args...>::type,
                    impl::cpp_type<Self>,
                    Args...
                >
            ) || (
                !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __call__<Self, Args...>::type, Object> &&
                __getattr__<Self, "__call__">::enable &&
                impl::inherits<typename __getattr__<Self, "__call__">::type, impl::FunctionTag>
            )
        )
    )
decltype(auto) Object::operator()(this Self&& self, Args&&... args) {
    if constexpr (std::is_invocable_v<__call__<Self, Args...>, Self, Args...>) {
        return __call__<Self, Args...>{}(
            std::forward<Self>(self),
            std::forward<Args>(args)...
        );

    } else if constexpr (impl::has_cpp<Self>) {
        return from_python(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    } else {
        return getattr<"__call__">(std::forward<Self>(self))(
            std::forward<Args>(args)...
        );
    }
}


template <impl::is<Object> Self, std::convertible_to<Object>... Args>
Object __call__<Self, Args...>::operator()(Self self, Args... args) {
    return impl::Arguments<
        Arg<"args", Object>::args,
        Arg<"kwargs", Object>::kwargs
    >::template Bind<Args...>{}(ptr(self), std::forward<Args>(args)...);
}


////////////////////////
////    FUNCTION    ////
////////////////////////


namespace impl {

    /* A `@classmethod` descriptor for a C++ function type, which references an
    unbound function and produces bound equivalents that pass the enclosing type as a
    `self` argument when accessed. */
    struct ClassMethod : PyObject {
        static PyTypeObject __type__;

        PyObject* __wrapped__;
        PyObject* member_function_type = nullptr;
        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);

        explicit ClassMethod(PyObject* func) : __wrapped__(Py_NewRef(func)) {}

        ~ClassMethod() noexcept {
            Py_XDECREF(__wrapped__);
            Py_XDECREF(member_function_type);
        }

        static void __dealloc__(ClassMethod* self) noexcept {
            self->~ClassMethod();
        }

        static PyObject* __get__(
            ClassMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            PyObject* const args[] = {
                self->member_function_type,
                self->__wrapped__,
                type == Py_None ? reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
            };
            return PyObject_VectorcallMethod(
                ptr(template_string<"_capture">()),
                args,
                3,
                nullptr
            );
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
                PyObject* cls = args[0];
                PyObject* forward[] = {
                    self->__wrapped__,
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
                self->member_function_type =
                    get_member_function_type(self->__wrapped__, cls);
                return result;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: rather than forwarding to the member function type, this would need
        /// to generate a slice from the underlying function object, right?  This would
        /// need some serious thought.

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            return PyNumber_Or(
                PyType_IsSubtype(Py_TYPE(lhs), &__type__) ?
                    reinterpret_cast<ClassMethod*>(lhs)->member_function_type :
                    lhs,
                PyType_IsSubtype(Py_TYPE(rhs), &__type__) ?
                    reinterpret_cast<ClassMethod*>(rhs)->member_function_type :
                    rhs
            );
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            return PyNumber_And(
                PyType_IsSubtype(Py_TYPE(lhs), &__type__) ?
                    reinterpret_cast<ClassMethod*>(lhs)->member_function_type :
                    lhs,
                PyType_IsSubtype(Py_TYPE(rhs), &__type__) ?
                    reinterpret_cast<ClassMethod*>(rhs)->member_function_type :
                    rhs
            );
        }

        static PyObject* __repr__(ClassMethod* self) noexcept {
            try {
                std::string str =
                    "<classmethod(" +
                    repr(reinterpret_borrow<Object>(self->__wrapped__)) +
                    ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        static PyObject* get_member_function_type(
            PyObject* func,
            PyObject* cls
        ) noexcept {
            Object bertrand = reinterpret_steal<Object>(PyImport_Import(
                ptr(template_string<"bertrand">())
            ));
            if (bertrand.is(nullptr)) {
                Exception::from_python();
            }
            Object key = reinterpret_steal<Object>(PyObject_GetAttr(
                func,
                ptr(template_string<"__template_key__">())
            ));
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            PySliceObject* rtype = reinterpret_cast<PySliceObject*>(PyTuple_GET_ITEM(
                ptr(key),
                0
            ));
            /// TODO: do I also need to remove the first argument?  Also, allocating
            /// a new tuple is just a good idea anyways, since __template_key__ may
            /// not be read-only.
            PyObject* new_rtype = PySlice_New(
                cls,
                reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type),
                rtype->step
            );
            if (new_rtype == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(key), 0, new_rtype);
            Py_DECREF(rtype);
            PyObject* result = PyObject_GetItem(
                ptr(template_string<"Function">()),
                ptr(key)
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return result;
        }

        static PyMemberDef members[];

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

    };

    PyMemberDef ClassMethod::members[] = {
        {
            "__wrapped__",
            Py_T_OBJECT_EX,
            offsetof(ClassMethod, __wrapped__),
            Py_READONLY,
            nullptr
        },
        {nullptr}
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
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION |
            Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function as a class method of a Python class.

Notes
-----
This descriptor can only be instantiated by applying the `@classmethod`
decorator of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_members = ClassMethod::members,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&ClassMethod::__get__),
        .tp_vectorcall_offset = offsetof(ClassMethod, __vectorcall__)
    };

    /* A `@staticmethod` descriptor for a C++ function type, which references an
    unbound function and directly forwards it when accessed. */
    struct StaticMethod : PyObject {
        static PyTypeObject __type__;

        PyObject* __wrapped__;
        vectorcallfunc __vectorcall__ = reinterpret_cast<vectorcallfunc>(&__call__);

        explicit StaticMethod(PyObject* func) noexcept : __wrapped__(Py_NewRef(func)) {}

        ~StaticMethod() noexcept {
            Py_XDECREF(__wrapped__);
        }

        static void __dealloc__(StaticMethod* self) noexcept {
            self->~StaticMethod();
        }

        static PyObject* __get__(
            StaticMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return Py_NewRef(self->__wrapped__);
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
                    self->__wrapped__,
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

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            return PyNumber_Or(
                PyType_IsSubtype(Py_TYPE(lhs), &__type__) ?
                    reinterpret_cast<StaticMethod*>(lhs)->__wrapped__ :
                    lhs,
                PyType_IsSubtype(Py_TYPE(rhs), &__type__) ?
                    reinterpret_cast<StaticMethod*>(rhs)->__wrapped__ :
                    rhs
            );
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            return PyNumber_And(
                PyType_IsSubtype(Py_TYPE(lhs), &__type__) ?
                    reinterpret_cast<StaticMethod*>(lhs)->__wrapped__ :
                    lhs,
                PyType_IsSubtype(Py_TYPE(rhs), &__type__) ?
                    reinterpret_cast<StaticMethod*>(rhs)->__wrapped__ :
                    rhs
            );
        }

        static PyObject* __repr__(StaticMethod* self) noexcept {
            try {
                std::string str =
                    "<staticmethod(" +
                    repr(reinterpret_borrow<Object>(self->__wrapped__)) +
                    ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        static PyMemberDef members[];

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&__or__),
        };

    };

    PyMemberDef StaticMethod::members[] = {
        {
            "__wrapped__",
            Py_T_OBJECT_EX,
            offsetof(StaticMethod, __wrapped__),
            Py_READONLY,
            nullptr
        },
        {nullptr}
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
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION |
            Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function into a static method of a Python
class.

Notes
-----
This descriptor can only be instantiated by applying the `@staticmethod`
decorator of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_members = StaticMethod::members,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&StaticMethod::__get__),
        .tp_vectorcall_offset = offsetof(StaticMethod, __vectorcall__)
    };

    /* A `@property` descriptor for a C++ function type that accepts a single
    compatible argument, which will be used as the getter for the property.  Setters
    and deleters can also be registered with the same `self` parameter.  The setter can
    accept any type for the assigned value, allowing overloads. */
    struct Property : PyObject {
        static PyTypeObject __type__;

        PyObject* cls;
        PyObject* fget;
        PyObject* fset;
        PyObject* fdel;

        /// TODO: Properties should convert the setter/deleter into C++ functions
        /// supporting overloads, just like the getter?  I don't even really know
        /// how that would work.

        explicit Property(
            PyObject* cls,
            PyObject* fget,
            PyObject* fset,
            PyObject* fdel
        ) noexcept : cls(cls), fget(fget), fset(fset), fdel(fdel)
        {
            Py_INCREF(cls);
            Py_INCREF(fget);
            Py_XINCREF(fset);
            Py_XINCREF(fdel);
        }

        ~Property() noexcept {
            Py_DECREF(cls);
            Py_DECREF(fget);
            Py_XDECREF(fset);
            Py_XDECREF(fdel);
        }

        static void __dealloc__(Property* self) noexcept {
            self->~Property();
        }

        static PyObject* __get__(
            Property* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return PyObject_CallOneArg(self->fget, obj);
        }

        static PyObject* __set__(
            Property* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            if (value) {
                if (self->fset == nullptr) {
                    PyObject* name = PyObject_GetAttr(
                        self->fget,
                        ptr(template_string<"__name__">())
                    );
                    if (name == nullptr) {
                        return nullptr;
                    }
                    PyErr_Format(
                        PyExc_AttributeError,
                        "property '%U' of %R object has no setter",
                        name,
                        self->cls
                    );
                    Py_DECREF(name);
                    return nullptr;
                }
                PyObject* const args[] = {obj, value};
                return PyObject_Vectorcall(
                    self->fset,
                    args,
                    2,
                    nullptr
                );
            }

            if (self->fdel == nullptr) {
                PyObject* name = PyObject_GetAttr(
                    self->fget,
                    ptr(template_string<"__name__">())
                );
                if (name == nullptr) {
                    return nullptr;
                }
                PyErr_Format(
                    PyExc_AttributeError,
                    "property '%U' of %R object has no deleter",
                    name,
                    self->cls
                );
                Py_DECREF(name);
                return nullptr;
            }
            return PyObject_CallOneArg(self->fdel, obj);
        }

        /// TODO: @setter/@deleter decorators?  

        static PyObject* __repr__(Property* self) noexcept {
            try {
                std::string str =
                    "<property(" + repr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(self->fget))
                    ) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /// TODO: these may need to be properties, so that assigning to a
        /// property's setter/deleter will convert the object into a C++ function

        /// TODO: properties must expose a __wrapped__ member that points to the
        /// getter.

        static PyMemberDef members[];

    };

    PyMemberDef Property::members[] = {
        {
            "fget",
            Py_T_OBJECT_EX,
            offsetof(Property, fget),
            Py_READONLY,
            nullptr
        },
        {
            "fset",
            Py_T_OBJECT_EX,
            offsetof(Property, fset),
            0,
            nullptr
        },
        {
            "fdel",
            Py_T_OBJECT_EX,
            offsetof(Property, fdel),
            0,
            nullptr
        },
        {nullptr}
    };

    PyTypeObject Property::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Property).name(),
        .tp_basicsize = sizeof(Property),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Property::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Property::__repr__),
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function as a property getter of a Python
class.

Notes
-----
This descriptor can only be instantiated by applying the `@property` decorator
of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_members = Property::members,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Property::__get__),
        .tp_descr_set = reinterpret_cast<descrsetfunc>(&Property::__set__),
    };

    /* The Python `bertrand.Function[]` template interface type, which holds all
    instantiations, each of which inherit from this class, and allows for CTAD-like
    construction via the `__new__()` operator.  Has no interface otherwise, requiring
    the user to manually instantiate it as if it were a C++ template. */
    struct FunctionTemplates : PyObject {

        /// TODO: this HAS to be a heap type because it is an instance of the metaclass,
        /// and therefore always has mutable state.
        /// -> Maybe when writing bindings, this is just given as a function to the
        /// binding generator, and it would be responsible for implementing the
        /// template interface's CTAD constructor, and it would use Python-style
        /// argument annotations just like any other function.

        /// TODO: Okay, the way to do this is to have the bindings automatically
        /// populate tp_new with an overloadable function, and then the user can
        /// register overloads directly from Python.  The function you supply to the
        /// binding helper would be inserted as the base case, which defaults to
        /// raising a TypeError if the user tries to instantiate the template.  If
        /// that is the case, then I might be able to automatically register overloads
        /// as each type is instantiated, in a way that doesn't cause errors if the
        /// overload conflicts with an existing one.
        /// -> If I implement that using argument annotations, then this gets
        /// substantially simpler as well, since I don't need to extract the arguments
        /// manually.

        /// TODO: remember to set tp_vectorcall to this method, so I don't need to
        /// implement real __new__/__init__ constructors.
        static PyObject* __new__(
            FunctionTemplates* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            try {
                size_t nargs = PyVectorcall_NARGS(nargsf);
                size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
                if (nargs != 1) {
                    throw TypeError(
                        "expected a single, positional-only argument, but "
                        "received " + std::to_string(nargs)
                    );
                }
                PyObject* func = args[0];
                Object name = reinterpret_steal<Object>(nullptr);
                Object doc = reinterpret_steal<Object>(nullptr);
                if (kwcount) {
                    for (size_t i = 0; i < kwcount; ++i) {
                        PyObject* key = PyTuple_GET_ITEM(kwnames, i);
                        int is_name = PyObject_RichCompareBool(
                            key,
                            ptr(template_string<"name">()),
                            Py_EQ
                        );
                        if (is_name < 0) {
                            Exception::from_python();
                        } else if (is_name) {
                            name = reinterpret_borrow<Object>(args[nargs + i]);
                            if (!PyUnicode_Check(ptr(name))) {
                                throw TypeError(
                                    "expected 'name' to be a string, but received " +
                                    repr(name)
                                );
                            }
                        }
                        int is_doc = PyObject_RichCompareBool(
                            key,
                            ptr(template_string<"doc">()),
                            Py_EQ
                        );
                        if (is_doc < 0) {
                            Exception::from_python();
                        } else if (is_doc) {
                            doc = reinterpret_borrow<Object>(args[nargs + i]);
                            if (!PyUnicode_Check(ptr(doc))) {
                                throw TypeError(
                                    "expected 'doc' to be a string, but received " +
                                    repr(doc)
                                );
                            }
                        }
                        if (!is_name && !is_doc) {
                            throw TypeError(
                                "unexpected keyword argument '" +
                                repr(reinterpret_borrow<Object>(key)) + "'"
                            );
                        }
                    }
                }

                // inspect the input function and subscript the template interface to
                // get the correct specialization
                impl::Inspect signature = {
                    func,
                    impl::fnv1a_seed,
                    impl::fnv1a_prime
                };
                Object specialization = reinterpret_steal<Object>(
                    PyObject_GetItem(
                        self,
                        ptr(signature.template_key())
                    )
                );
                if (specialization.is(nullptr)) {
                    Exception::from_python();
                }

                // if the parameter list contains unions, then we need to default-
                // initialize the specialization and then register separate overloads
                // for each path through the parameter list.  Note that if the function
                // is the only argument and already exactly matches the deduced type,
                // then we can just return it directly to avoid unnecessary nesting.
                Object result = reinterpret_steal<Object>(nullptr);
                if (signature.size() > 1) {
                    if (!kwcount) {
                        if (specialization.is(
                            reinterpret_cast<PyObject*>(Py_TYPE(func))
                        )) {
                            return release(specialization);
                        }
                        result = reinterpret_steal<Object>(PyObject_CallNoArgs(
                            ptr(specialization)
                        ));
                    } else if (name.is(nullptr)) {
                        PyObject* args[] = {
                            nullptr,
                            ptr(doc),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    } else if (doc.is(nullptr)) {
                        PyObject* args[] = {
                            nullptr,
                            ptr(name),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    } else {
                        PyObject* args[] = {
                            nullptr,
                            ptr(name),
                            ptr(doc),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    }
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object rc = reinterpret_steal<Object>(PyObject_CallMethodOneArg(
                        ptr(result),
                        ptr(impl::template_string<"overload">()),
                        func
                    ));
                    if (rc.is(nullptr)) {
                        Exception::from_python();
                    }
                    return release(result);
                }

                // otherwise, we can initialize the specialization directly, which
                // captures the function and uses it as the base case
                if (!kwcount) {
                    if (specialization.is(
                        reinterpret_cast<PyObject*>(Py_TYPE(func))
                    )) {
                        return release(specialization);
                    }
                    result = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(specialization),
                        func
                    ));
                } else if (name.is(nullptr)) {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(doc),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                } else if (doc.is(nullptr)) {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(name),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                } else {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(name),
                        ptr(doc),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                }
                if (result.is(nullptr)) {
                    Exception::from_python();
                }
                return release(result);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    };

}  // namespace impl


template <typename F = Object(*)(
    Arg<"args", Object>::args,
    Arg<"kwargs", Object>::kwargs
)>
    requires (
        impl::function_pointer_like<F> &&
        impl::no_qualified_args<impl::Signature<F>> &&
        impl::no_duplicate_arguments<impl::Signature<F>> &&
        impl::proper_argument_order<impl::Signature<F>> &&
        impl::args_fit_within_bitset<impl::Signature<F>> &&
        impl::args_are_python<impl::Signature<F>> &&
        impl::return_is_python<impl::Signature<F>> &&
        impl::unqualified_return<impl::Signature<F>>
    )
struct Function;


template <typename F>
struct Interface<Function<F>> : impl::FunctionTag {

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
            impl::Arguments<A...>::no_duplicate_arguments &&
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
    template <StaticStr Name>
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
    template <StaticStr Name> requires (has<Name>)
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
    static constexpr uint64_t required = impl::Signature<F>::required;

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

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.  This helper has no overhead over a traditional
    C++ function call, disregarding any logic necessary to construct default values. */
    template <typename Func, typename... Args> requires (invocable<Func> && bind<Args...>)
    static Return call(const Defaults& defaults, Func&& func, Args&&... args) {
        return typename impl::Signature<F>::template Bind<Args...>{}(
            defaults,
            std::forward<Func>(func),
            std::forward<Args>(args)...
        );
    }

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <impl::inherits<Object> R = Object, typename... Args>
        requires (!std::is_reference_v<R> && bind<Args...>)
    static Return call(PyObject* func, Args&&... args) {
        PyObject* result = typename impl::Signature<F>::template Bind<Args...>{}(
            func,
            std::forward<Args>(args)...
        );
        if constexpr (std::is_void_v<Return>) {
            Py_DECREF(result);
        } else {
            return reinterpret_steal<R>(result);
        }
    }

    /* Register an overload for this function from C++. */
    template <typename Self, typename Func>
        requires (
            !std::is_const_v<std::remove_reference_t<Self>> &&
            compatible<Func>
        )
    void overload(this Self&& self, const Function<Func>& func);

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
struct Interface<Type<Function<F>>> {

    /* The normalized function pointer type for this specialization. */
    using Signature = Interface<Function<F>>::Signature;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = Interface<Function<F>>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = Interface<Function<F>>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = Interface<Function<F>>::Overloads;

    /* The function's return type. */
    using Return = Interface<Function<F>>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return = Interface<Function<F>>::template with_return<R>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self = Interface<Function<F>>::template with_self<C>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_arguments &&
            impl::Arguments<A...>::no_qualified_arg_annotations
        )
    using with_args = Interface<Function<F>>::template with_args<A...>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = Interface<Function<F>>::template compatible<Func>;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = Interface<Function<F>>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = Interface<Function<F>>::template bind<Args...>;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = Interface<Function<F>>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = Interface<Function<F>>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = Interface<Function<F>>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = Interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = Interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = Interface<Function<F>>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = Interface<Function<F>>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = Interface<Function<F>>::n_opt_pos;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = Interface<Function<F>>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = Interface<Function<F>>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = Interface<Function<F>>::template has<Name>;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = Interface<Function<F>>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = Interface<Function<F>>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = Interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = Interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = Interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = Interface<Function<F>>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = Interface<Function<F>>::has_opt_pos;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = Interface<Function<F>>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = Interface<Function<F>>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = Interface<Function<F>>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = Interface<Function<F>>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = Interface<Function<F>>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = Interface<Function<F>>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = Interface<Function<F>>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = Interface<Function<F>>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = Interface<Function<F>>::opt_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = Interface<Function<F>>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = Interface<Function<F>>::opt_pos_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = Interface<Function<F>>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = Interface<Function<F>>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = Interface<Function<F>>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = Interface<Function<F>>::kwargs_index;

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = Interface<Function<F>>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr uint64_t required = Interface<Function<F>>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = Interface<Function<F>>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = Interface<Function<F>>::prime;

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

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.  This helper has no overhead over a traditional
    C++ function call, disregarding any logic necessary to construct default values. */
    template <typename Func, typename... Args> requires (invocable<Func> && bind<Args...>)
    static Return call(const Defaults& defaults, Func&& func, Args&&... args) {
        return Interface<Function<F>>::call(
            defaults,
            std::forward<Func>(func),
            std::forward<Args>(args)...
        );
    }

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <impl::inherits<Object> R = Object, typename... Args>
        requires (!std::is_reference_v<R> && bind<Args...>)
    static Return call(PyObject* func, Args&&... args) {
        return Interface<Function<F>>::template call<R>(
            func,
            std::forward<Args>(args)...
        );
    }

    /* Register an overload for this function. */
    template <impl::inherits<Interface<Function<F>>> Self, typename Func>
        requires (!std::is_const_v<std::remove_reference_t<Self>> && compatible<Func>)
    void overload(Self&& self, const Function<Func>& func) {
        std::forward<Self>(self).overload(func);
    }

    /* Attach the function as a bound method of a Python type. */
    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void method(const Self& self, Type<T>& type) {
        std::forward<Self>(self).method(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void classmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).classmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void staticmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).staticmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void property(const Self& self, Type<T>& type, /* setter */, /* deleter */) {
        std::forward<Self>(self).property(type);
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);

    template <impl::inherits<Interface> Self>
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
        impl::function_pointer_like<F> &&
        impl::no_qualified_args<impl::Signature<F>> &&
        impl::no_duplicate_arguments<impl::Signature<F>> &&
        impl::proper_argument_order<impl::Signature<F>> &&
        impl::args_fit_within_bitset<impl::Signature<F>> &&
        impl::args_are_python<impl::Signature<F>> &&
        impl::return_is_python<impl::Signature<F>> &&
        impl::unqualified_return<impl::Signature<F>>
    )
struct Function : Object, Interface<Function<F>> {
private:

    /* Non-member function type. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A Python wrapper around a C++ function.

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

        vectorcallfunc call = reinterpret_cast<vectorcallfunc>(__call__);
        PyObject* pyfunc = nullptr;
        PyObject* pysignature = nullptr;
        Sig::Defaults defaults;
        std::function<typename Sig::to_value::type> func;
        Sig::Overloads overloads;
        PyObject* name = nullptr;
        PyObject* docstring = nullptr;

        /* Exposes a C++ function to Python */
        explicit PyFunction(
            Object&& name,
            Object&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : defaults(std::move(defaults)), func(std::move(func)),
            name(release(name)), docstring(release(docstring))
        {}

        /* Exposes a Python function to C++ by generating a capturing lambda wrapper,
        after a quick signature validation.  The function must exactly match the
        enclosing signature, including argument names, types, and
        posonly/kwonly/optional/variadic qualifiers.  If the function lists union types
        for one or more arguments, then only one path through the parameter list needs
        to match for the conversion to succeed.  This is called by the narrowing cast
        operator when converting a dynamic object to this function type. */
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

        ~PyFunction() noexcept {
            Py_XDECREF(pyfunc);
            Py_XDECREF(pysignature);
            Py_XDECREF(name);
            Py_XDECREF(docstring);
        }

        static void __dealloc__(PyFunction* self) noexcept {
            PyObject_GC_UnTrack(self);
            self->~PyFunction();
            Py_TYPE(self)->tp_free(self);
        }

        /* Python-level allocation function.  Initializes pointers to null, but
        otherwise does nothing in order to allow for future re-initialization.  If the
        initializer function is already an instance of this type, then it is returned
        as-is so as to avoid unnecessary nesting. */
        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
            if (self == nullptr) {
                return nullptr;
            }
            self->call = reinterpret_cast<vectorcallfunc>(__call__);
            self->pyfunc = nullptr;
            self->pysignature = nullptr;
            self->name = nullptr;
            self->docstring = nullptr;
            return reinterpret_cast<PyObject*>(self);
        }

        /* Python-level constructor.  This is called by the `@bertrand` decorator once
        this template has been deduced from a valid signature.  Additionally, since
        the `__new__()` method bypasses this constructor when the initializer is
        already an instance of this type, and C++ narrowing conversions use a dedicated
        constructor, this method will only be called for Python functions that are
        explicitly wrapped directly from Python.

        In this case, we need special logic to properly handle union types in the
        supplied signature, which must be encoded into the overload trie to allow for
        proper dispatching:

            1.  If one member of the union is a supertype of all the others, then the
                base function will use that type, and register the others as separate
                overloads that resolve to the same function.
            2.  Otherwise, we traverse each type's MRO to find the first common
                ancestor, and use that for the base function signature, with all of the
                original paths as overloads.  This eventually terminates at the dynamic
                `object` type, which is the ultimate base of all Python types.
            3.  If the common type that describes all elements of the union is not
                itself an element of the union, then we generate an implicit base
                function that raises a `TypeError` when called, and register each
                member of the union as a separate overload that corrects this.  That
                way, when the function is called, all members of the union will be
                matched against the observed arguments, falling back to the error case
                if none of them match.
            4.  In order to retain accurate static analysis, the resulting function
                will directly reference the `inspect.Signature` object of the
                initializer, even if we synthesized a new base function from a
                supertype.
         */
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

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

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
                impl::Inspect signature = {func, Sig::seed, Sig::prime};
                for (PyObject* rtype : signature.returns()) {
                    Object type = reinterpret_borrow<Object>(rtype);
                    if (!issubclass<typename Sig::Return>(type)) {
                        std::string message =
                            "overload return type '" + repr(type) + "' is not a "
                            "subclass of " + repr(Type<typename Sig::Return>());
                        PyErr_SetString(PyExc_TypeError, message.c_str());
                        return nullptr;
                    }
                }
                auto it = signature.begin();
                auto end = signature.end();
                try {
                    while (it != end) {
                        self->overloads.insert(*it, obj);
                        ++it;
                    }
                } catch (...) {
                    auto it2 = signature.begin();
                    while (it2 != it) {
                        self->overloads.remove(*it2);
                        ++it2;
                    }
                    throw;
                }
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

        /* Attach a function to a type as an instance method descriptor.  Accepts the
        type to attach to, which can be provided by calling this method as a decorator
        from Python. */
        static PyObject* method(PyFunction* self, PyObject* cls) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
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
                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    if (PyObject_SetAttr(cls, self->name, self)) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
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
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
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
                        new (descr) impl::ClassMethod(self);
                    } catch (...) {
                        Py_DECREF(descr);
                        Exception::to_python();
                        return nullptr;
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
            impl::StaticMethod* descr = reinterpret_cast<impl::StaticMethod*>(
                impl::StaticMethod::__type__.tp_alloc(&impl::StaticMethod::__type__, 0)
            );
            if (descr == nullptr) {
                return nullptr;
            }
            try {
                new (descr) impl::StaticMethod(self);
            } catch (...) {
                Py_DECREF(descr);
                Exception::to_python();
                return nullptr;
            }
            return descr;
        }

        /// TODO: .property needs to be converted into a getset descriptor that
        /// returns an unbound descriptor object.

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
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
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

        /* Call the function from Python. */
        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            /// TODO: this will have to convert all of the arguments to bertrand types
            /// before proceeding, since Python doesn't retain enough type information
            /// to disambiguate containers based on their contents, and thus the whole
            /// caching mechanism would not work correctly.  The only way to fix this
            /// is to modify call_key to do the translation before hashing the
            /// arguments and searching the trie.
            /// -> What I may be able to do in the future is implement a similar level
            /// of static analysis in Python as I currently have in C++, and then
            /// substitute the call for a private method that does not do any
            /// conversions or search the overload trie, since all of that should be
            /// able to be resolved at compile time via static analysis.  In that case,
            /// this would be a direct function call

            try {



                // forward to private API for underlying call
                return _call(self, args, nargsf, kwnames);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Implement the descriptor protocol to generate bound member functions. */
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
                size_t len = PyTuple_GET_SIZE(ptr(unbound_key));
                Object bound_key = reinterpret_steal<Object>(
                    PyTuple_New(len - 1)
                );
                if (bound_key.is(nullptr)) {
                    return nullptr;
                }

                // the first element encodes the unbound function's return type.  All
                // we need to do is replace the first index of the slice with the new
                // type and exclude the first argument from the unbound key
                PySliceObject* rtype = reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(unbound_key), 0)
                );
                PyObject* slice = PySlice_New(
                    type == Py_None ?
                        reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
                    Py_None,
                    rtype->step
                );
                if (slice == nullptr) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(ptr(bound_key), 0, slice);
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

                // finally, we pass both the unbound function and the `self` argument
                // to the bound type's normal Python constructor and return the result
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

        /* `len(function)` will get the number of overloads that are currently being
        tracked. */
        static Py_ssize_t __len__(PyFunction* self) noexcept {
            return self->overloads.data.size();
        }

        /* Index the function to resolve a specific overload, as if the function were
        being called normally.  Returns `None` if no overload can be found, indicating
        that the function will be called with its base implementation. */
        static PyObject* __getitem__(PyFunction* self, PyObject* specifier) noexcept {
            if (PyTuple_Check(specifier)) {
                Py_INCREF(specifier);
            } else {
                specifier = PyTuple_Pack(1, specifier);
                if (specifier == nullptr) {
                    return nullptr;
                }
            }
            try {
                Object key = reinterpret_steal<Object>(specifier);
                std::optional<PyObject*> func = self->overloads.get(
                    subscript_key(key)
                );
                if (func.has_value()) {
                    return Py_NewRef(func.value() ? func.value() : self);
                }
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Delete a matching overload, removing it from the overload trie. */
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
                Py_INCREF(specifier);
            } else {
                specifier = PyTuple_Pack(1, specifier);
                if (specifier == nullptr) {
                    return -1;
                }
            }
            try {
                Object key = reinterpret_steal<Object>(specifier);
                std::optional<Object> func = self->overloads.remove(
                    subscript_key(key)
                );
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* Check whether a given function is contained within the overload trie. */
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

        /* Iterate over all overloads stored in the trie. */
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

        /* Check whether an object implements this function via the descriptor
        protocol. */
        static PyObject* __instancecheck__(
            PyFunction* self,
            PyObject* instance
        ) noexcept {
            return __subclasscheck__(
                self,
                reinterpret_cast<PyObject*>(Py_TYPE(instance))
            );
        }

        /* Check whether a type implements this function via the descriptor
        protocol. */
        static PyObject* __subclasscheck__(
            PyFunction* self,
            PyObject* cls
        ) noexcept {
            constexpr auto check_intersection = [](
                this auto& check_intersection,
                PyFunction* self,
                PyObject* cls
            ) -> bool {
                if (PyType_IsSubtype(
                    reinterpret_cast<PyTypeObject*>(cls),
                    &impl::FuncIntersect::__type__
                )) {
                    return check_intersection(
                        self,
                        ptr(reinterpret_cast<impl::FuncIntersect*>(cls)->lhs)
                    ) || check_intersection(
                        self,
                        ptr(reinterpret_cast<impl::FuncIntersect*>(cls)->rhs)
                    );
                }
                return reinterpret_cast<PyObject*>(self) == cls;
            };

            try {
                /// NOTE: structural interesection types are considered subclasses of
                /// their component functions, as are exact function matches.
                if (check_intersection(self, cls)) {
                    Py_RETURN_TRUE;
                }

                /// otherwise, we check for an equivalent descriptor on the input class
                if (PyObject_HasAttr(cls, self->name)) {
                    Object attr = reinterpret_steal<Object>(
                        PyObject_GetAttr(cls, self->name)
                    );
                    if (attr.is(nullptr)) {
                        return nullptr;
                    } else if (attr.is(self) || (
                        hasattr<"__wrapped__">(attr) &&
                        getattr<"__wrapped__">(attr).is(self)
                    )) {
                        Py_RETURN_TRUE;
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Supplying a __signature__ attribute allows C++ functions to be introspected
        via the `inspect` module, just like their pure-Python equivalents. */
        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            if (self->pysignature) {
                return Py_NewRef(self->pysignature);
            }

            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }

                // if this function captures a Python function, forward to it
                if (self->pyfunc) {
                    Object signature = reinterpret_steal<Object>(PyObject_GetAttr(
                        ptr(inspect),
                        ptr(impl::template_string<"signature">())
                    ));
                    if (signature.is(nullptr)) {
                        return nullptr;
                    }
                    return PyObject_CallOneArg(
                        ptr(signature),
                        self->pyfunc
                    );
                }

                // otherwise, we need to build a signature object ourselves
                Object Signature = getattr<"Signature">(inspect);
                Object Parameter = getattr<"Parameter">(inspect);

                // build the parameter annotations
                Object tuple = PyTuple_New(Sig::n);
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
                PyObject* args[] = {nullptr, ptr(tuple), ptr(return_type)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, ptr(impl::template_string<"return_annotation">()))
                );
                return PyObject_Vectorcall(
                    ptr(Signature),
                    args + 1,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Default `repr()` demangles the function name + signature. */
        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                constexpr std::string demangled =
                    impl::demangle(typeid(Function<F>).name());
                std::string str = "<" + demangled + " at " +
                    std::to_string(reinterpret_cast<size_t>(self)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* Implements the Python constructor without any */

        /* Implements the Python call operator without any type safety checks. */
        static PyObject* _call(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                // check for overloads and forward if one is found
                if (!self->overloads.data.empty()) {
                    PyObject* overload = self->overloads.search(
                        call_key(args, nargsf, kwnames)
                    );
                    if (overload) {
                        return PyObject_Vectorcall(
                            overload,
                            args,
                            nargsf,
                            kwnames
                        );
                    }
                }

                // if this function wraps a captured Python function, then we can
                // immediately forward to it as an optimization
                if (!self->pyfunc.is(nullptr)) {
                    return PyObject_Vectorcall(
                        ptr(self->pyfunc),
                        args,
                        nargsf,
                        kwnames
                    );
                }

                /// TODO: fix the last bugs in the call operator.

                // otherwise, we fall back to the base C++ implementation, which
                // requires us to translate the Python arguments according to the
                // template signature
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyFunction* self,
                    PyObject* const* args,
                    Py_ssize_t nargsf,
                    PyObject* kwnames
                ) {
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    if constexpr (!Sig::has_args) {
                        constexpr size_t expected = Sig::n - Sig::n_kwonly;
                        if (nargs > expected) {
                            throw TypeError(
                                "expected at most " + std::to_string(expected) +
                                " positional arguments, but received " +
                                std::to_string(nargs)
                            );
                        }
                    }
                    size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
                    if constexpr (!Sig::has_kwargs) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t len;
                            const char* name = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &len
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            } else if (!Sig::callback(std::string_view(name, len))) {
                                throw TypeError(
                                    "unexpected keyword argument '" +
                                    std::string(name, len) + "'"
                                );
                            }
                        }
                    }
                    if constexpr (std::is_void_v<typename Sig::Return>) {
                        self->func(
                            {impl::Arguments<Target...>::template from_python<Is>(
                                self->defaults,
                                args,
                                nargs,
                                kwnames,
                                kwcount
                            )}...
                        );
                        Py_RETURN_NONE;
                    } else {
                        return release(to_python(
                            self->func(
                                {impl::Arguments<Target...>::template from_python<Is>(
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
                    std::make_index_sequence<Sig::n>{},
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

        static PyObject* _bind_classmethod(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            using T = impl::ArgTraits<typename Sig::template at<0>>::type;
            if (kwnames) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_classmethod() does not accept keyword arguments"
                );
                return nullptr;
            }
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
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            if (kwnames) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "_bind_staticmethod() does not accept keyword arguments"
                );
                return nullptr;
            }
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
        static bool validate_parameter(const impl::Param& param) {
            using T = Sig::template at<I>;
            return (
                param.name == impl::ArgTraits<T>::name &&
                param.kind == impl::ArgTraits<T>::kind &&
                param.value == ptr(Type<typename impl::ArgTraits<T>::type>())
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
                    impl::ArgTraits<typename Sig::Defaults::template at<J>>::name + "'"
                );
            }
            return default_value;
        }

        template <size_t I>
        static Object build_parameter(PyFunction* self, const Object& Parameter) {
            using T = Sig::template at<I>;
            using Traits = impl::ArgTraits<T>;

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

        static impl::Params<std::vector<impl::Param>> call_key(
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) {
            size_t hash = 0;
            Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
            Py_ssize_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
            std::vector<impl::Param> key;
            key.reserve(nargs + kwcount);

            for (Py_ssize_t i = 0; i < nargs; ++i) {
                PyObject* arg = args[i];
                key.emplace_back(
                    "",
                    reinterpret_cast<PyObject*>(Py_TYPE(arg)),
                    impl::ArgKind::POS
                );
                hash = impl::hash_combine(
                    hash,
                    key.back().hash(Sig::seed, Sig::prime)
                );
            }

            for (Py_ssize_t i = 0; i < kwcount; ++i) {
                PyObject* name = PyTuple_GET_ITEM(kwnames, i);
                key.emplace_back(
                    impl::get_parameter_name(name),
                    reinterpret_cast<PyObject*>(Py_TYPE(args[nargs + i])),
                    impl::ArgKind::KW
                );
                hash = impl::hash_combine(
                    hash,
                    key.back().hash(Sig::seed, Sig::prime)
                );
            }
            return {std::move(key), hash};
        }

        static impl::Params<std::vector<impl::Param>> subscript_key(
            const Object& specifier
        ) {
            size_t hash = 0;
            Py_ssize_t size = PyTuple_GET_SIZE(ptr(specifier));
            std::vector<impl::Param> key;
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
                    } else if (slice->step != Py_None) {
                        throw TypeError(
                            "keyword argument cannot have a third slice element: " +
                            repr(reinterpret_borrow<Object>(slice->step))
                        );
                    }
                    key.emplace_back(
                        name,
                        PyType_Check(slice->stop) ?
                            slice->stop :
                            reinterpret_cast<PyObject*>(Py_TYPE(slice->stop)),
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
                    key.emplace_back(
                        "",
                        PyType_Check(item) ?
                            item :
                            reinterpret_cast<PyObject*>(Py_TYPE(item)),
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

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&impl::FuncIntersect::__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&impl::FuncUnion::__or__),
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
            /// TODO: @Function.method should return a descriptor?  Or maybe just
            /// forward the function itself?
            {
                "method",
                reinterpret_cast<PyCFunction>(&method),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "_call",
                reinterpret_cast<PyCFunction>(&_call),
                METH_FASTCALL | METH_KEYWORDS,
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
            {nullptr}
        };

    };

    /* Bound member function type.  Must be constructed with a corresponding `self`
    parameter, which will be inserted as the first argument to a call according to
    Python style. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
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

        template <StaticStr ModName>
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
                    impl::demangle(Py_TYPE(self->__self__)->tp_name) + ".";
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


/// TODO: so providing the extra `self` argument would be a way to convert a static
/// function pointer into a member function pointer.  The only problem is what happens
/// if the first argument type is `std::string`?  Perhaps you need to pass the self
/// parameter as an initializer list.  Alternatively, the initializer list could be
/// necessary for the function name/docstring, which would prevent conflicts with
/// the `self` wrapper.

/// -> What if you provide self as an initializer list, together with the function
/// itself?


/*
    /// TODO: I don't know if this syntax is even possible, actually.

    auto func = py::Function(
        "subtract",
        "a simple example function",
        {
            foo,
            [](py::Arg<"x", const Foo&> x, py::Arg<"y", int>::opt y) {
                return x.value - y.value;
            }
        },
        py::arg<"y"> = 2
    );
*/


/* CTAD guides for construction from function pointer types. */
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;


/// TODO: if you pass in a static function + a self parameter, then CTAD will deduce to
/// a member function of the appropriate type.


/// TODO: modify these to account for conversion from static functions to member functions
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        (impl::Signature<Func>::has_self ?
            std::same_as<Self, typename impl::Signature<Func>::Self> :
            impl::Signature<Func>::n > 0 && std::convertible_to<
                Self,
                typename impl::ArgTraits<typename impl::Signature<Func>::template at<0>>::type
            >
        ) &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        std::same_as<Self, typename impl::Signature<Func>::Self> &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        std::same_as<Self, typename impl::Signature<Func>::Self> &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::string, std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;


/* CTAD guides for construction from function objects implementing a call operator. */
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(std::string, std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;


template <impl::inherits<impl::FunctionTag> F>
struct __template__<F> {
    using Func = std::remove_reference_t<F>;

    template <size_t I, size_t PosOnly, size_t KwOnly>
    static void get(PyObject* tuple, size_t& offset) {
        using T = Func::template at<I>;
        Type<typename impl::ArgTraits<T>::type> type;

        /// NOTE: `/` and `*` argument delimiters must be inserted where necessary to
        /// model positional-only and keyword-only arguments correctly in Python.
        if constexpr (
            (I == PosOnly) ||
            ((I == Func::n - 1) && impl::ArgTraits<T>::posonly())
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

        if constexpr (impl::ArgTraits<T>::posonly()) {
            if constexpr (impl::ArgTraits<T>::name.empty()) {
                if constexpr (impl::ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        Type<typename impl::ArgTraits<T>::type>(),
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
                        impl::ArgTraits<T>::name,
                        impl::ArgTraits<T>::name.size()
                    )
                );
                if (name.is(nullptr)) {
                    Exception::from_python();
                }
                if constexpr (impl::ArgTraits<T>::opt()) {
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

        } else if constexpr (impl::ArgTraits<T>::kw()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                impl::ArgTraits<T>::opt() ? Py_Ellipsis : Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (impl::ArgTraits<T>::args()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "*" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 1
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

        } else if constexpr (impl::ArgTraits<T>::kwargs()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "**" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 2
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
            reinterpret_borrow<Object>(Py_None) :
            Object(Type<typename impl::ArgTraits<typename Func::Return>::type>());
        if constexpr (Func::has_self) {
            PyObject* slice = PySlice_New(
                Type<typename impl::ArgTraits<typename Func::Self>::type>(),
                issubclass<Func::Self, impl::TypeTag>() ?
                    reinterpret_cast<PyObject*>(&PyType_Type) :
                    Py_None,
                ptr(rtype)
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, slice);
        } else {
            PyObject* slice = PySlice_New(
                Py_None,
                Py_None,
                ptr(rtype)
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, slice);
        }

        constexpr size_t PosOnly = Func::has_posonly ? 
            std::min({Func::args_idx, Func::kw_idx, Func::kwargs_idx}) :
            Func::n;

        []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* list
        ) {
            size_t offset = 1;
            (get<Is, PosOnly, Func::kwonly_idx>(list, offset), ...);
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
                    "' at index: " + std::to_string(I) 
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
                    "' at index " + std::to_string(I) + ", not: '" +
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
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
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
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
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
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
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
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
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
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
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
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
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
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
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
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
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
void Interface<Function<F>>::overload(this Self&& self, const Function<Func>& func) {
    /// TODO: C++ side of function overloading
}


template <typename F>
template <typename T>
void Interface<Function<F>>::method(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::classmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::staticmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::property(
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
    template <StaticStr Name, typename Self, typename... Args>
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
    template <typename Self, StaticStr Name, typename... Args>
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


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/// TODO: Object::operator()


}  // namespace py


#endif
