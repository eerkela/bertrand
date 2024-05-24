#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_OPERATORS_H
#define BERTRAND_PYTHON_COMMON_OPERATORS_H

#include "declarations.h"
#include "except.h"


namespace bertrand {
namespace py {


namespace impl {

    namespace ops {

        template <typename T>
        using to_object = std::conditional_t<python_like<T>, T, Object>;

        static const pybind11::int_ one = 1;

        namespace sequence {

            template <typename Return, typename L, typename R>
            struct add {
                static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                    PyObject* result = PySequence_Concat(lhs.ptr(), rhs.ptr());
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Return>(result);
                }
            };

            template <typename Return, typename L, typename R>
            struct iadd {
                static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                    PyObject* result = PySequence_InPlaceConcat(
                        lhs.ptr(),
                        rhs.ptr()
                    );
                    if (result == nullptr) {
                        Exception::from_python();
                    } else if (result == lhs.ptr()) {
                        Py_DECREF(result);
                    } else {
                        lhs = reinterpret_steal<L>(result);
                    }
                }
            };

            template <typename Return, typename L, typename R>
            struct mul {
                static Return operator()(const L& lhs, const R& rhs);
            };

            template <typename Return, typename L, typename R>
            struct imul {
                static void operator()(L& lhs, Py_ssize_t rhs) {
                    PyObject* result = PySequence_InPlaceRepeat(lhs.ptr(), rhs);
                    if (result == nullptr) {
                        Exception::from_python();
                    } else if (result == lhs.ptr()) {
                        Py_DECREF(result);
                    } else {
                        lhs = reinterpret_steal<L>(result);
                    }
                }
            };

        }

        template <typename Return, typename Self, typename... Args>
        struct call {
            static Return operator()(const Self& self, Args&&... args) {
                try {
                    if constexpr (std::is_void_v<Return>) {
                        Handle(self.ptr())(std::forward<Args>(args)...);
                    } else {
                        return reinterpret_steal<Return>(
                            Handle(self.ptr())(std::forward<Args>(args)...).release()
                        );
                    }
                } catch (...) {
                    Exception::from_pybind11();
                }
            }
        };

        template <typename Return, typename Self, typename Key>
        struct getitem {
            static auto operator()(const Self& self, auto&& key);
        };

        template <typename Return, typename Self, typename Key>
        struct contains {
            static bool operator()(const Self& self, const to_object<Key>& key) {
                int result = PySequence_Contains(self.ptr(), key.ptr());
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename Self>
        struct len {
            static size_t operator()(const Self& self) {
                Py_ssize_t size = PyObject_Size(self.ptr());
                if (size < 0) {
                    Exception::from_python();
                }
                return size;
            }
        };

        template <typename Return, typename Self>
        struct begin {
            static auto operator()(const Self& self);
        };

        template <typename Return, typename Self>
        struct end {
            static auto operator()(const Self& self);
        };

        template <typename Return, typename Self>
        struct rbegin {
            static auto operator()(const Self& self);
        };
    
        template <typename Return, typename Self>
        struct rend {
            static auto operator()(const Self& self);
        };

        template <typename Self>
        struct dereference {
            static auto operator()(const Self& self) {
                try {
                    return *Handle(self.ptr());
                } catch (...) {
                    Exception::from_pybind11();
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct lt {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_LT);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename L, typename R>
        struct le {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_LE);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename L, typename R>
        struct eq {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_EQ);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename L, typename R>
        struct ne {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_NE);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename L, typename R>
        struct ge {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_GE);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename L, typename R>
        struct gt {
            static bool operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                int result = PyObject_RichCompareBool(lhs.ptr(), rhs.ptr(), Py_GT);
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };

        template <typename Return, typename Self>
        struct abs {
            static Return operator()(const to_object<Self>& self) {
                PyObject* result = PyNumber_Absolute(self.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Self>
        struct invert {
            static Return operator()(const to_object<Self>& self) {
                PyObject* result = PyNumber_Invert(self.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Self>
        struct pos {
            static Return operator()(const to_object<Self>& self) {
                PyObject* result = PyNumber_Positive(self.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Self>
        struct increment {
            static void operator()(to_object<Self>& self) {
                PyObject* result = PyNumber_InPlaceAdd(self.ptr(), one.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                if (result == self.ptr()) {
                    Py_DECREF(result);
                } else {
                    self = reinterpret_steal<Return>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct add {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Add(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct iadd {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceAdd(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename Self>
        struct neg {
            static Return operator()(const to_object<Self>& self) {
                PyObject* result = PyNumber_Negative(self.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Self>
        struct decrement {
            static void operator()(to_object<Self>& self) {
                PyObject* result = PyNumber_InPlaceSubtract(self.ptr(), one.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                if (result == self.ptr()) {
                    Py_DECREF(result);
                } else {
                    self = reinterpret_steal<Return>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct sub {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Subtract(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct isub {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceAdd(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct mul {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Multiply(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct imul {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceMultiply(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct truediv {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_TrueDivide(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct itruediv {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceTrueDivide(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct floordiv {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_FloorDivide(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct ifloordiv {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceFloorDivide(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct mod {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Remainder(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct imod {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceRemainder(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename Base, typename Exp>
        struct pow {
            static Return operator()(const to_object<Base>& base, const to_object<Exp>& exp) {
                PyObject* result = PyNumber_Power(
                    base.ptr(),
                    exp.ptr(),
                    Py_None
                );
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Base, typename Exp, typename Mod>
        struct powmod {
            static Return operator()(
                const to_object<Base>& base,
                const to_object<Exp>& exp,
                const to_object<Mod>& mod
            ) {
                PyObject* result = PyNumber_Power(
                    base.ptr(),
                    exp.ptr(),
                    mod.ptr()
                );
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename Base, typename Exp>
        struct ipow {
            static void operator()(to_object<Base>& base, const to_object<Exp>& exp) {
                PyObject* result = PyNumber_InPlacePower(
                    base.ptr(),
                    exp.ptr(),
                    Py_None
                );
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == base.ptr()) {
                    Py_DECREF(result);
                } else {
                    base = reinterpret_steal<Base>(result);
                }
            }
        };

        template <typename Return, typename Base, typename Exp, typename Mod>
        struct ipowmod {
            static void operator()(
                to_object<Base>& base,
                const to_object<Exp>& exp,
                const to_object<Mod>& mod
            ) {
                PyObject* result = PyNumber_InPlacePower(
                    base.ptr(),
                    exp.ptr(),
                    mod.ptr()
                );
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == base.ptr()) {
                    Py_DECREF(result);
                } else {
                    base = reinterpret_steal<Base>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct lshift {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Lshift(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct ilshift {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceLshift(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct rshift {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Rshift(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct irshift {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceRshift(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct and_ {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_And(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct iand {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceAnd(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct or_ {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Or(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct ior {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceOr(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

        template <typename Return, typename L, typename R>
        struct xor_ {
            static Return operator()(const to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_Xor(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<Return>(result);
            }
        };

        template <typename Return, typename L, typename R>
        struct ixor {
            static void operator()(to_object<L>& lhs, const to_object<R>& rhs) {
                PyObject* result = PyNumber_InPlaceXor(lhs.ptr(), rhs.ptr());
                if (result == nullptr) {
                    Exception::from_python();
                } else if (result == lhs.ptr()) {
                    Py_DECREF(result);
                } else {
                    lhs = reinterpret_steal<L>(result);
                }
            }
        };

    }

    // NOTE: using a secondary helper struct to handle double underscore attributes
    // delays template instantiation enough to prevent ambiguities with specializations
    // in subclasses, which may be generic on either the object type and/or attribute
    // name.  Effectively, getters and setters for all underscore methods are made
    // available for free on all subclasses of py::Object.

    // TODO: setattr_helper has to account for Value type?

    // TODO: I honestly don't know whether the _helper classes are actually helpful.
    // They limit type safety to some extent, since I can't specifically type things
    // like the __init__ or __new__ methods.

    template <StaticStr name>
    struct getattr_helper {
        static constexpr bool enable = false;
    };

    template <StaticStr name>
    struct setattr_helper {
        static constexpr bool enable = false;
    };

    template <StaticStr name>
    struct delattr_helper {
        static constexpr bool enable = false;
    };

    template <> struct getattr_helper<"__dict__">           : Returns<Dict<Str, Object>> {};
    template <> struct setattr_helper<"__dict__">           : Returns<void> {};
    template <> struct delattr_helper<"__dict__">           : Returns<void> {};
    template <> struct getattr_helper<"__class__">          : Returns<Type> {};
    template <> struct setattr_helper<"__class__">          : Returns<void> {};
    template <> struct delattr_helper<"__class__">          : Returns<void> {};
    template <> struct getattr_helper<"__bases__">          : Returns<Tuple<Type>> {};
    template <> struct setattr_helper<"__bases__">          : Returns<void> {};
    template <> struct delattr_helper<"__bases__">          : Returns<void> {};
    template <> struct getattr_helper<"__name__">           : Returns<Str> {};
    template <> struct setattr_helper<"__name__">           : Returns<void> {};
    template <> struct delattr_helper<"__name__">           : Returns<void> {};
    template <> struct getattr_helper<"__qualname__">       : Returns<Str> {};
    template <> struct setattr_helper<"__qualname__">       : Returns<void> {};
    template <> struct delattr_helper<"__qualname__">       : Returns<void> {};
    template <> struct getattr_helper<"__type_params__">    : Returns<Object> {};  // type?
    template <> struct setattr_helper<"__type_params__">    : Returns<void> {};
    template <> struct delattr_helper<"__type_params__">    : Returns<void> {};
    template <> struct getattr_helper<"__mro__">            : Returns<Tuple<Type>> {};
    template <> struct setattr_helper<"__mro__">            : Returns<void> {};
    template <> struct delattr_helper<"__mro__">            : Returns<void> {};
    template <> struct getattr_helper<"__subclasses__">     : Returns<Function<
        List<Type>()
    >> {};
    template <> struct setattr_helper<"__subclasses__">     : Returns<void> {};
    template <> struct delattr_helper<"__subclasses__">     : Returns<void> {};
    template <> struct getattr_helper<"__doc__">            : Returns<Str> {};
    template <> struct setattr_helper<"__doc__">            : Returns<void> {};
    template <> struct delattr_helper<"__doc__">            : Returns<void> {};
    template <> struct getattr_helper<"__module__">         : Returns<Str> {};
    template <> struct setattr_helper<"__module__">         : Returns<void> {};
    template <> struct delattr_helper<"__module__">         : Returns<void> {};
    template <> struct getattr_helper<"__new__">            : Returns<Function<
        Object(  // TODO: It would be better if this yielded the correct type
            Arg<"cls", const Type&>,
            Arg<"args", const Object&>::args,
            Arg<"kwargs", const Object&>::kwargs
        )
    >> {};
    template <> struct setattr_helper<"__new__">            : Returns<void> {};
    template <> struct delattr_helper<"__new__">            : Returns<void> {};
    template <> struct getattr_helper<"__init__">           : Returns<Function<
        void(
            Arg<"args", const Object&>::args,
            Arg<"kwargs", const Object&>::kwargs
        )
    >> {};
    template <> struct setattr_helper<"__init__">           : Returns<void> {};
    template <> struct delattr_helper<"__init__">           : Returns<void> {};
    template <> struct getattr_helper<"__del__">            : Returns<Function<
        void()
    >> {};
    template <> struct setattr_helper<"__del__">            : Returns<void> {};
    template <> struct delattr_helper<"__del__">            : Returns<void> {};
    template <> struct getattr_helper<"__repr__">           : Returns<Function<
        Str()
    >> {};
    template <> struct setattr_helper<"__repr__">           : Returns<void> {};
    template <> struct delattr_helper<"__repr__">           : Returns<void> {};
    template <> struct getattr_helper<"__str__">            : Returns<Function<
        Str()
    >> {};
    template <> struct setattr_helper<"__str__">            : Returns<void> {};
    template <> struct delattr_helper<"__str__">            : Returns<void> {};
    template <> struct getattr_helper<"__bytes__">          : Returns<Function<
        Bytes()
    >> {};
    template <> struct setattr_helper<"__bytes__">          : Returns<void> {};
    template <> struct delattr_helper<"__bytes__">          : Returns<void> {};
    template <> struct getattr_helper<"__format__">         : Returns<Function<
        Str(Arg<"format_spec", const Str&>)
    >> {};
    template <> struct setattr_helper<"__format__">         : Returns<void> {};
    template <> struct delattr_helper<"__format__">         : Returns<void> {};
    template <> struct getattr_helper<"__bool__">           : Returns<Function<
        bool()
    >> {};
    template <> struct setattr_helper<"__bool__">           : Returns<void> {};
    template <> struct delattr_helper<"__bool__">           : Returns<void> {};
    template <> struct getattr_helper<"__dir__">            : Returns<Function<
        List<Str>()
    >> {};
    template <> struct setattr_helper<"__dir__">            : Returns<void> {};
    template <> struct delattr_helper<"__dir__">            : Returns<void> {};
    template <> struct getattr_helper<"__get__">            : Returns<Function<
        Object(  // TODO: check this
            Arg<"instance", const Object&>,
            Arg<"cls", const Type&>
        )
    >> {};
    template <> struct setattr_helper<"__get__">            : Returns<void> {};
    template <> struct delattr_helper<"__get__">            : Returns<void> {};
    template <> struct getattr_helper<"__set__">            : Returns<Function<
        void(  // TODO: check this
            Arg<"instance", const Object&>,
            Arg<"value", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__set__">            : Returns<void> {};
    template <> struct delattr_helper<"__set__">            : Returns<void> {};
    template <> struct getattr_helper<"__delete__">         : Returns<Function<
        void(Arg<"instance", const Object&>)  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__delete__">         : Returns<void> {};
    template <> struct delattr_helper<"__delete__">         : Returns<void> {};
    template <> struct getattr_helper<"__self__">           : Returns<Object> {};
    template <> struct setattr_helper<"__self__">           : Returns<void> {};
    template <> struct delattr_helper<"__self__">           : Returns<void> {};
    template <> struct getattr_helper<"__wrapped__">        : Returns<Object> {};
    template <> struct setattr_helper<"__wrapped__">        : Returns<void> {};
    template <> struct delattr_helper<"__wrapped__">        : Returns<void> {};
    template <> struct getattr_helper<"__objclass__">       : Returns<Object> {};
    template <> struct setattr_helper<"__objclass__">       : Returns<void> {};
    template <> struct delattr_helper<"__objclass__">       : Returns<void> {};
    template <> struct getattr_helper<"__slots__">          : Returns<Object> {};
    template <> struct setattr_helper<"__slots__">          : Returns<void> {};
    template <> struct delattr_helper<"__slots__">          : Returns<void> {};
    template <> struct getattr_helper<"__init_subclass__">  : Returns<Function<
        void(
            Arg<"args", const Object&>::args,
            Arg<"kwargs", const Object&>::kwargs
        )
    >> {};
    template <> struct setattr_helper<"__init_subclass__">  : Returns<void> {};
    template <> struct delattr_helper<"__init_subclass__">  : Returns<void> {};
    template <> struct getattr_helper<"__set_name__">       : Returns<Function<
        void(  // TODO: check this
            Arg<"name", const Str&>,
            Arg<"value", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__set_name__">       : Returns<void> {};
    template <> struct delattr_helper<"__set_name__">       : Returns<void> {};
    template <> struct getattr_helper<"__instancecheck__">  : Returns<Function<
        bool(Arg<"instance", const Object&>)
    >> {};
    template <> struct setattr_helper<"__instancecheck__">  : Returns<void> {};
    template <> struct delattr_helper<"__instancecheck__">  : Returns<void> {};
    template <> struct getattr_helper<"__subclasscheck__">  : Returns<Function<
        bool(
            Arg<"self", const Object&>,
            Arg<"subclass", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__subclasscheck__">  : Returns<void> {};
    template <> struct delattr_helper<"__subclasscheck__">  : Returns<void> {};
    template <> struct getattr_helper<"__class_getitem__">  : Returns<Function<
        Object(Arg<"item", const Object&>)
    >> {};
    template <> struct setattr_helper<"__class_getitem__">  : Returns<void> {};
    template <> struct delattr_helper<"__class_getitem__">  : Returns<void> {};
    template <> struct getattr_helper<"__complex__">        : Returns<Function<
        Complex()
    >> {};
    template <> struct setattr_helper<"__complex__">        : Returns<void> {};
    template <> struct delattr_helper<"__complex__">        : Returns<void> {};
    template <> struct getattr_helper<"__int__">            : Returns<Function<
        Int()
    >> {};
    template <> struct setattr_helper<"__int__">            : Returns<void> {};
    template <> struct delattr_helper<"__int__">            : Returns<void> {};
    template <> struct getattr_helper<"__float__">          : Returns<Function<
        Float()
    >> {};
    template <> struct setattr_helper<"__float__">          : Returns<void> {};
    template <> struct delattr_helper<"__float__">          : Returns<void> {};
    template <> struct getattr_helper<"__index__">          : Returns<Function<
        Int()
    >> {};
    template <> struct setattr_helper<"__index__">          : Returns<void> {};
    template <> struct delattr_helper<"__index__">          : Returns<void> {};
    template <> struct getattr_helper<"__enter__">          : Returns<Function<
        Object()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__enter__">          : Returns<void> {};
    template <> struct delattr_helper<"__enter__">          : Returns<void> {};
    template <> struct getattr_helper<"__exit__">           : Returns<Function<
        void(  // TODO: check this
            Arg<"exc_type", const Type&>,
            Arg<"exc_value", const Object&>,
            Arg<"traceback", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__exit__">           : Returns<void> {};
    template <> struct delattr_helper<"__exit__">           : Returns<void> {};
    template <> struct getattr_helper<"__match_args__">     : Returns<Tuple<Object>> {};
    template <> struct setattr_helper<"__match_args__">     : Returns<void> {};
    template <> struct delattr_helper<"__match_args__">     : Returns<void> {};
    template <> struct getattr_helper<"__buffer__">         : Returns<Function<
        Buffer()  // TODO: check this.  Also, implement Buffer/MemoryView
    >> {};
    template <> struct setattr_helper<"__buffer__">         : Returns<void> {};
    template <> struct delattr_helper<"__buffer__">         : Returns<void> {};
    template <> struct getattr_helper<"__release_buffer__"> : Returns<Function<
        void()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__release_buffer__"> : Returns<void> {};
    template <> struct delattr_helper<"__release_buffer__"> : Returns<void> {};
    template <> struct getattr_helper<"__await__">          : Returns<Function<
        Object()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__await__">          : Returns<void> {};
    template <> struct delattr_helper<"__await__">          : Returns<void> {};
    template <> struct getattr_helper<"__aiter__">          : Returns<Function<
        Object()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__aiter__">          : Returns<void> {};
    template <> struct delattr_helper<"__aiter__">          : Returns<void> {};
    template <> struct getattr_helper<"__anext__">          : Returns<Function<
        Object()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__anext__">          : Returns<void> {};
    template <> struct delattr_helper<"__anext__">          : Returns<void> {};
    template <> struct getattr_helper<"__aenter__">         : Returns<Function<
        Object()  // TODO: check this
    >> {};
    template <> struct setattr_helper<"__aenter__">         : Returns<void> {};
    template <> struct delattr_helper<"__aenter__">         : Returns<void> {};
    template <> struct getattr_helper<"__aexit__">          : Returns<Function<
        void(  // TODO: check this
            Arg<"exc_type", const Type&>,
            Arg<"exc_value", const Object&>,
            Arg<"traceback", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__aexit__">          : Returns<void> {};
    template <> struct delattr_helper<"__aexit__">          : Returns<void> {};

    template <typename L, typename R>
    concept object_operand =
        std::derived_from<L, Object> || std::derived_from<R, Object>;

    template <typename T>
    struct unwrap_proxy_helper {
        using type = T;
    };

    template <proxy_like T>
    struct unwrap_proxy_helper<T> {
        using type = typename T::Wrapped;
    };

    template <typename T>
    using unwrap_proxy = typename unwrap_proxy_helper<T>::type;

    /* Standardized error message for type narrowing via pybind11 accessors or the
    generic Object wrapper. */
    template <typename Derived>
    TypeError noconvert(PyObject* obj) {
        std::ostringstream msg;
        msg << "cannot convert python object from type '"
            << Py_TYPE(obj)->tp_name
            << "' to type '"
            << reinterpret_cast<PyTypeObject*>(Derived::type.ptr())->tp_name
            << "'";
        return TypeError(msg.str());
    }

}


/////////////////////////
////    AS OBJECT    ////
/////////////////////////


template <std::derived_from<Object> T>
struct __as_object__<T> : Returns<T> {};

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
// template <std::derived_from<pybind11::function> T>
// struct __as_object__<T> : Returns<Function<Object(Args<Object>, Kwargs<Object>)>> {};

template <>
struct __as_object__<std::nullptr_t> : Returns<NoneType> {};
template <>
struct __as_object__<std::nullopt_t> : Returns<NoneType> {};
template <std::derived_from<pybind11::none> T>
struct __as_object__<T> : Returns<NoneType> {};

template <std::derived_from<pybind11::ellipsis> T>
struct __as_object__<T> : Returns<EllipsisType> {};

template <std::derived_from<pybind11::slice> T>
struct __as_object__<T> : Returns<Slice> {};

template <std::derived_from<pybind11::module_> T>
struct __as_object__<T> : Returns<Module> {};

template <>
struct __as_object__<bool> : Returns<Bool> {};
template <std::derived_from<pybind11::bool_> T>
struct __as_object__<T> : Returns<Bool> {};

template <std::integral T> requires (!std::same_as<bool, T>)
struct __as_object__<T> : Returns<Int> {};
template <std::derived_from<pybind11::int_> T>
struct __as_object__<T> : Returns<Int> {};

template <std::floating_point T>
struct __as_object__<T> : Returns<Float> {};
template <std::derived_from<pybind11::float_> T>
struct __as_object__<T> : Returns<Float> {};

template <impl::complex_like T> requires (!std::derived_from<T, Object>)
struct __as_object__<T> : Returns<Complex> {};

template <>
struct __as_object__<const char*> : Returns<Str> {};
template <size_t N>
struct __as_object__<const char(&)[N]> : Returns<Str> {};
template <std::derived_from<std::string> T>
struct __as_object__<T> : Returns<Str> {};
template <std::derived_from<std::string_view> T>
struct __as_object__<T> : Returns<Str> {};
template <std::derived_from<pybind11::str> T>
struct __as_object__<T> : Returns<Str> {};

template <>
struct __as_object__<void*> : Returns<Bytes> {};
template <std::derived_from<pybind11::bytes> T>
struct __as_object__<T> : Returns<Bytes> {};

template <std::derived_from<pybind11::bytearray> T>
struct __as_object__<T> : Returns<ByteArray> {};

template <typename... Args>
struct __as_object__<std::chrono::duration<Args...>> : Returns<Timedelta> {};

// TODO: std::time_t?

template <typename... Args>
struct __as_object__<std::chrono::time_point<Args...>> : Returns<Datetime> {};

template <typename First, typename Second>
struct __as_object__<std::pair<First, Second>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename... Args>
struct __as_object__<std::tuple<Args...>> : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename T, size_t N>
struct __as_object__<std::array<T, N>> : Returns<Tuple<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::tuple> T>
struct __as_object__<T> : Returns<Tuple<Object>> {};

template <typename T, typename... Args>
struct __as_object__<std::vector<T, Args...>> : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::deque<T, Args...>> : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::list<T, Args...>> : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::forward_list<T, Args...>> : Returns<List<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::list> T>
struct __as_object__<T> : Returns<List<Object>> {};

template <typename T, typename... Args>
struct __as_object__<std::unordered_set<T, Args...>> : Returns<Set<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::set<T, Args...>> : Returns<Set<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::set> T>
struct __as_object__<T> : Returns<Set<Object>> {};

template <std::derived_from<pybind11::frozenset> T>
struct __as_object__<T> : Returns<FrozenSet<Object>> {};

template <typename K, typename V, typename... Args>
struct __as_object__<std::unordered_map<K, V, Args...>> : Returns<Dict<impl::as_object_t<K>, impl::as_object_t<V>>> {};
template <typename K, typename V, typename... Args>
struct __as_object__<std::map<K, V, Args...>> : Returns<Dict<impl::as_object_t<K>, impl::as_object_t<V>>> {};
template <std::derived_from<pybind11::dict> T>
struct __as_object__<T> : Returns<Dict<Object, Object>> {};

template <std::derived_from<pybind11::type> T>
struct __as_object__<T> : Returns<Type> {};


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
auto as_object(T&& value) -> __as_object__<std::remove_cvref_t<T>>::Return {
    return std::forward<T>(value);
}


////////////////////////////////////
////    IMPLICIT CONVERSIONS    ////
////////////////////////////////////


/* Implicitly convert all objects to pybind11::handle. */
template <typename Self>
struct __cast__<Self, pybind11::handle> : Returns<pybind11::handle> {
    static pybind11::handle operator()(const Self& self) {
        return self.ptr();
    }
};


/* Implicitly convert all objects to pybind11::object */
template <typename Self>
struct __cast__<Self, pybind11::object> : Returns<pybind11::object> {
    static pybind11::object operator()(const Self& self) {
        return pybind11::reinterpret_borrow<pybind11::object>(self.ptr());
    }
};


/* Implicitly convert all objects to equivalent pybind11 type. */
template <typename Self, impl::pybind11_like T> requires (Self::template check<T>())
struct __cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return pybind11::reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert all objects to one of their subclasses by applying a type check. */
template <typename Self, std::derived_from<Self> T>
struct __cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        if (!T::check(self)) {
            throw impl::noconvert<T>(self.ptr());
        }
        return reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert all objects to a proxy by converting to its wrapped type and then
moving the result into a managed buffer. */
template <typename Self, impl::proxy_like T>
    requires (__cast__<Self, impl::unwrap_proxy<T>>::enable)
struct __cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return T(__cast__<Self, impl::unwrap_proxy<T>>::operator()(self));
    }
};


////////////////////
////    CALL    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__call__">           : Returns<Function<
        Object(  // TODO: ideally, this would return the same type as the __call__ struct?
            Arg<"args", const Object&>::args,
            Arg<"kwargs", const Object&>::kwargs
        )
    >> {};
    template <> struct setattr_helper<"__call__">           : Returns<void> {};
    template <> struct delattr_helper<"__call__">           : Returns<void> {};
}


template <typename T, typename... Args> requires (impl::proxy_like<Args> || ...)
struct __call__<T, Args...> : __call__<T, impl::unwrap_proxy<Args>...> {};
template <impl::proxy_like T, typename... Args>
struct __call__<T, Args...> : __call__<impl::unwrap_proxy<T>, Args...> {};
template <impl::proxy_like T, typename... Args> requires (impl::proxy_like<Args> || ...)
struct __call__<T, Args...> : __call__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Args>...> {};



////////////////////
////    ATTR    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__getattr__">        : Returns<Function<
        Object(Arg<"name", const Str&>)
    >> {};
    template <> struct setattr_helper<"__getattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__getattr__">        : Returns<void> {};

    template <> struct getattr_helper<"__getattribute__">   : Returns<Function<
        Object(Arg<"name", const Str&>)
    >> {};
    template <> struct setattr_helper<"__getattribute__">   : Returns<void> {};
    template <> struct delattr_helper<"__getattribute__">   : Returns<void> {};

    template <> struct getattr_helper<"__setattr__">        : Returns<Function<
        void(
            Arg<"name", const Str&>,
            Arg<"value", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__setattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__setattr__">        : Returns<void> {};

    template <> struct getattr_helper<"__delattr__">        : Returns<Function<
        void(Arg<"name", const Str&>)
    >> {};
    template <> struct setattr_helper<"__delattr__">        : Returns<void> {};
    template <> struct delattr_helper<"__delattr__">        : Returns<void> {};
}



template <std::derived_from<Object> T, StaticStr name> requires (impl::getattr_helper<name>::enable)
struct __getattr__<T, name> : Returns<typename impl::getattr_helper<name>::Return> {};
template <impl::proxy_like T, StaticStr name>
struct __getattr__<T, name> : __getattr__<impl::unwrap_proxy<T>, name> {};


template <std::derived_from<Object> T, StaticStr name, typename Value> requires (impl::setattr_helper<name>::enable)
struct __setattr__<T, name, Value> : Returns<typename impl::setattr_helper<name>::Return> {};
template <impl::proxy_like T, StaticStr name, impl::not_proxy_like Value>
struct __setattr__<T, name, Value> : __setattr__<impl::unwrap_proxy<T>, name, Value> {};
template <impl::not_proxy_like T, StaticStr name, impl::proxy_like Value>
struct __setattr__<T, name, Value> : __setattr__<T, name, impl::unwrap_proxy<Value>> {};
template <impl::proxy_like T, StaticStr name, impl::proxy_like Value>
struct __setattr__<T, name, Value> : __setattr__<impl::unwrap_proxy<T>, name, impl::unwrap_proxy<Value>> {};


template <std::derived_from<Object> T, StaticStr name> requires (impl::delattr_helper<name>::enable)
struct __delattr__<T, name> : Returns<typename impl::delattr_helper<name>::Return> {};
template <impl::proxy_like T, StaticStr name>
struct __delattr__<T, name> : __delattr__<impl::unwrap_proxy<T>, name> {};


////////////////////
////    ITEM    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__getitem__">        : Returns<Function<
        Object(Arg<"key", const Object&>)
    >> {};
    template <> struct setattr_helper<"__getitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__getitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__setitem__">        : Returns<Function<
        void(
            Arg<"key", const Object&>,
            Arg<"value", const Object&>
        )
    >> {};
    template <> struct setattr_helper<"__setitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__setitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__delitem__">        : Returns<Function<
        void(Arg<"key", const Object&>)
    >> {};
    template <> struct setattr_helper<"__delitem__">        : Returns<void> {};
    template <> struct delattr_helper<"__delitem__">        : Returns<void> {};

    template <> struct getattr_helper<"__missing__">        : Returns<Function<
        Object(Arg<"key", const Object&>)
    >> {};
    template <> struct setattr_helper<"__missing__">        : Returns<void> {};
    template <> struct delattr_helper<"__missing__">        : Returns<void> {};
}



template <impl::proxy_like T, impl::not_proxy_like Key>
struct __getitem__<T, Key> : __getitem__<impl::unwrap_proxy<T>, Key> {};
template <impl::not_proxy_like T, impl::proxy_like Key>
struct __getitem__<T, Key> : __getitem__<T, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like T, impl::proxy_like Key>
struct __getitem__<T, Key> : __getitem__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Key>> {};


template <impl::proxy_like T, impl::not_proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<impl::unwrap_proxy<T>, Key, Value> {};
template <impl::not_proxy_like T, impl::proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<T, impl::unwrap_proxy<Key>, Value> {};
template <impl::not_proxy_like T, impl::not_proxy_like Key, impl::proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<T, Key, impl::unwrap_proxy<Value>> {};
template <impl::proxy_like T, impl::proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Key>, Value> {};
template <impl::proxy_like T, impl::not_proxy_like Key, impl::proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<impl::unwrap_proxy<T>, Key, Value> {};
template <impl::not_proxy_like T, impl::proxy_like Key, impl::proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<T, impl::unwrap_proxy<Key>, Value> {};
template <impl::proxy_like T, impl::proxy_like Key, impl::proxy_like Value>
struct __setitem__<T, Key, Value> : __setitem__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Key>, impl::unwrap_proxy<Value>> {};


template <impl::proxy_like T, impl::not_proxy_like Key>
struct __delitem__<T, Key> : __delitem__<impl::unwrap_proxy<T>, Key> {};
template <impl::not_proxy_like T, impl::proxy_like Key>
struct __delitem__<T, Key> : __delitem__<T, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like T, impl::proxy_like Key>
struct __delitem__<T, Key> : __delitem__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Key>> {};


////////////////////
////    SIZE    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__len__">            : Returns<Function<
        Int()
    >> {};
    template <> struct setattr_helper<"__len__">            : Returns<void> {};
    template <> struct delattr_helper<"__len__">            : Returns<void> {};

    template <> struct getattr_helper<"__length_hint__">    : Returns<Function<
        Int()
    >> {};
    template <> struct setattr_helper<"__length_hint__">    : Returns<void> {};
    template <> struct delattr_helper<"__length_hint__">    : Returns<void> {};
}



template <impl::proxy_like T>
struct __len__<T> : __len__<impl::unwrap_proxy<T>> {};


////////////////////
////    ITER    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__iter__">           : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__iter__">           : Returns<void> {};
    template <> struct delattr_helper<"__iter__">           : Returns<void> {};

    template <> struct getattr_helper<"__next__">           : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__next__">           : Returns<void> {};
    template <> struct delattr_helper<"__next__">           : Returns<void> {};

    template <> struct getattr_helper<"__reversed__">       : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__reversed__">       : Returns<void> {};
    template <> struct delattr_helper<"__reversed__">       : Returns<void> {};
}



template <impl::proxy_like T>
struct __iter__<T> : __iter__<impl::unwrap_proxy<T>> {};


template <impl::proxy_like T>
struct __reversed__<T> : __reversed__<impl::unwrap_proxy<T>> {};


////////////////////////
////    CONTAINS    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__contains__">       : Returns<Function<
        Bool(Arg<"key", const Object&>)
    >> {};
    template <> struct setattr_helper<"__contains__">       : Returns<void> {};
    template <> struct delattr_helper<"__contains__">       : Returns<void> {};
}



template <impl::proxy_like T, impl::not_proxy_like Key>
struct __contains__<T, Key> : __contains__<impl::unwrap_proxy<T>, Key> {};
template <impl::not_proxy_like T, impl::proxy_like Key>
struct __contains__<T, Key> : __contains__<T, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like T, impl::proxy_like Key>
struct __contains__<T, Key> : __contains__<impl::unwrap_proxy<T>, impl::unwrap_proxy<Key>> {};


///////////////////////////
////    DEREFERENCE    ////
///////////////////////////


template <typename Self> requires (__iter__<Self>::enable)
auto operator*(const Self& self) {
    if constexpr (impl::proxy_like<Self>) {
        return *self.value();
    } else {
        return impl::ops::dereference<Self>::operator()(self);
    }
}


////////////////////
////    HASH    ////
////////////////////


namespace impl {
    template <> struct getattr_helper<"__hash__">           : Returns<Function<
        Int(Arg<"self", const Object&>)
    >> {};
    template <> struct setattr_helper<"__hash__">           : Returns<void> {};
    template <> struct delattr_helper<"__hash__">           : Returns<void> {};
}



template <impl::proxy_like T>
struct __hash__<T> : __hash__<impl::unwrap_proxy<T>> {};


///////////////////
////    ABS    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__abs__">            : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__abs__">            : Returns<void> {};
    template <> struct delattr_helper<"__abs__">            : Returns<void> {};
}



template <impl::proxy_like T>
struct __abs__<T> : __abs__<impl::unwrap_proxy<T>> {};


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename Self> requires (__abs__<Self>::enable)
auto abs(const Self& self) {
    using Return = __abs__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Absolute value operator must return a py::Object subclass.  Check your "
        "specialization of __abs__ for this type and ensure the Return type is set to "
        "a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return abs(self.value());
    } else {
        return impl::ops::abs<Return, Self>::operator()(self);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <impl::has_abs T> requires (!__abs__<T>::enable)
auto abs(const T& value) {
    return std::abs(value);
}


//////////////////////
////    INVERT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__invert__">         : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__invert__">         : Returns<void> {};
    template <> struct delattr_helper<"__invert__">         : Returns<void> {};
}



template <impl::proxy_like T>
struct __invert__<T> : __invert__<impl::unwrap_proxy<T>> {};


template <typename Self> requires (__invert__<Self>::enable)
auto operator~(const Self& self) {
    using Return = typename __invert__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise NOT operator must return a py::Object subclass.  Check your "
        "specialization of __invert__ for this type and ensure the Return type "
        "is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return ~self.value();
    } else {
        return impl::ops::invert<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> T> requires (!__invert__<T>::enable)
auto operator~(const T& self) = delete;


////////////////////////
////    POSITIVE    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__pos__">            : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__pos__">            : Returns<void> {};
    template <> struct delattr_helper<"__pos__">            : Returns<void> {};
}



template <impl::proxy_like T>
struct __pos__<T> : __pos__<impl::unwrap_proxy<T>> {};


template <typename Self> requires (__pos__<Self>::enable)
auto operator+(const Self& self) {
    using Return = typename __pos__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary positive operator must return a py::Object subclass.  Check "
        "your specialization of __pos__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return +self.value();
    } else {
        return impl::ops::pos<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> T> requires (!__pos__<T>::enable)
auto operator+(const T& self) = delete;


////////////////////////
////    NEGATIVE    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__neg__">            : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__neg__">            : Returns<void> {};
    template <> struct delattr_helper<"__neg__">            : Returns<void> {};
}



template <impl::proxy_like T>
struct __neg__<T> : __neg__<impl::unwrap_proxy<T>> {};


template <typename Self> requires (__neg__<Self>::enable)
auto operator-(const Self& self) {
    using Return = typename __neg__<Self>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Unary negative operator must return a py::Object subclass.  Check "
        "your specialization of __neg__ for this type and ensure the Return "
        "type is set to a py::Object subclass."
    );
    if constexpr (impl::proxy_like<Self>) {
        return -self.value();
    } else {
        return impl::ops::neg<Return, Self>::operator()(self);
    }
}


template <std::derived_from<Object> T> requires (!__neg__<T>::enable)
auto operator-(const T& self) = delete;


/////////////////////////
////    INCREMENT    ////
/////////////////////////



template <impl::proxy_like T>
struct __increment__<T> : __increment__<impl::unwrap_proxy<T>> {};


template <typename Self> requires (__increment__<Self>::enable)
Self& operator++(Self& self) {
    using Return = typename __increment__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<Self>) {
        ++self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Increment operator requires a Python object."
        );
        impl::ops::increment<Return, Self>::operator()(self);
    }
    return self;
}


template <typename Self> requires (__increment__<Self>::enable)
Self operator++(Self& self, int) {
    using Return = typename __increment__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Increment operator must return a reference to the derived type.  "
        "Check your specialization of __increment__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::proxy_like<Self>) {
        ++self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Increment operator requires a Python object."
        );
        impl::ops::increment<Return, Self>::operator()(self);
    }
    return copy;
}


template <std::derived_from<Object> T> requires (!__increment__<T>::enable)
T& operator++(T& self) = delete;


template <std::derived_from<Object> T> requires (!__increment__<T>::enable)
T operator++(T& self, int) = delete;


/////////////////////////
////    DECREMENT    ////
/////////////////////////



template <impl::proxy_like T>
struct __decrement__<T> : __decrement__<impl::unwrap_proxy<T>> {};


template <typename Self> requires (__decrement__<Self>::enable)
Self& operator--(Self& self) {
    using Return = typename __decrement__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    if constexpr (impl::proxy_like<Self>) {
        --self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Decrement operator requires a Python object."
        );
        impl::ops::decrement<Return, Self>::operator()(self);
    }
    return self;
}


template <typename Self> requires (__decrement__<Self>::enable)
Self operator--(Self& self, int) {
    using Return = typename __decrement__<Self>::Return;
    static_assert(
        std::same_as<Return, Self>,
        "Decrement operator must return a reference to the derived type.  "
        "Check your specialization of __decrement__ for this type and ensure "
        "the Return type is set to the derived type."
    );
    Self copy = self;
    if constexpr (impl::proxy_like<Self>) {
        --self.value();
    } else {
        static_assert(
            impl::python_like<Self>,
            "Decrement operator requires a Python object."
        );
        impl::ops::decrement<Return, Self>::operator()(self);
    }
    return copy;
}


template <std::derived_from<Object> T> requires (!__decrement__<T>::enable)
T& operator--(T& self) = delete;


template <std::derived_from<Object> T> requires (!__decrement__<T>::enable)
T operator--(T& self, int) = delete;


/////////////////////////
////    LESS-THAN    ////
/////////////////////////


namespace impl {
    template <> struct getattr_helper<"__lt__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__lt__">             : Returns<void> {};
    template <> struct delattr_helper<"__lt__">             : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __lt__<L, R> : __lt__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __lt__<L, R> : __lt__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lt__<L, R> : __lt__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) {
    using Return = typename __lt__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than operator must return a boolean value.  Check your "
        "specialization of __lt__ for these types and ensure the Return type "
        "is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() < rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs < rhs.value();
    } else {
        return impl::ops::lt<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__lt__<L, R>::enable)
auto operator<(const L& lhs, const R& rhs) = delete;


//////////////////////////////////
////    LESS-THAN-OR-EQUAL    ////
//////////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__le__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__le__">             : Returns<void> {};
    template <> struct delattr_helper<"__le__">             : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __le__<L, R> : __le__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __le__<L, R> : __le__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __le__<L, R> : __le__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) {
    using Return = typename __le__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Less-than-or-equal operator must return a boolean value.  Check your "
        "specialization of __le__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() <= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs <= rhs.value();
    } else {
        return impl::ops::le<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__le__<L, R>::enable)
auto operator<=(const L& lhs, const R& rhs) = delete;


/////////////////////
////    EQUAL    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__eq__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__eq__">             : Returns<void> {};
    template <> struct delattr_helper<"__eq__">             : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __eq__<L, R> : __eq__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __eq__<L, R> : __eq__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __eq__<L, R> : __eq__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) {
    using Return = typename __eq__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Equality operator must return a boolean value.  Check your "
        "specialization of __eq__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() == rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs == rhs.value();
    } else {
        return impl::ops::eq<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__eq__<L, R>::enable)
auto operator==(const L& lhs, const R& rhs) = delete;


////////////////////////
////   NOT-EQUAL    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__ne__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ne__">             : Returns<void> {};
    template <> struct delattr_helper<"__ne__">             : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __ne__<L, R> : __ne__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ne__<L, R> : __ne__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ne__<L, R> : __ne__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) {
    using Return = typename __ne__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Inequality operator must return a boolean value.  Check your "
        "specialization of __ne__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() != rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs != rhs.value();
    } else {
        return impl::ops::ne<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__ne__<L, R>::enable)
auto operator!=(const L& lhs, const R& rhs) = delete;


/////////////////////////////////////
////    GREATER-THAN-OR-EQUAL    ////
/////////////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__ge__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ge__">             : Returns<void> {};
    template <> struct delattr_helper<"__ge__">             : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __ge__<L, R> : __ge__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ge__<L, R> : __ge__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ge__<L, R> : __ge__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) {
    using Return = typename __ge__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than-or-equal operator must return a boolean value.  Check "
        "your specialization of __ge__ for this type and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >= rhs.value();
    } else {
        return impl::ops::ge<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__ge__<L, R>::enable)
auto operator>=(const L& lhs, const R& rhs) = delete;


////////////////////////////
////    GREATER-THAN    ////
////////////////////////////


namespace impl {
    template <> struct getattr_helper<"__gt__">             : Returns<Function<
        Bool(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__gt__">             : Returns<void> {};
    template <> struct delattr_helper<"__gt__">             : Returns<void> {};
}


template <impl::proxy_like L, impl::not_proxy_like R>
struct __gt__<L, R> : __gt__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __gt__<L, R> : __gt__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __gt__<L, R> : __gt__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


template <typename L, typename R> requires (__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) {
    using Return = typename __gt__<L, R>::Return;
    static_assert(
        std::same_as<Return, bool>,
        "Greater-than operator must return a boolean value.  Check your "
        "specialization of __gt__ for this type and ensure the Return type is "
        "set to bool."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() > rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs > rhs.value();
    } else {
        return impl::ops::gt<Return, L, R>::operator()(lhs, rhs);
    }
}


template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__gt__<L, R>::enable)
auto operator>(const L& lhs, const R& rhs) = delete;


///////////////////
////    ADD    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__add__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__add__">            : Returns<void> {};
    template <> struct delattr_helper<"__add__">            : Returns<void> {};

    template <> struct getattr_helper<"__radd__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__radd__">           : Returns<void> {};
    template <> struct delattr_helper<"__radd__">           : Returns<void> {};

    template <> struct getattr_helper<"__iadd__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__iadd__">           : Returns<void> {};
    template <> struct delattr_helper<"__iadd__">           : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __add__<L, R> : __add__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __add__<L, R> : __add__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __add__<L, R> : __add__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) {
    using Return = typename __add__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Addition operator must return a py::Object subclass.  Check your "
        "specialization of __add__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() + rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs + rhs.value();
    } else {
        return impl::ops::add<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__add__<L, R>::enable)
auto operator+(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __iadd__<L, R> : __iadd__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __iadd__<L, R> : __iadd__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iadd__<L, R> : __iadd__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__iadd__<L, R>::enable)
L& operator+=(L& lhs, const R& rhs) {
    using Return = typename __iadd__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iadd__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() += rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs += rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place addition operator requires a Python object."
        );
        impl::ops::iadd<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__iadd__<L, R>::enable)
auto operator+=(const L& lhs, const R& rhs) = delete;


////////////////////////
////    SUBTRACT    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__sub__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__sub__">            : Returns<void> {};
    template <> struct delattr_helper<"__sub__">            : Returns<void> {};

    template <> struct getattr_helper<"__rsub__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rsub__">           : Returns<void> {};
    template <> struct delattr_helper<"__rsub__">           : Returns<void> {};

    template <> struct getattr_helper<"__isub__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__isub__">           : Returns<void> {};
    template <> struct delattr_helper<"__isub__">           : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __sub__<L, R> : __sub__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __sub__<L, R> : __sub__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __sub__<L, R> : __sub__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) {
    using Return = typename __sub__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Subtraction operator must return a py::Object subclass.  Check your "
        "specialization of __sub__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() - rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs - rhs.value();
    } else {
        return impl::ops::sub<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__sub__<L, R>::enable)
auto operator-(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __isub__<L, R> : __isub__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __isub__<L, R> : __isub__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __isub__<L, R> : __isub__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__isub__<L, R>::enable)
L& operator-=(L& lhs, const R& rhs) {
    using Return = typename __isub__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place addition operator must return a mutable reference to the left "
        "operand.  Check your specialization of __isub__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() -= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs -= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place addition operator requires a Python object."
        );
        impl::ops::isub<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__isub__<L, R>::enable)
auto operator-=(const L& lhs, const R& rhs) = delete;


////////////////////////
////    MULTIPLY    ////
////////////////////////


namespace impl {
    template <> struct getattr_helper<"__mul__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__mul__">            : Returns<void> {};
    template <> struct delattr_helper<"__mul__">            : Returns<void> {};

    template <> struct getattr_helper<"__matmul__">         : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__matmul__">         : Returns<void> {};
    template <> struct delattr_helper<"__matmul__">         : Returns<void> {};

    template <> struct getattr_helper<"__rmul__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rmul__">           : Returns<void> {};
    template <> struct delattr_helper<"__rmul__">           : Returns<void> {};

    template <> struct getattr_helper<"__rmatmul__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rmatmul__">        : Returns<void> {};
    template <> struct delattr_helper<"__rmatmul__">        : Returns<void> {};

    template <> struct getattr_helper<"__imul__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__imul__">           : Returns<void> {};
    template <> struct delattr_helper<"__imul__">           : Returns<void> {};

    template <> struct getattr_helper<"__imatmul__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__imatmul__">        : Returns<void> {};
    template <> struct delattr_helper<"__imatmul__">        : Returns<void> {};
}


template <impl::proxy_like L, impl::not_proxy_like R>
struct __mul__<L, R> : __mul__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __mul__<L, R> : __mul__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mul__<L, R> : __mul__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) {
    using Return = typename __mul__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Multiplication operator must return a py::Object subclass.  Check "
        "your specialization of __mul__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() * rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs * rhs.value();
    } else {
        return impl::ops::mul<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__mul__<L, R>::enable)
auto operator*(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __imul__<L, R> : __imul__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __imul__<L, R> : __imul__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imul__<L, R> : __imul__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__imul__<L, R>::enable)
L& operator*=(L& lhs, const R& rhs) {
    using Return = typename __imul__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place multiplication operator must return a mutable reference to the "
        "left operand.  Check your specialization of __imul__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() *= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs *= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place multiplication operator requires a Python object."
        );
        impl::ops::imul<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__imul__<L, R>::enable)
auto operator*=(const L& lhs, const R& rhs) = delete;


//////////////////////
////    DIVIDE    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__truediv__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__truediv__">        : Returns<void> {};
    template <> struct delattr_helper<"__truediv__">        : Returns<void> {};

    template <> struct getattr_helper<"__floordiv__">       : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__floordiv__">       : Returns<void> {};
    template <> struct delattr_helper<"__floordiv__">       : Returns<void> {};

    template <> struct getattr_helper<"__rtruediv__">       : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rtruediv__">       : Returns<void> {};
    template <> struct delattr_helper<"__rtruediv__">       : Returns<void> {};

    template <> struct getattr_helper<"__rfloordiv__">      : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rfloordiv__">      : Returns<void> {};
    template <> struct delattr_helper<"__rfloordiv__">      : Returns<void> {};

    template <> struct getattr_helper<"__itruediv__">       : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__itruediv__">       : Returns<void> {};
    template <> struct delattr_helper<"__itruediv__">       : Returns<void> {};

    template <> struct getattr_helper<"__ifloordiv__">      : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ifloordiv__">      : Returns<void> {};
    template <> struct delattr_helper<"__ifloordiv__">      : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __truediv__<L, R> : __truediv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __truediv__<L, R> : __truediv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __truediv__<L, R> : __truediv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) {
    using Return = typename __truediv__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "True division operator must return a py::Object subclass.  Check "
        "your specialization of __truediv__ for this type and ensure the "
        "Return type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() / rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs / rhs.value();
    } else {
        return impl::ops::truediv<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__truediv__<L, R>::enable)
auto operator/(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __itruediv__<L, R> : __itruediv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __itruediv__<L, R> : __itruediv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __itruediv__<L, R> : __itruediv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__itruediv__<L, R>::enable)
L& operator/=(L& lhs, const R& rhs) {
    using Return = typename __itruediv__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place true division operator must return a mutable reference to the "
        "left operand.  Check your specialization of __itruediv__ for these "
        "types and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() /= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs /= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place true division operator requires a Python object."
        );
        impl::ops::itruediv<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__itruediv__<L, R>::enable)
auto operator/=(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __floordiv__<L, R> : __floordiv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __floordiv__<L, R> : __floordiv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __floordiv__<L, R> : __floordiv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};


///////////////////////
////    MODULUS    ////
///////////////////////


namespace impl {
    template <> struct getattr_helper<"__mod__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__mod__">            : Returns<void> {};
    template <> struct delattr_helper<"__mod__">            : Returns<void> {};

    template <> struct getattr_helper<"__divmod__">         : Returns<Function<
        Tuple<Object>(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__divmod__">         : Returns<void> {};
    template <> struct delattr_helper<"__divmod__">         : Returns<void> {};

    template <> struct getattr_helper<"__rmod__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rmod__">           : Returns<void> {};
    template <> struct delattr_helper<"__rmod__">           : Returns<void> {};

    template <> struct getattr_helper<"__rdivmod__">        : Returns<Function<
        Tuple<Object>(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rdivmod__">        : Returns<void> {};
    template <> struct delattr_helper<"__rdivmod__">        : Returns<void> {};

    template <> struct getattr_helper<"__imod__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__imod__">           : Returns<void> {};
    template <> struct delattr_helper<"__imod__">           : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __mod__<L, R> : __mod__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __mod__<L, R> : __mod__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mod__<L, R> : __mod__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) {
    using Return = typename __mod__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Modulus operator must return a py::Object subclass.  Check your "
        "specialization of __mod__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() % rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs % rhs.value();
    } else {
        return impl::ops::mod<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__mod__<L, R>::enable)
auto operator%(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __imod__<L, R> : __imod__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __imod__<L, R> : __imod__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imod__<L, R> : __imod__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__imod__<L, R>::enable)
L& operator%=(L& lhs, const R& rhs) {
    using Return = typename __imod__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place modulus operator must return a mutable reference to the left "
        "operand.  Check your specialization of __imod__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() %= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs %= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place modulus operator requires a Python object."
        );
        impl::ops::imod<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__imod__<L, R>::enable)
auto operator%=(const L& lhs, const R& rhs) = delete;


/////////////////////
////    ROUND    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__round__">          : Returns<Function<
        Object(Arg<"ndigits", const Int&>::opt)
    >> {};
    template <> struct setattr_helper<"__round__">          : Returns<void> {};
    template <> struct delattr_helper<"__round__">          : Returns<void> {};

    template <> struct getattr_helper<"__trunc__">          : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__trunc__">          : Returns<void> {};
    template <> struct delattr_helper<"__trunc__">          : Returns<void> {};

    template <> struct getattr_helper<"__floor__">          : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__floor__">          : Returns<void> {};
    template <> struct delattr_helper<"__floor__">          : Returns<void> {};

    template <> struct getattr_helper<"__ceil__">           : Returns<Function<
        Object()
    >> {};
    template <> struct setattr_helper<"__ceil__">           : Returns<void> {};
    template <> struct delattr_helper<"__ceil__">           : Returns<void> {};
}


/////////////////////
////    POWER    ////
/////////////////////


namespace impl {
    template <> struct getattr_helper<"__pow__">            : Returns<Function<
        Object(
            Arg<"exp", const Object&>,
            Arg<"mod", const Object&>::opt
        )
    >> {};
    template <> struct setattr_helper<"__pow__">            : Returns<void> {};
    template <> struct delattr_helper<"__pow__">            : Returns<void> {};

    template <> struct getattr_helper<"__rpow__">           : Returns<Function<
        Object(Arg<"base", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rpow__">           : Returns<void> {};
    template <> struct delattr_helper<"__rpow__">           : Returns<void> {};

    template <> struct getattr_helper<"__ipow__">           : Returns<Function<
        Object(
            Arg<"exp", const Object&>,
            Arg<"mod", const Object&>::opt
        )
    >> {};
    template <> struct setattr_helper<"__ipow__">           : Returns<void> {};
    template <> struct delattr_helper<"__ipow__">           : Returns<void> {};
}



template <impl::proxy_like base, impl::not_proxy_like exponent>
struct __pow__<base, exponent> : __pow__<impl::unwrap_proxy<base>, exponent> {};
template <impl::not_proxy_like base, impl::proxy_like exponent>
struct __pow__<base, exponent> : __pow__<base, impl::unwrap_proxy<exponent>> {};
template <impl::proxy_like base, impl::proxy_like exponent>
struct __pow__<base, exponent> : __pow__<impl::unwrap_proxy<base>, impl::unwrap_proxy<exponent>> {};


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp> requires (__pow__<Base, Exp>::enable)
auto pow(const Base& base, const Exp& exp) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value());
    } else {
        return impl::ops::pow<Return, Base, Exp>::operator()(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <typename Base, typename Exp> requires (!impl::object_operand<Base, Exp>)
auto pow(const Base& base, const Exp& exponent) {
    if constexpr (impl::complex_like<Base> && impl::complex_like<Exp>) {
        return std::common_type_t<Base, Exp>(
            std::pow(base.real(), exponent.real()),
            std::pow(base.imag(), exponent.imag())
        );
    } else if constexpr (impl::complex_like<Base>) {
        return Base(
            std::pow(base.real(), exponent),
            std::pow(base.imag(), exponent)
        );
    } else if constexpr (impl::complex_like<Exp>) {
        return Exp(
            std::pow(base, exponent.real()),
            std::pow(base, exponent.imag())
        );
    } else {
        return std::pow(base, exponent);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <impl::int_like Base, impl::int_like Exp, impl::int_like Mod>
    requires (__pow__<Base, Exp>::enable)
auto pow(const Base& base, const Exp& exp, const Mod& mod) {
    using Return = typename __pow__<Base, Exp>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "pow() must return a py::Object subclass.  Check your specialization "
        "of __pow__ for this type and ensure the Return type is derived from "
        "py::Object."
    );
    if constexpr (impl::proxy_like<Base>) {
        return pow(base.value(), exp, mod);
    } else if constexpr (impl::proxy_like<Exp>) {
        return pow(base, exp.value(), mod);
    } else {
        return impl::ops::powmod<Return, Base, Exp, Mod>::operator()(base, exp, mod);
    }
}


// TODO: enable C++ equivalent for modular exponentiation

// /* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
// semantics. */
// template <std::integral Base, std::integral Exp, std::integral Mod>
// auto pow(Base base, Exp exp, Mod mod) {
//     std::common_type_t<Base, Exp, Mod> result = 1;
//     base = py::mod(base, mod);
//     while (exp > 0) {
//         if (exp % 2) {
//             result = py::mod(result * base, mod);
//         }
//         exp >>= 1;
//         base = py::mod(base * base, mod);
//     }
//     return result;
// }


//////////////////////
////    LSHIFT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__lshift__">         : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__lshift__">         : Returns<void> {};
    template <> struct delattr_helper<"__lshift__">         : Returns<void> {};

    template <> struct getattr_helper<"__rlshift__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rlshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__rlshift__">        : Returns<void> {};

    template <> struct getattr_helper<"__ilshift__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ilshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__ilshift__">        : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __lshift__<L, R> : __lshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __lshift__<L, R> : __lshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lshift__<L, R> : __lshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R>
    requires (__lshift__<L, R>::enable && !std::derived_from<L, std::ostream>)
auto operator<<(const L& lhs, const R& rhs) {
    using Return = typename __lshift__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Left shift operator must return a py::Object subclass.  Check your "
        "specialization of __lshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() << rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs << rhs.value();
    } else {
        return impl::ops::lshift<Return, L, R>::operator()(lhs, rhs);
    }
}

template <std::derived_from<std::ostream> L, std::derived_from<Object> R>
L& operator<<(L& os, const R& obj) {
    PyObject* repr = PyObject_Repr(obj.ptr());
    if (repr == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(repr, &size);
    if (data == nullptr) {
        Py_DECREF(repr);
        Exception::from_python();
    }
    os.write(data, size);
    Py_DECREF(repr);
    return os;
}

template <std::derived_from<std::ostream> L, impl::proxy_like T>
L& operator<<(L& os, const T& proxy) {
    os << proxy.value();
    return os;
}

template <typename L, typename R>
    requires (
        !__lshift__<L, R>::enable &&
        impl::object_operand<L, R> &&
        !std::derived_from<L, std::ostream>
    )
auto operator<<(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __ilshift__<L, R> : __ilshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ilshift__<L, R> : __ilshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ilshift__<L, R> : __ilshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__ilshift__<L, R>::enable)
L& operator<<=(L& lhs, const R& rhs) {
    using Return = typename __ilshift__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place left shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ilshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() <<= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs <<= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place left shift operator requires a Python object."
        );
        impl::ops::ilshift<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__ilshift__<L, R>::enable)
auto operator<<=(const L& lhs, const R& rhs) = delete;


//////////////////////
////    RSHIFT    ////
//////////////////////


namespace impl {
    template <> struct getattr_helper<"__rshift__">         : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rshift__">         : Returns<void> {};
    template <> struct delattr_helper<"__rshift__">         : Returns<void> {};

    template <> struct getattr_helper<"__rrshift__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rrshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__rrshift__">        : Returns<void> {};

    template <> struct getattr_helper<"__irshift__">        : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__irshift__">        : Returns<void> {};
    template <> struct delattr_helper<"__irshift__">        : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __rshift__<L, R> : __rshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __rshift__<L, R> : __rshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __rshift__<L, R> : __rshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) {
    using Return = typename __rshift__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Right shift operator must return a py::Object subclass.  Check your "
        "specialization of __rshift__ for this type and ensure the Return "
        "type is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() >> rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs >> rhs.value();
    } else {
        return impl::ops::rshift<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__rshift__<L, R>::enable)
auto operator>>(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __irshift__<L, R> : __irshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __irshift__<L, R> : __irshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __irshift__<L, R> : __irshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__irshift__<L, R>::enable)
L& operator>>=(L& lhs, const R& rhs) {
    using Return = typename __irshift__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place right shift operator must return a mutable reference to the left "
        "operand.  Check your specialization of __irshift__ for these types "
        "and ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() >>= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs >>= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place right shift operator requires a Python object."
        );
        impl::ops::irshift<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__irshift__<L, R>::enable)
auto operator>>=(const L& lhs, const R& rhs) = delete;


///////////////////
////    AND    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__and__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__and__">            : Returns<void> {};
    template <> struct delattr_helper<"__and__">            : Returns<void> {};

    template <> struct getattr_helper<"__rand__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rand__">           : Returns<void> {};
    template <> struct delattr_helper<"__rand__">           : Returns<void> {};

    template <> struct getattr_helper<"__iand__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__iand__">           : Returns<void> {};
    template <> struct delattr_helper<"__iand__">           : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __and__<L, R> : __and__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __and__<L, R> : __and__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __and__<L, R> : __and__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) {
    using Return = typename __and__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise AND operator must return a py::Object subclass.  Check your "
        "specialization of __and__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() & rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs & rhs.value();
    } else {
        return impl::ops::and_<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__and__<L, R>::enable)
auto operator&(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __iand__<L, R> : __iand__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __iand__<L, R> : __iand__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iand__<L, R> : __iand__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__iand__<L, R>::enable)
L& operator&=(L& lhs, const R& rhs) {
    using Return = typename __iand__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise AND operator must return a mutable reference to the left "
        "operand.  Check your specialization of __iand__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() &= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs &= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise AND operator requires a Python object."
        );
        impl::ops::iand<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__iand__<L, R>::enable)
auto operator&=(const L& lhs, const R& rhs) = delete;


//////////////////
////    OR    ////
//////////////////


namespace impl {
    template <> struct getattr_helper<"__or__">             : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__or__">             : Returns<void> {};
    template <> struct delattr_helper<"__or__">             : Returns<void> {};

    template <> struct getattr_helper<"__ror__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ror__">            : Returns<void> {};
    template <> struct delattr_helper<"__ror__">            : Returns<void> {};

    template <> struct getattr_helper<"__ior__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ior__">            : Returns<void> {};
    template <> struct delattr_helper<"__ior__">            : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __or__<L, R> : __or__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __or__<L, R> : __or__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __or__<L, R> : __or__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R>
    requires (__or__<L, R>::enable && !std::ranges::view<R>)
auto operator|(const L& lhs, const R& rhs) {
    using Return = typename __or__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise OR operator must return a py::Object subclass.  Check your "
        "specialization of __or__ for this type and ensure the Return type is "
        "derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() | rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs | rhs.value();
    } else {
        return impl::ops::or_<Return, L, R>::operator()(lhs, rhs);
    }
}

template <std::derived_from<Object> L, std::ranges::view R>
auto operator|(const L& container, const R& view) {
    return std::views::all(container) | view;
}

// TODO: should this be enabled?  Does it cause a lifetime issue?
// template <impl::proxy_like L, std::ranges::view R>
// auto operator|(const L& container, const R& view) {
//     return container.value() | view;
// }

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__or__<L, R>::enable)
auto operator|(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __ior__<L, R> : __ior__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ior__<L, R> : __ior__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ior__<L, R> : __ior__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__ior__<L, R>::enable)
L& operator|=(L& lhs, const R& rhs) {
    using Return = typename __ior__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise OR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ior__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() |= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs |= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise OR operator requires a Python object."
        );
        impl::ops::ior<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__ior__<L, R>::enable)
auto operator|=(const L& lhs, const R& rhs) = delete;


///////////////////
////    XOR    ////
///////////////////


namespace impl {
    template <> struct getattr_helper<"__xor__">            : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__xor__">            : Returns<void> {};
    template <> struct delattr_helper<"__xor__">            : Returns<void> {};

    template <> struct getattr_helper<"__rxor__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__rxor__">           : Returns<void> {};
    template <> struct delattr_helper<"__rxor__">           : Returns<void> {};

    template <> struct getattr_helper<"__ixor__">           : Returns<Function<
        Object(Arg<"other", const Object&>)
    >> {};
    template <> struct setattr_helper<"__ixor__">           : Returns<void> {};
    template <> struct delattr_helper<"__ixor__">           : Returns<void> {};
}



template <impl::proxy_like L, impl::not_proxy_like R>
struct __xor__<L, R> : __xor__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __xor__<L, R> : __xor__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __xor__<L, R> : __xor__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) {
    using Return = typename __xor__<L, R>::Return;
    static_assert(
        std::derived_from<Return, Object>,
        "Bitwise XOR operator must return a py::Object subclass.  Check your "
        "specialization of __xor__ for this type and ensure the Return type "
        "is derived from py::Object."
    );
    if constexpr (impl::proxy_like<L>) {
        return lhs.value() ^ rhs;
    } else if constexpr (impl::proxy_like<R>) {
        return lhs ^ rhs.value();
    } else {
        return impl::ops::xor_<Return, L, R>::operator()(lhs, rhs);
    }
}

template <typename L, typename R>
    requires (impl::object_operand<L, R> && !__xor__<L, R>::enable)
auto operator^(const L& lhs, const R& rhs) = delete;



template <impl::proxy_like L, impl::not_proxy_like R>
struct __ixor__<L, R> : __ixor__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ixor__<L, R> : __ixor__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ixor__<L, R> : __ixor__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};

template <typename L, typename R> requires (__ixor__<L, R>::enable)
L& operator^=(L& lhs, const R& rhs) {
    using Return = typename __ixor__<L, R>::Return;
    static_assert(
        std::same_as<Return, L&>,
        "In-place bitwise XOR operator must return a mutable reference to the left "
        "operand.  Check your specialization of __ixor__ for these types and "
        "ensure the Return type is set to the left operand."
    );
    if constexpr (impl::proxy_like<L>) {
        lhs.value() ^= rhs;
    } else if constexpr (impl::proxy_like<R>) {
        lhs ^= rhs.value();
    } else {
        static_assert(
            impl::python_like<L>,
            "In-place bitwise XOR operator requires a Python object."
        );
        impl::ops::ixor<Return, L, R>::operator()(lhs, rhs);
    }
    return lhs;
}

template <std::derived_from<Object> L, typename R> requires (!__ixor__<L, R>::enable)
auto operator^=(const L& lhs, const R& rhs) = delete;


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_OPERATORS_H
