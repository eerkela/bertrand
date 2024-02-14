#ifndef BERTRAND_PYBIND_H
#define BERTRAND_PYBIND_H

#include <any>
#include <chrono>
#include <complex>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Python.h>
#include <datetime.h>  // Python datetime library
#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
#include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>


/* NOTE: this uses slightly different syntax from normal pybind11 in order to support
 * more of the Python standard library and to replicate its semantics more closely.
 * The only real differences are that types are capitalized in order to avoid
 * extraneous underscores and conflicts with non-member functions of the same name, and
 * they implement the same interface as the Python types they represent, without
 * requiring `attr()` syntax.  Containers can also be directly constructed using
 * initializer lists, and a wider variety of global methods are supported.
 *
 * For example, the following pybind11 code:
 *
 *     py::list foo = py::cast(std::vector<int>{1, 2, 3});
 *     py::int_ bar = foo.attr("pop")();
 *
 * would be written as:
 *
 *     py::List foo {1, 2, 3};
 *     py::Int bar = foo.pop();
 *
 * Which is more concise and readable.
 */


/* TODO: at some point in the future, we could potentially support static typing for
 * python containers:
 *
 *     py::List foo {1, 2, 3};
 *     py::TypedList<int> bar = foo;
 *
 * List would just be a type alias for `TypedList<void>`, which would enable dynamic
 * typing.  If specialized on a subclass of `pybind11::object`, we would apply an
 * isinstance check whenever an item is added to the list.  If specialized on a C++
 * type, then we could use the compiler to enable static checks.  This would add a lot
 * of code, but it would be really awesome for cross-language type safety.  We could
 * also potentially optimize for C++ types that are supported in the CPython API.  But
 * again, that would create a lot of extra code.
 */


using namespace pybind11::literals;
namespace bertrand {
namespace py {


/////////////////////////////////////////
////     INHERITED FROM PYBIND11     ////
/////////////////////////////////////////


// exceptions
using pybind11::error_already_set;
using TypeError = pybind11::type_error;
using ValueError = pybind11::value_error;
using KeyError = pybind11::key_error;
using IndexError = pybind11::index_error;
using BufferError = pybind11::buffer_error;
using CastError = pybind11::cast_error;
using ReferenceCastError = pybind11::reference_cast_error;
using ImportError = pybind11::import_error;
using StopIteration = pybind11::stop_iteration;

// TODO: NotImplementedError, others

// wrapper types
template <typename... Args>
using Class = pybind11::class_<Args...>;
using Module = pybind11::module_;
using Handle = pybind11::handle;
using Object = pybind11::object;
using Iterator = pybind11::iterator;
using Iterable = pybind11::iterable;
using AnySet = pybind11::anyset;
using NoneType = pybind11::none;
using EllipsisType = pybind11::ellipsis;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Sequence = pybind11::sequence;
using Buffer = pybind11::buffer;
using MemoryView = pybind11::memoryview;
using Bytes = pybind11::bytes;
using Bytearray = pybind11::bytearray;
class NotImplemented;  // done
class Bool;  // done
class Int;  // done
class Float;  // done
class Complex;  // done
class Slice;  // done
class Range;  // done
class List;  // done
class Tuple;  // done
class Args;  // done
class Set;  // done
class FrozenSet;  // done
class KeysView;  // done
class ItemsView;  // done
class ValuesView;  // done
class Dict;  // done
class Kwargs;  // done
class MappingProxy;  // done
class Str;  // done
class Type;  // done
class Function;
class Method;
class ClassMethod;
class StaticMethod;
class Property;


// python builtins
const static NoneType None;
const static EllipsisType Ellipsis;
using pybind11::globals;
using pybind11::exec;
using pybind11::eval;
using pybind11::eval_file;
using pybind11::isinstance;
using pybind11::hasattr;
using pybind11::delattr;
using pybind11::getattr;
using pybind11::setattr;
using pybind11::hash;
using pybind11::len;
using pybind11::len_hint;
using pybind11::repr;
using pybind11::print;
using pybind11::iter;


// binding functions
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
using pybind11::implicitly_convertible;
using pybind11::cast;
using pybind11::reinterpret_borrow;
using pybind11::reinterpret_steal;
using pybind11::args_are_all_keyword_or_ds;
using pybind11::make_tuple;
using pybind11::make_iterator;
using pybind11::make_key_iterator;
using pybind11::make_value_iterator;
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
using pybind11::cpp_function;
using pybind11::scoped_ostream_redirect;
using pybind11::scoped_estream_redirect;
using pybind11::add_ostream_redirect;


// annotations
using pybind11::is_method;
using pybind11::is_setter;
using pybind11::is_operator;
using pybind11::is_final;
using pybind11::scope;
using pybind11::doc;
using pybind11::name;
using pybind11::sibling;
using pybind11::base;
using pybind11::keep_alive;
using pybind11::multiple_inheritance;
using pybind11::dynamic_attr;
using pybind11::buffer_protocol;
using pybind11::metaclass;
using pybind11::custom_type_setup;
using pybind11::module_local;
using pybind11::arithmetic;
using pybind11::prepend;
using pybind11::call_guard;
using pybind11::arg;
using pybind11::arg_v;
using pybind11::kw_only;
using pybind11::pos_only;

namespace detail = pybind11::detail;


/* A simple struct that converts a generic C++ object into a Python equivalent in
its constructor.  This is used in conjunction with std::initializer_list to parse
mixed-type lists in a type-safe manner.

NOTE: this incurs a small performance penalty, effectively requiring an extra loop
over the initializer list before the function is called.  Users can avoid this by
providing a homogenous `std::initializer_list<T>` overload alongside
`std::iniitializer_list<Initializer>`.  This causes conversions to be deferred to
the function body, which may be able to handle them more efficiently in a single
loop. */
struct Initializer {
    Object value;

    /* We disable initializer construction from pybind11::handle in order not to
     * interfere with pybind11's reinterpret_borrow/reinterpret_steal helper functions,
     * which use initializer lists internally.  Disabling the generic initializer list
     * constructor whenever a handle is passed in allows reinterpret_borrow/
     * reinterpret_steal to continue to work as expected.
     */

    template <typename T, std::enable_if_t<!std::is_same_v<pybind11::handle, std::decay_t<T>>, int> = 0>
    Initializer(T&& value) : value(detail::object_or_cast(std::forward<T>(value))) {}
};


///////////////////////////////
////    WRAPPER CLASSES    ////
///////////////////////////////


// impl namespace is different from detail in order to avoid conflicts with pybind11
namespace impl {

    /* Convert an arbitrary Python/C++ value into a pybind11 object and then return a
    new reference.  This is used in the constructors of list and tuple objects, whose
    SET_ITEM() functions steal a reference.  Converts null pointers into Python None. */
    template <typename T>
    static inline PyObject* convert_newref(T&& value) {
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = value.ptr();
            if (result == nullptr) {
                result = Py_None;
            }
            return Py_NewRef(result);
        } else {
            PyObject* result = py::cast(std::forward<T>(value)).release().ptr();
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }
    }

    /* SFINAE trait that detects whether a C++ container has an STL-style `size()`
    method. */
    template <typename T, typename = void>
    constexpr bool has_size = false;
    template <typename T>
    constexpr bool has_size<T, std::void_t<decltype(std::declval<T>().size())>> = true;

    /* Mixin holding operator overloads for numeric types.  For some reason, pybind11
    enables these operators, but does not allow implicit conversion from C++ operands,
    so we override them here. */
    template <typename Derived>
    struct NumericOps {

        #define BINARY_OPERATOR(op, endpoint)                                           \
            template <typename T>                                                       \
            inline Object op(T&& other) const {                                         \
                PyObject* result = PyNumber_##endpoint(                                 \
                    static_cast<const Derived*>(this)->ptr(),                           \
                    detail::object_or_cast(std::forward<T>(other)).ptr()                \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                return reinterpret_steal<Object>(result);                               \
            }                                                                           \

        #define REVERSE_OPERATOR(op, endpoint)                                          \
            template <                                                                  \
                typename T,                                                             \
                std::enable_if_t<                                                       \
                    !std::is_base_of_v<Derived, std::decay_t<T>>,                       \
                    int                                                                 \
                > = 0                                                                   \
            >                                                                           \
            inline friend Object op(T&& other, const Derived& self) {                   \
                PyObject* result = PyNumber_##endpoint(                                 \
                    detail::object_or_cast(std::forward<T>(other)).ptr(),               \
                    self.ptr()                                                          \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                return reinterpret_steal<Object>(result);                               \
            }                                                                           \

        #define INPLACE_OPERATOR(op, endpoint)                                          \
            template <typename T>                                                       \
            inline Object& op(T&& other) {                                              \
                Derived* self = static_cast<const Derived*>(this);                      \
                PyObject* result = PyNumber_InPlace##endpoint(                          \
                    self->ptr(),                                                        \
                    detail::object_or_cast(std::forward<T>(other)).ptr()                \
                );                                                                      \
                if (result == nullptr) {                                                \
                    throw error_already_set();                                          \
                }                                                                       \
                if (result == self->ptr()) {                                            \
                    Py_DECREF(result);                                                  \
                } else {                                                                \
                    *this = reinterpret_steal<Object>(result);                          \
                }                                                                       \
                return *this;                                                           \
            }                                                                           \

        BINARY_OPERATOR(operator+, Add);
        BINARY_OPERATOR(operator-, Subtract);
        BINARY_OPERATOR(operator*, Multiply);
        BINARY_OPERATOR(operator/, TrueDivide);
        BINARY_OPERATOR(operator%, Remainder);
        BINARY_OPERATOR(operator<<, Lshift);
        BINARY_OPERATOR(operator>>, Rshift);
        BINARY_OPERATOR(operator&, And);
        BINARY_OPERATOR(operator|, Or);
        BINARY_OPERATOR(operator^, Xor);
        REVERSE_OPERATOR(operator+, Add);
        REVERSE_OPERATOR(operator-, Subtract);
        REVERSE_OPERATOR(operator*, Multiply);
        REVERSE_OPERATOR(operator/, TrueDivide);
        REVERSE_OPERATOR(operator%, Remainder);
        REVERSE_OPERATOR(operator<<, Lshift);
        REVERSE_OPERATOR(operator>>, Rshift);
        REVERSE_OPERATOR(operator&, And);
        REVERSE_OPERATOR(operator|, Or);
        REVERSE_OPERATOR(operator^, Xor);
        INPLACE_OPERATOR(operator+=, Add);
        INPLACE_OPERATOR(operator-=, Subtract);
        INPLACE_OPERATOR(operator*=, Multiply);
        INPLACE_OPERATOR(operator/=, TrueDivide);
        INPLACE_OPERATOR(operator%=, Remainder);
        INPLACE_OPERATOR(operator<<=, Lshift);
        INPLACE_OPERATOR(operator>>=, Rshift);
        INPLACE_OPERATOR(operator&=, And);
        INPLACE_OPERATOR(operator|=, Or);
        INPLACE_OPERATOR(operator^=, Xor);

        #undef BINARY_OPERATOR
        #undef REVERSE_OPERATOR
        #undef INPLACE_OPERATOR
    };

    /* Mixin holding operator overloads for types implementing the sequence protocol,
    which makes them both concatenatable and repeatable. */
    template <typename Derived>
    struct SequenceOps {

        /* Equivalent to Python `sequence.count(value)`, but also takes optional
        start/stop indices similar to `sequence.index()`. */
        template <typename T>
        inline Py_ssize_t count(
            T&& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(
                    static_cast<const Derived*>(this)->ptr(),
                    start,
                    stop
                );
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Count(
                    slice,
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Count(
                    static_cast<const Derived*>(this)->ptr(),
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

        /* Equivalent to Python `s.index(value[, start[, stop]])`. */
        template <typename T>
        inline Py_ssize_t index(
            T&& value,
            Py_ssize_t start = 0,
            Py_ssize_t stop = -1
        ) const {
            if (start != 0 || stop != -1) {
                PyObject* slice = PySequence_GetSlice(
                    static_cast<const Derived*>(this)->ptr(),
                    start,
                    stop
                );
                if (slice == nullptr) {
                    throw error_already_set();
                }
                Py_ssize_t result = PySequence_Index(
                    slice,
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                Py_DECREF(slice);
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            } else {
                Py_ssize_t result = PySequence_Index(
                    static_cast<const Derived*>(this)->ptr(),
                    detail::object_or_cast(std::forward<T>(value)).ptr()
                );
                if (result == -1 && PyErr_Occurred()) {
                    throw error_already_set();
                }
                return result;
            }
        }

        /* Equivalent to Python `sequence + items`. */
        template <typename T>
        inline Derived concat(T&& items) const {
            PyObject* result = PySequence_Concat(
                static_cast<const Derived*>(this)->ptr(),
                detail::object_or_cast(std::forward<T>(items)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        /* Equivalent to Python `sequence * repetitions`. */
        inline Derived repeat(Py_ssize_t repetitions) const {
            PyObject* result = PySequence_Repeat(
                static_cast<const Derived*>(this)->ptr(),
                repetitions
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Derived>(result);
        }

        template <typename T>
        inline Derived operator+(T&& items) const {
            return static_cast<const Derived*>(this)->concat(std::forward<T>(items));
        }

        inline Derived operator*(Py_ssize_t repetitions) {
            return static_cast<const Derived*>(this)->repeat(repetitions);
        }

        friend inline Derived operator*(Py_ssize_t repetitions, const Derived& seq) {
            return seq.repeat(repetitions);
        }

        template <typename T>
        inline Derived& operator+=(T&& items) {
            Derived* self = static_cast<Derived*>(this);
            PyObject* result = PySequence_InPlaceConcat(
                self->ptr(),
                detail::object_or_cast(std::forward<T>(items)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self->ptr()) {
                Py_DECREF(result);
            } else {
                *self = reinterpret_steal<Derived>(result);
            }
            return *self;
        }

        inline Derived& operator*=(Py_ssize_t repetitions) {
            Derived* self = static_cast<Derived*>(this);
            PyObject* result = PySequence_InPlaceRepeat(self->ptr(), repetitions);
            if (result == nullptr) {
                throw error_already_set();
            }
            if (result == self->ptr()) {
                Py_DECREF(result);
            } else {
                *self = reinterpret_steal<Derived>(result);
            }
            return *self;
        }

        /* Slice operator.  Accepts an initializer list that expands to a Python slice
        object with generalized syntax. */
        using SliceIndex = std::variant<long long, pybind11::none>;
        inline auto operator[](std::initializer_list<SliceIndex> slice) const {
            if (slice.size() > 3) {
                throw ValueError(
                    "slices must be of the form {[start[, stop[, step]]]}"
                );
            }
            size_t i = 0;
            std::array<Object, 3> params {None, None, None};
            for (auto&& item : slice) {
                params[i++] = std::visit(
                    [](auto&& arg) -> Object {
                        return detail::object_or_cast(arg);
                    },
                    item
                );
            }
            return static_cast<const Derived*>(this)->operator[](
                pybind11::slice(params[0], params[1], params[2])
            );
        }

    };

    #define COMPARISON_OPERATOR(op, endpoint)                                           \
        template <typename T>                                                           \
        inline bool op(T&& other) const {                                               \
            int result = PyObject_RichCompareBool(                                      \
                static_cast<const Derived*>(this)->ptr(),                               \
                detail::object_or_cast(std::forward<T>(other)).ptr(),                   \
                endpoint                                                                \
            );                                                                          \
            if (result == -1) {                                                         \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \

    #define REVERSE_COMPARISON(op, endpoint)                                            \
        template <                                                                      \
            typename T,                                                                 \
            std::enable_if_t<                                                           \
                !std::is_base_of_v<Derived, std::decay_t<T>>,                           \
                int                                                                     \
            > = 0                                                                       \
        >                                                                               \
        inline friend bool op(T&& other, const Derived& self) {                         \
            int result = PyObject_RichCompareBool(                                      \
                detail::object_or_cast(std::forward<T>(other)).ptr(),                   \
                self.ptr(),                                                             \
                endpoint                                                                \
            );                                                                          \
            if (result == -1) {                                                         \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        }                                                                               \

    /* Mixin holding equality comparisons that always fall back to PyObject_RichCompare.
    This resolves the asymmetry between pybind11's `==`/`!=` operators and the Python
    equivalents. */
    template <typename Derived>
    struct EqualCompare {
        COMPARISON_OPERATOR(operator==, Py_EQ);
        COMPARISON_OPERATOR(operator!=, Py_NE);
        REVERSE_COMPARISON(operator==, Py_EQ);
        REVERSE_COMPARISON(operator!=, Py_NE);
    };

    /* Mixin holding a full suite of comparison operators that always fall back to
    PyObject_RichCompare. */
    template <typename Derived>
    struct FullCompare {
        COMPARISON_OPERATOR(operator<, Py_LT);
        COMPARISON_OPERATOR(operator<=, Py_LE);
        COMPARISON_OPERATOR(operator==, Py_EQ);
        COMPARISON_OPERATOR(operator!=, Py_NE);
        COMPARISON_OPERATOR(operator>, Py_GT);
        COMPARISON_OPERATOR(operator>=, Py_GE);
        REVERSE_COMPARISON(operator<, Py_GT);
        REVERSE_COMPARISON(operator<=, Py_GE);
        REVERSE_COMPARISON(operator==, Py_EQ);
        REVERSE_COMPARISON(operator!=, Py_NE);
        REVERSE_COMPARISON(operator>, Py_LT);
        REVERSE_COMPARISON(operator>=, Py_LE);
    };

    #undef COMPARISON_OPERATOR
    #undef REVERSE_COMPARISON

    /* Mixin holding an optimized reverse iterator for data structures that allow
    direct access to the underlying array.  This avoids the overhead of going through
    the Python interpreter to obtain a reverse iterator, and brings it up to parity
    with forward iteration. */
    template <typename Derived>
    struct ReverseIterable {
        /* An optimized reverse iterator that bypasses the python interpreter. */
        struct ReverseIterator {
            using iterator_category     = std::forward_iterator_tag;
            using difference_type       = std::ptrdiff_t;
            using value_type            = Object;
            using pointer               = value_type*;
            using reference             = value_type&;

            PyObject** array;
            Py_ssize_t index;

            inline ReverseIterator(PyObject** array, Py_ssize_t index) :
                array(array), index(index)
            {}
            inline ReverseIterator(Py_ssize_t index) : array(nullptr), index(index) {}
            inline ReverseIterator(const ReverseIterator&) = default;
            inline ReverseIterator(ReverseIterator&&) = default;
            inline ReverseIterator& operator=(const ReverseIterator&) = default;
            inline ReverseIterator& operator=(ReverseIterator&&) = default;

            inline Handle operator*() const {
                return array[index];
            }

            inline ReverseIterator& operator++() {
                --index;
                return *this;
            }

            inline bool operator==(const ReverseIterator& other) const {
                return index == other.index;
            }

            inline bool operator!=(const ReverseIterator& other) const {
                return index != other.index;
            }

        };

    };

    template <typename Base, typename T>
    using enable_for = std::enable_if_t<std::is_base_of_v<Base, T>>;

    /* Types for which is_reverse_iterable is true will use custom reverse iterators
    when py::reversed() is called on them.  These types must expose a ReverseIterator
    class that implements the reverse iteration. */
    template <typename T, typename = void>
    constexpr bool is_reverse_iterable = false;
    template <typename T>
    constexpr bool is_reverse_iterable<T, enable_for<Tuple, T>> = true;
    template <typename T>
    constexpr bool is_reverse_iterable<T, enable_for<List, T>> = true;

    /* All new subclasses of pybind11::object must define these constructors, which are
    taken directly from PYBIND11_OBJECT_COMMON.  The check() function will be called
    if the constructor is invoked with a generic object as input, and the convert()
    function will be called if it fails that check.  Both should operate on raw
    PyObject* pointers, and convert() should always return a new reference.  They will
    never be passed null objects. */
    #define CONSTRUCTORS(cls, check, convert)                                           \
        using Base::operator=;                                                          \
        cls(handle h, borrowed_t ref) : Base(h, ref) {}                                 \
        cls(handle h, stolen_t ref) : Base(h, ref) {}                                   \
        cls(const pybind11::object& obj) : Base([&obj] {                                \
            if (obj.ptr() == nullptr) {                                                 \
                throw TypeError("cannot convert null object to " #cls);                 \
            } else if (check(obj.ptr())) {                                              \
                return obj.inc_ref().ptr();                                             \
            } else {                                                                    \
                return convert(obj.ptr());                                              \
            }                                                                           \
        }(), stolen_t{}) {}                                                             \
        cls(pybind11::object&& obj) : Base([&obj] {                                     \
            if (obj.ptr() == nullptr) {                                                 \
                throw TypeError("cannot convert null object to " #cls);                 \
            } else if (check(obj.ptr())) {                                              \
                return obj.release().ptr();                                             \
            } else {                                                                    \
                return convert(obj.ptr());                                              \
            }                                                                           \
        }(), stolen_t{}) {}                                                             \
        template <typename Policy>                                                      \
        cls(const detail::accessor<Policy> &proxy) : cls(Object(proxy)) {}              \
        template <typename T>                                                           \
        cls& operator=(std::initializer_list<T> init) {                                 \
            Base::operator=(cls(init));                                                 \
            return *this;                                                               \
        }                                                                               \

}  // namespace impl


class NotImplementedType :
    public pybind11::object,
    public impl::EqualCompare<NotImplementedType>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<NotImplementedType>;

    inline static int check_not_implemented(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) Py_TYPE(Py_NotImplemented));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_not_implemented(PyObject* obj) {
        throw TypeError("cannot convert object to NotImplemented");
    }

public:
    CONSTRUCTORS(NotImplementedType, check_not_implemented, convert_not_implemented);
    NotImplementedType() : Base(Py_NotImplemented, borrowed_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
};


const static NotImplementedType NotImplemented;


/* Wrapper around pybind11::bool_ that enables math operations with C++ inputs. */
class Bool :
    public pybind11::bool_,
    public impl::NumericOps<Bool>,
    public impl::FullCompare<Bool>
{
    using Base = pybind11::bool_;
    using Ops = impl::NumericOps<Bool>;
    using Compare = impl::FullCompare<Bool>;

public:
    using Base::Base;
    using Base::operator=;

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor, as well as converting math operators
that account for C++ inputs. */
class Int :
    public pybind11::int_,
    public impl::NumericOps<Int>,
    public impl::FullCompare<Int>
{
    using Base = pybind11::int_;
    using Ops = impl::NumericOps<Int>;
    using Compare = impl::FullCompare<Int>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct an Int from a string with an optional base. */
    explicit Int(const pybind11::str& str, int base = 0) : Base([&str, &base] {
        PyObject* result = PyLong_FromUnicodeObject(str.ptr(), base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const char* str, int base = 0) : Base([&str, &base] {
        PyObject* result = PyLong_FromString(str, nullptr, base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string& str, int base = 0) :
        Int(str.c_str(), base)
    {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string_view& str, int base = 0) :
        Int(str.data(), base)
    {}

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};


/* Wrapper around pybind11::float_ that enables conversions from strings, similar to
Python's `float()` constructor, as well as converting math operators that account for
C++ inputs. */
class Float :
    public pybind11::float_,
    public impl::NumericOps<Float>,
    public impl::FullCompare<Float>
{
    using Base = pybind11::float_;
    using Ops = impl::NumericOps<Float>;
    using Compare = impl::FullCompare<Float>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Float from a string. */
    explicit Float(const pybind11::str& str) : Base([&str] {
        PyObject* result = PyFloat_FromString(str.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const char* str) : Base([&str] {
        PyObject* string = PyUnicode_FromString(str);
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string& str) : Base([&str] {
        PyObject* string = PyUnicode_FromStringAndSize(str.c_str(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Float from a string. */
    explicit Float(const std::string_view& str) : Base([&str] {
        PyObject* string = PyUnicode_FromStringAndSize(str.data(), str.size());
        if (string == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};


/* New subclass of pybind11::object that represents a complex number at the Python
level. */
class Complex :
    public pybind11::object,
    public impl::NumericOps<Complex>,
    public impl::FullCompare<Complex>
{
    using Base = pybind11::object;
    using Ops = impl::NumericOps<Complex>;
    using Compare = impl::FullCompare<Complex>;

    template <typename T>
    static constexpr bool is_numeric = (
        std::is_integral_v<T> || std::is_floating_point_v<T>
    );

    static PyObject* convert_to_complex(PyObject* obj) {
        Py_complex complex_struct = PyComplex_AsCComplex(obj);
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        PyObject* result = PyComplex_FromDoubles(
            complex_struct.real, complex_struct.imag
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Complex, PyComplex_Check, convert_to_complex);

    /* Default constructor.  Initializes to 0+0j. */
    inline Complex() : Base([] {
        PyObject* result = PyComplex_FromDoubles(0.0, 0.0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from a C++ std::complex. */
    template <typename T>
    inline Complex(const std::complex<T>& value) : Base([&value] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(value.real()),
            static_cast<double>(value.imag())
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from a real component as a C++ integer or float. */
    template <typename Real, std::enable_if_t<is_numeric<Real>, int> = 0>
    inline Complex(Real real) : Base([&real] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            0.0
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Complex number from separate real and imaginary components as C++
    integers or floats. */
    template <
        typename Real,
        typename Imag,
        std::enable_if_t<is_numeric<Real> && is_numeric<Imag>, int> = 0
    >
    inline Complex(Real real, Imag imag) : Base([&real, &imag] {
        PyObject* result = PyComplex_FromDoubles(
            static_cast<double>(real),
            static_cast<double>(imag)
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Implicitly convert a Complex number into a C++ std::complex. */
    template <typename T>
    operator std::complex<T>() const {
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return std::complex<T>(complex_struct.real, complex_struct.imag);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the real part of the Complex number. */
    inline double real() const noexcept {
        return PyComplex_RealAsDouble(this->ptr());
    }

    /* Get the imaginary part of the Complex number. */
    inline double imag() const noexcept {
        return PyComplex_ImagAsDouble(this->ptr());
    }

    /* Get the magnitude of the Complex number. */
    inline Complex conjugate() const {
        Py_complex complex_struct = PyComplex_AsCComplex(this->ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            throw error_already_set();
        }
        return {complex_struct.real, -complex_struct.imag};
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};



// TODO: Datetime/Timedelta/Timezone classes



/* Wrapper around pybind11::slice that allows it to be instantiated with non-integer
inputs in order to represent denormalized slices at the Python level, and provides more
pythonic access to its members. */
class Slice :
    public pybind11::slice,
    public impl::FullCompare<Slice>
{
    using Base = pybind11::slice;
    using Compare = impl::FullCompare<Slice>;

    static PyObject* convert_to_slice(PyObject* obj) {
        PyObject* result = PySlice_New(nullptr, obj, nullptr);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Slice, PySlice_Check, convert_to_slice);

    /* Construct a slice from a (possibly denormalized) stop object. */
    template <typename Stop>
    Slice(Stop&& stop) : Base([&stop] {
        PyObject* result = PySlice_New(
            nullptr,
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            nullptr
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a slice from (possibly denormalized) start and stop objects. */
    template <typename Start, typename Stop>
    Slice(Start&& start, Stop&& stop) : Base([&start, &stop] {
        PyObject* result = PySlice_New(
            detail::object_or_cast(std::forward<Start>(start)).ptr(),
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            nullptr
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a slice from (possibly denormalized) start, stop, and step objects. */
    template <typename Start, typename Stop, typename Step>
    Slice(Start&& start, Stop&& stop, Step&& step) : Base([&start, &stop, &step] {
        PyObject* result = PySlice_New(
            detail::object_or_cast(std::forward<Start>(start)).ptr(),
            detail::object_or_cast(std::forward<Stop>(stop)).ptr(),
            detail::object_or_cast(std::forward<Step>(step)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start object of the slice.  Note that this might not be an integer. */
    inline Object start() const {
        return this->attr("start");
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    inline Object stop() const {
        return this->attr("stop");
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    inline Object step() const {
        return this->attr("step");
    }

    /* Data struct containing normalized indices obtained from a py::Slice object. */
    struct Indices {
        Py_ssize_t start;
        Py_ssize_t stop;
        Py_ssize_t step;
        Py_ssize_t length;
    };

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:
        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice
    */
    inline Indices indices(size_t size) const {
        Py_ssize_t start, stop, step, length = 0;
        if (PySlice_GetIndicesEx(
            this->ptr(),
            static_cast<Py_ssize_t>(size),
            &start,
            &stop,
            &step,
            &length
        )) {
            throw error_already_set();
        }
        return {start, stop, step, length};
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;
};


/* New subclass of pybind11::object that represents a range object at the Python
level. */
class Range :
    public pybind11::object,
    public impl::EqualCompare<Range>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Range>;

    inline static bool range_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyRange_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_range(PyObject* obj) {
        PyObject* result = PyObject_CallOneArg((PyObject*) &PyRange_Type, obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Range, range_check, convert_to_range);

    /* Construct a range from 0 to the given stop index (exclusive). */
    explicit Range(Py_ssize_t stop) : Base([&stop] {
        PyObject* range = PyDict_GetItemString(PyEval_GetBuiltins(), "range");
        PyObject* result = PyObject_CallFunction(range, "n", stop);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a range from the given start and stop indices (exclusive). */
    explicit Range(
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step = 1
    ) : Base([&start, &stop, &step] {
        PyObject* range = PyDict_GetItemString(PyEval_GetBuiltins(), "range");
        PyObject* result = PyObject_CallFunction(range, "nnn", start, stop, step);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    // TODO: conversion from/to std::ranges::range (C++20)

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the start index of the Range sequence. */
    inline Py_ssize_t start() const {
        return PyLong_AsSsize_t(this->attr("start").ptr());
    }

    /* Get the stop index of the Range sequence. */
    inline Py_ssize_t stop() const {
        return PyLong_AsSsize_t(this->attr("stop").ptr());
    }

    /* Get the step size of the Range sequence. */
    inline Py_ssize_t step() const {
        return PyLong_AsSsize_t(this->attr("step").ptr());
    }    

};


/* Wrapper around pybind11::list that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class List :
    public pybind11::list,
    public impl::SequenceOps<List>,
    public impl::FullCompare<List>,
    public impl::ReverseIterable<List>
{
    using Base = pybind11::list;
    using Ops = impl::SequenceOps<List>;
    using Compare = impl::FullCompare<List>;

    static PyObject* convert_to_list(PyObject* obj) {
        PyObject* result = PySequence_List(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(List, PyList_Check, convert_to_list);

    /* Default constructor.  Initializes to an empty list. */
    List() : Base([] {
        PyObject* result = PyList_New(0);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Pack the contents of a braced initializer into a new Python list. */
    List(const std::initializer_list<Initializer>& contents) : Base([&contents] {
        PyObject* result = PyList_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const Initializer& element : contents) {
                PyList_SET_ITEM(
                    result,
                    i++,
                    const_cast<Initializer&>(element).value.release().ptr()
                );
            }
            return result;
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }(), stolen_t{}) {}

    /* Pack the contents of a braced initializer into a new Python list. */
    template <typename T, std::enable_if_t<!std::is_same_v<Initializer, T>, int> = 0>
    List(const std::initializer_list<T>& contents) : Base([&contents] {
        PyObject* result = PyList_New(contents.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            for (const T& element : contents) {
                PyList_SET_ITEM(result, i++, impl::convert_newref(element));
            }
            return result;
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }(), stolen_t{}) {}

    /* Unpack a generic container into a new List.  Equivalent to Python
    `list(container)`, except that it also works on C++ containers. */
    template <typename T>
    explicit List(T&& container) : Base([&container]{
        if constexpr (detail::is_pyobject<T>::value) {
            PyObject* result = PySequence_List(container.ptr());
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            size_t size = 0;
            if constexpr (impl::has_size<T>) {
                size = container.size();
            }
            PyObject* result = PyList_New(size);
            if (result == nullptr) {
                throw error_already_set();
            }
            try {
                size_t i = 0;
                for (auto&& item : container) {
                    PyList_SET_ITEM(
                        result,
                        i++,
                        impl::convert_newref(std::forward<decltype(item)>(item))
                    );
                }
                return result;
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        }
    }(), stolen_t{}) {}

    ///////////////////////////////////
    ////    PyList* API METHODS    ////
    ///////////////////////////////////

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->ptr()));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->ptr());
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `list.append(value)`. */
    template <typename T>
    inline void append(T&& value) {
        if (PyList_Append(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(value)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.extend(items)`. */
    template <typename T>
    inline void extend(T&& items) {
        if constexpr (detail::is_pyobject<T>::value) {
            this->attr("extend")(std::forward<T>(items));
        } else {
            for (auto&& item : items) {
                append(std::forward<decltype(item)>(item));
            }
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    template <typename T>
    inline void extend(const std::initializer_list<T>& items) {
        for (const T& item : items) {
            append(item);
        }
    }

    /* Equivalent to Python `list.extend(items)`, where items are given as a braced
    initializer list. */
    inline void extend(const std::initializer_list<Initializer>& items) {
        for (const Initializer& item : items) {
            append(item.value);
        }
    }

    /* Equivalent to Python `list.insert(index, value)`. */
    template <typename T>
    inline void insert(Py_ssize_t index, T&& value) {
        if (PyList_Insert(
            this->ptr(),
            index,
            detail::object_or_cast(std::forward<T>(value)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.copy()`. */
    inline List copy() const {
        PyObject* result = PyList_GetSlice(this->ptr(), 0, size());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `list.clear()`. */
    inline void clear() {
        if (PyList_SetSlice(this->ptr(), 0, size(), nullptr)) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.remove(value)`. */
    template <typename T>
    inline void remove(T&& value) {
        this->attr("remove")(std::forward<T>(value));
    }

    /* Equivalent to Python `list.pop([index])`. */
    inline Object pop(Py_ssize_t index = -1) {
        return this->attr("pop")(index);
    }

    /* Equivalent to Python `list.reverse()`. */
    inline void reverse() {
        if (PyList_Reverse(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.sort()`. */
    inline void sort() {
        if (PyList_Sort(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `list.sort(reverse=reverse)`. */
    inline void sort(bool reverse) {
        this->attr("sort")(py::arg("reverse") = py::Bool(reverse));
    }

    /* Equivalent to Python `list.sort(key=key[, reverse=reverse])`.  The key function
    can be given as any C++ function-like object, but users should note that pybind11
    has a hard time parsing generic arguments, so templates and the `auto` keyword
    should be avoided. */
    template <typename Func>
    inline void sort(Func&& key, bool reverse = false) {
        py::cpp_function func(std::forward<Func>(key));
        py::Bool flag(reverse);
        this->attr("sort")(py::arg("key") = func, py::arg("reverse") = flag);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Base::operator[];
    using Ops::operator[];

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::concat;
    using Ops::operator+;
    using Ops::operator*;
    using Ops::operator*=;

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    template <typename T>
    inline List concat(const std::initializer_list<T>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (auto&& item : items) {
                PyList_SET_ITEM(
                    result,
                    i++,
                    impl::convert_newref(std::forward<decltype(item)>(item))
                );
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Overload of concat() that allows the operand to be a braced initializer list. */
    inline List concat(const std::initializer_list<Initializer>& items) const {
        PyObject* result = PyList_New(size() + items.size());
        if (result == nullptr) {
            throw error_already_set();
        }
        try {
            size_t i = 0;
            size_t length = size();
            PyObject** array = data();
            while (i < length) {
                PyList_SET_ITEM(result, i, Py_NewRef(array[i]));
                ++i;
            }
            for (const Initializer& item : items) {
                PyList_SET_ITEM(
                    result,
                    i++,
                    const_cast<Initializer&>(item).value.release().ptr()
                );
            }
            return reinterpret_steal<List>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    template <typename T>
    inline List operator+(const std::initializer_list<T>& items) const {
        return concat(items);
    }

    inline List operator+(const std::initializer_list<Initializer>& items) const {
        return concat(items);
    }

    template <typename T>
    inline List& operator+=(T&& items) {
        extend(std::forward<T>(items));
        return *this;
    }

    template <typename T>
    inline List& operator+=(const std::initializer_list<T>& items) {
        extend(items);
        return *this;
    }

    inline List& operator+=(const std::initializer_list<Initializer>& items) {
        extend(items);
        return *this;
    }

};


namespace impl {

#define TUPLE_INTERFACE(cls)                                                            \
    static PyObject* convert_to_tuple(PyObject* obj) {                                  \
        PyObject* result = PySequence_Tuple(obj);                                       \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
public:                                                                                 \
    CONSTRUCTORS(cls, PyTuple_Check, convert_to_tuple);                                 \
                                                                                        \
    /* Default constructor.  Initializes to empty tuple. */                             \
    inline cls() : Base([] {                                                            \
        PyObject* result = PyTuple_New(0);                                              \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python tuple. */            \
    template <typename T>                                                               \
    cls(const std::initializer_list<T>& contents) : Base([&contents] {                  \
        PyObject* result = PyTuple_New(contents.size());                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            size_t i = 0;                                                               \
            for (auto&& element : contents) {                                           \
                PyTuple_SET_ITEM(                                                       \
                    result,                                                             \
                    i++,                                                                \
                    impl::convert_newref(std::forward<decltype(element)>(element))      \
                );                                                                      \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python tuple. */            \
    cls(const std::initializer_list<Initializer>& contents) : Base([&contents] {        \
        PyObject* result = PyTuple_New(contents.size());                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            size_t i = 0;                                                               \
            for (const Initializer& element : contents) {                               \
                PyTuple_SET_ITEM(                                                       \
                    result,                                                             \
                    i++,                                                                \
                    const_cast<Initializer&>(element).value.release().ptr()             \
                );                                                                      \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Unpack a pybind11::list into a new tuple directly using the C API. */            \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<std::is_base_of_v<pybind11::list, T>, int> = 0                 \
    >                                                                                   \
    explicit cls(T&& list) : Base([&list] {                                             \
        PyObject* result = PyList_AsTuple(list.ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Unpack a generic container into a new Python tuple. */                           \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<!std::is_base_of_v<pybind11::list, T>, int> = 0                \
    >                                                                                   \
    explicit cls(T&& container) : Base([&container] {                                   \
        if constexpr (detail::is_pyobject<T>::value) {                                  \
            PyObject* result = PySequence_Tuple(container.ptr());                       \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        } else {                                                                        \
            size_t size = 0;                                                            \
            if constexpr (impl::has_size<T>) {                                          \
                size = container.size();                                                \
            }                                                                           \
            PyObject* result = PyTuple_New(size);                                       \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            try {                                                                       \
                size_t i = 0;                                                           \
                for (auto&& item : container) {                                         \
                    PyTuple_SET_ITEM(                                                   \
                        result,                                                         \
                        i++,                                                            \
                        impl::convert_newref(std::forward<decltype(item)>(item))        \
                    );                                                                  \
                }                                                                       \
                return result;                                                          \
            } catch (...) {                                                             \
                Py_DECREF(result);                                                      \
                throw;                                                                  \
            }                                                                           \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
        /*     PyTuple_ API     */                                                      \
                                                                                        \
    /* Get the size of the tuple. */                                                    \
    inline size_t size() const noexcept {                                               \
        return PyTuple_GET_SIZE(this->ptr());                                           \
    }                                                                                   \
                                                                                        \
    /* Get the underlying PyObject* array. */                                           \
    inline PyObject** data() const noexcept {                                           \
        return PySequence_Fast_ITEMS(this->ptr());                                      \
    }                                                                                   \
                                                                                        \
    /* Directly access an item without bounds checking or constructing a proxy. */      \
    inline Object GET_ITEM(Py_ssize_t index) const {                                    \
        return reinterpret_borrow<Object>(PyTuple_GET_ITEM(this->ptr(), index));        \
    }                                                                                   \
                                                                                        \
    /* Directly set an item without bounds checking or constructing a proxy. */         \
    template <typename T>                                                               \
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {                           \
        /* NOTE: This steals a reference to `value` and does not clear the previous */  \
        /* item if one is present.  This is dangerous, and should only be used to */    \
        /* fill in a newly-allocated (empty) tuple. */                                  \
        PyTuple_SET_ITEM(this->ptr(), index, value);                                    \
    }                                                                                   \
                                                                                        \
        /*    OPERATORS    */                                                           \
                                                                                        \
    using Base::operator[];                                                             \
    using Ops::operator[];                                                              \
                                                                                        \
    using Compare::operator<;                                                           \
    using Compare::operator<=;                                                          \
    using Compare::operator==;                                                          \
    using Compare::operator!=;                                                          \
    using Compare::operator>;                                                           \
    using Compare::operator>=;                                                          \
                                                                                        \
    using Ops::concat;                                                                  \
    using Ops::operator+;                                                               \
    using Ops::operator+=;                                                              \
    using Ops::operator*;                                                               \
    using Ops::operator*=;                                                              \
                                                                                        \
    /* Overload of concat() that allows the operand to be a braced initializer list. */ \
    template <typename T>                                                               \
    inline cls concat(const std::initializer_list<T>& items) const {                    \
        PyObject* result = PyTuple_New(size() + items.size());                          \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            size_t i = 0;                                                               \
            size_t length = size();                                                     \
            PyObject** array = data();                                                  \
            while (i < length) {                                                        \
                PyTuple_SET_ITEM(result, i, Py_NewRef(array[i]));                       \
                ++i;                                                                    \
            }                                                                           \
            for (auto&& item : items) {                                                 \
                PyTuple_SET_ITEM(                                                       \
                    result,                                                             \
                    i++,                                                                \
                    impl::convert_newref(std::forward<decltype(item)>(item))            \
                );                                                                      \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Overload of concat() that allows the operand to be a braced initializer list. */ \
    inline cls concat(const std::initializer_list<Initializer>& items) const {          \
        PyObject* result = PyTuple_New(size() + items.size());                          \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            size_t i = 0;                                                               \
            size_t length = size();                                                     \
            PyObject** array = data();                                                  \
            while (i < length) {                                                        \
                PyTuple_SET_ITEM(result, i, Py_NewRef(array[i]));                       \
                ++i;                                                                    \
            }                                                                           \
            for (const Initializer& item : items) {                                     \
                PyTuple_SET_ITEM(                                                       \
                    result,                                                             \
                    i++,                                                                \
                    const_cast<Initializer&>(item).value.release().ptr()                \
                );                                                                      \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator+(const std::initializer_list<T>& items) const {                 \
        return concat(items);                                                           \
    }                                                                                   \
                                                                                        \
    inline cls operator+(const std::initializer_list<Initializer>& items) const {       \
        return concat(items);                                                           \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls& operator+=(const std::initializer_list<T>& items) {                     \
        *this = concat(items);                                                          \
        return *this;                                                                   \
    }                                                                                   \
                                                                                        \
    inline cls& operator+=(const std::initializer_list<Initializer>& items) {           \
        *this = concat(items);                                                          \
        return *this;                                                                   \
    }                                                                                   \

}  // namespace impl


/* Wrapper around pybind11::tuple that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Tuple :
    public pybind11::tuple,
    public impl::SequenceOps<Tuple>,
    public impl::FullCompare<Tuple>,
    public impl::ReverseIterable<Tuple>
{
    using Base = pybind11::tuple;
    using Ops = impl::SequenceOps<Tuple>;
    using Compare = impl::FullCompare<Tuple>;
    TUPLE_INTERFACE(Tuple);
};


/* Wrapper around pybind11::args that implements the same interface as py::Tuple.  Note
that py::Args is not a subclass of py::Tuple, so the pybind11's inheritance
relationship is not preserved. */
class Args :
    public pybind11::args,
    public impl::SequenceOps<Args>,
    public impl::FullCompare<Args>,
    public impl::ReverseIterable<Args>
{
    using Base = pybind11::args;
    using Ops = impl::SequenceOps<Args>;
    using Compare = impl::FullCompare<Args>;
    TUPLE_INTERFACE(Args);
};


#undef TUPLE_INTERFACE


namespace impl {

#define ANYSET_INTERFACE(cls)                                                           \
                                                                                        \
    static PyObject* convert_to_set(PyObject* obj) {                                    \
        PyObject* result = Py##cls##_New(obj);                                          \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
public:                                                                                 \
    CONSTRUCTORS(cls, Py##cls##_Check, convert_to_set);                                 \
                                                                                        \
    /* Default constructor.  Initializes to empty set. */                               \
    inline cls() : Base([] {                                                            \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python set. */              \
    cls(const std::initializer_list<Initializer>& contents) : Base([&contents] {        \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const Initializer& element : contents) {                               \
                if (PySet_Add(result, element.value.ptr())) {                           \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Pack the contents of a braced initializer into a new Python set. */              \
    template <typename T>                                                               \
    cls(const std::initializer_list<T>& contents) : Base([&contents] {                  \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const T& element : contents) {                                         \
                if (PySet_Add(result, detail::object_or_cast(element).ptr())) {         \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Unpack a generic container into a new Python set. */                             \
    template <typename T>                                                               \
    explicit cls(T&& container) : Base([&container] {                                   \
        if constexpr (detail::is_pyobject<T>::value) {                                  \
            PyObject* result = Py##cls##_New(container.ptr());                          \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            return result;                                                              \
        } else {                                                                        \
            PyObject* result = Py##cls##_New(nullptr);                                  \
            if (result == nullptr) {                                                    \
                throw error_already_set();                                              \
            }                                                                           \
            try {                                                                       \
                for (auto&& item : container) {                                         \
                    if (PySet_Add(                                                      \
                        result,                                                         \
                        detail::object_or_cast(std::forward<decltype(item)>(item)).ptr()\
                    )) {                                                                \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
                return result;                                                          \
            } catch (...) {                                                             \
                Py_DECREF(result);                                                      \
                throw;                                                                  \
            }                                                                           \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
        /*    PySet_ API    */                                                          \
                                                                                        \
    /* Get the size of the set. */                                                      \
    inline size_t size() const noexcept {                                               \
        return static_cast<size_t>(PySet_GET_SIZE(this->ptr()));                        \
    }                                                                                   \
                                                                                        \
        /*    PYTHON INTERFACE    */                                                    \
                                                                                        \
    /* Equivalent to Python `set.copy()`. */                                            \
    inline cls copy() const {                                                           \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<cls>(result);                                          \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.isdisjoint(other)`. */                                 \
    template <typename T>                                                               \
    inline bool isdisjoint(T&& other) const {                                           \
        if constexpr (detail::is_pyobject<std::decay_t<T>>::value) {                    \
            return this->attr("isdisjoint")(other).template cast<bool>();               \
        } else {                                                                        \
            for (auto&& item : other) {                                                 \
                if (contains(std::forward<decltype(item)>(item))) {                     \
                    return false;                                                       \
                }                                                                       \
            }                                                                           \
            return true;                                                                \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.isdisjoint(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline bool isdisjoint(std::initializer_list<T> other) const {                      \
        for (auto&& item : other) {                                                     \
            if (contains(std::forward<decltype(item)>(item))) {                         \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issubset(other)`. */                                   \
    template <typename T>                                                               \
    inline bool issubset(T&& other) const {                                             \
        return this->attr("issubset")(                                                  \
            detail::object_or_cast(std::forward<T>(other))                              \
        ).template cast<bool>();                                                        \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issubset(<braced initializer list>)`. */               \
    template <typename T>                                                               \
    inline bool issubset(std::initializer_list<T> other) const {                        \
        return this->attr("issubset")(cls(other)).template cast<bool>();                \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issuperset(other)`. */                                 \
    template <typename T>                                                               \
    inline bool issuperset(T&& other) const {                                           \
        if constexpr (detail::is_pyobject<std::decay_t<T>>::value) {                    \
            return this->attr("issuperset")(other).template cast<bool>();               \
        } else {                                                                        \
            for (auto&& item : other) {                                                 \
                if (!contains(std::forward<decltype(item)>(item))) {                    \
                    return false;                                                       \
                }                                                                       \
            }                                                                           \
            return true;                                                                \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.issuperset(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline bool issuperset(std::initializer_list<T> other) const {                      \
        for (auto&& item : other) {                                                     \
            if (!contains(std::forward<decltype(item)>(item))) {                        \
                return false;                                                           \
            }                                                                           \
        }                                                                               \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.union(*others)`. */                                    \
    template <typename... Args>                                                         \
    inline cls union_(Args&&... others) const {                                         \
        return this->attr("union")(                                                     \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.union(<braced initializer list>)`. */                  \
    template <typename T>                                                               \
    inline cls union_(std::initializer_list<T> other) const {                           \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& item : other) {                                                 \
                if (PySet_Add(                                                          \
                    result,                                                             \
                    detail::object_or_cast(std::forward<decltype(item)>(item)).ptr()    \
                )) {                                                                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.intersection(other)`. */                               \
    template <typename... Args>                                                         \
    inline cls intersection(Args&&... others) const {                                   \
        return this->attr("intersection")(                                              \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.intersection(<braced initializer list>)`. */           \
    template <typename T>                                                               \
    inline cls intersection(std::initializer_list<T> other) const {                     \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& item : other) {                                                 \
                Object obj = detail::object_or_cast(                                    \
                    std::forward<decltype(item)>(item)                                  \
                );                                                                      \
                if (contains(obj)) {                                                    \
                    if (PySet_Add(result, obj.ptr())) {                                 \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.difference(other)`. */                                 \
    template <typename... Args>                                                         \
    inline cls difference(Args&&... others) const {                                     \
        return this->attr("difference")(                                                \
            detail::object_or_cast(std::forward<Args>(others))...                       \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.difference(<braced initializer list>)`. */             \
    template <typename T>                                                               \
    inline cls difference(std::initializer_list<T> other) const {                       \
        PyObject* result = Py##cls##_New(this->ptr());                                  \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& item : other) {                                                 \
                Object obj = detail::object_or_cast(                                    \
                    std::forward<decltype(item)>(item)                                  \
                );                                                                      \
                if (PySet_Discard(result, obj.ptr()) == -1) {                           \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.symmetric_difference(other)`. */                       \
    template <typename T>                                                               \
    inline cls symmetric_difference(T&& other) const {                                  \
        return this->attr("symmetric_difference")(                                      \
            detail::object_or_cast(std::forward<T>(other))                              \
        );                                                                              \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `set.symmetric_difference(<braced initializer list)`. */    \
    template <typename T>                                                               \
    inline cls symmetric_difference(std::initializer_list<T> other) const {             \
        PyObject* result = Py##cls##_New(nullptr);                                      \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& item : other) {                                                 \
                Object obj = detail::object_or_cast(                                    \
                    std::forward<decltype(item)>(item)                                  \
                );                                                                      \
                if (contains(obj)) {                                                    \
                    if (PySet_Discard(result, obj.ptr()) == -1) {                       \
                        throw error_already_set();                                      \
                    }                                                                   \
                } else {                                                                \
                    if (PySet_Add(result, obj.ptr())) {                                 \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
        /*    OPERATORS    */                                                           \
                                                                                        \
    /* Equivalent to Python `key in set`. */                                            \
    template <typename T>                                                               \
    inline bool contains(T&& key) const {                                               \
        int result = PySet_Contains(                                                    \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(key)).ptr()                          \
        );                                                                              \
        if (result == -1) {                                                             \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
    using Compare::operator<;                                                           \
    using Compare::operator<=;                                                          \
    using Compare::operator==;                                                          \
    using Compare::operator!=;                                                          \
    using Compare::operator>=;                                                          \
    using Compare::operator>;                                                           \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator|(T&& other) const {                                             \
        return union_(std::forward<T>(other));                                          \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator|(std::initializer_list<T> other) const {                        \
        return union_(other);                                                           \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator&(T&& other) const {                                             \
        return intersection(std::forward<T>(other));                                    \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator&(std::initializer_list<T> other) const {                        \
        return intersection(other);                                                     \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator-(T&& other) const {                                             \
        return difference(std::forward<T>(other));                                      \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator-(std::initializer_list<T> other) const {                        \
        return difference(other);                                                       \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator^(T&& other) const {                                             \
        return symmetric_difference(std::forward<T>(other));                            \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator^(std::initializer_list<T> other) const {                        \
        return symmetric_difference(other);                                             \
    }                                                                                   \

}  // namespace impl


/* Wrapper around pybind11::frozenset that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class FrozenSet :
    public pybind11::frozenset,
    public impl::FullCompare<FrozenSet>
{
    using Base = pybind11::frozenset;
    using Compare = impl::FullCompare<FrozenSet>;

    ANYSET_INTERFACE(FrozenSet);

    template <typename T>
    inline FrozenSet& operator|=(T&& other) {
        *this = union_(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator|=(std::initializer_list<T> other) {
        *this = union_(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator&=(T&& other) {
        *this = intersection(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator&=(std::initializer_list<T> other) {
        *this = intersection(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator-=(T&& other) {
        *this = difference(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator-=(std::initializer_list<T> other) {
        *this = difference(other);
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator^=(T&& other) {
        *this = symmetric_difference(std::forward<T>(other));
        return *this;
    }

    template <typename T>
    inline FrozenSet& operator^=(std::initializer_list<T> other) {
        *this = symmetric_difference(other);
        return *this;
    }

};


/* Wrapper around pybind11::set that allows it to be directly initialized using
std::initializer_list and replicates the Python interface as closely as possible. */
class Set :
    public pybind11::set,
    public impl::FullCompare<Set>
{
    using Base = pybind11::set;
    using Compare = impl::FullCompare<Set>;

    ANYSET_INTERFACE(Set);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `set.add(key)`. */
    template <typename T>
    inline void add(T&& key) {
        if (PySet_Add(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(key)).ptr()
        )) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.remove(key)`. */
    template <typename T>
    inline void remove(T&& key) {
        Object obj = cast<Object>(std::forward<T>(key));
        int result = PySet_Discard(this->ptr(), obj.ptr());
        if (result == -1) {
            throw error_already_set();
        } else if (result == 0) {
            throw KeyError(repr(obj).cast<std::string>());
        }
    }

    /* Equivalent to Python `set.discard(key)`. */
    template <typename T>
    inline void discard(T&& key) {
        if (PySet_Discard(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(key)).ptr()
        ) == -1) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.pop()`. */
    inline Object pop() {
        PyObject* result = PySet_Pop(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    /* Equivalent to Python `set.clear()`. */
    inline void clear() {
        if (PySet_Clear(this->ptr())) {
            throw error_already_set();
        }
    }

    /* Equivalent to Python `set.update(*others)`. */
    template <typename... Args>
    inline void update(Args&&... others) {
        this->attr("update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.update(<braced initializer list>)`. */
    template <typename T>
    inline void update(std::initializer_list<T> other) {
        for (auto&& item : other) {
            add(std::forward<decltype(item)>(item));
        }
    }

    /* Equivalent to Python `set.intersection_update(*others)`. */
    template <typename... Args>
    inline void intersection_update(Args&&... others) {
        this->attr("intersection_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.intersection_update(<braced initializer list>)`. */
    template <typename T>
    inline void intersection_update(std::initializer_list<T> other) {
        this->attr("intersection_update")(cls(other));
    }

    /* Equivalent to Python `set.difference_update(*others)`. */
    template <typename... Args>
    inline void difference_update(Args&&... others) {
        this->attr("difference_update")(
            detail::object_or_cast(std::forward<Args>(others))...
        );
    }

    /* Equivalent to Python `set.difference_update(<braced initializer list>)`. */
    template <typename T>
    inline void difference_update(std::initializer_list<T> other) {
        for (auto&& item : other) {
            discard(std::forward<decltype(item)>(item));
        }
    }

    /* Equivalent to Python `set.symmetric_difference_update(other)`. */
    template <typename T>
    inline void symmetric_difference_update(T&& other) {
        this->attr("symmetric_difference_update")(
            detail::object_or_cast(std::forward<T>(other))
        );
    }

    /* Equivalent to Python `set.symmetric_difference_update(<braced initializer list>)`. */
    template <typename T>
    inline void symmetric_difference_update(std::initializer_list<T> other) {
        for (auto&& item : other) {
            if (contains(std::forward<decltype(item)>(item))) {
                discard(item);
            } else {
                add(item);
            }
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `set |= other`. */
    template <typename T>
    inline Set& operator|=(T&& other) {
        update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set |= <braced initializer list>`. */
    template <typename T>
    inline Set& operator|=(std::initializer_list<T> other) {
        update(other);
        return *this;
    }

    /* Equivalent to Python `set &= other`. */
    template <typename T>
    inline Set& operator&=(T&& other) {
        intersection_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set &= <braced initializer list>`. */
    template <typename T>
    inline Set& operator&=(std::initializer_list<T> other) {
        intersection_update(other);
        return *this;
    }

    /* Equivalent to Python `set -= other`. */
    template <typename T>
    inline Set& operator-=(T&& other) {
        difference_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set -= <braced initializer list>`. */
    template <typename T>
    inline Set& operator-=(std::initializer_list<T> other) {
        difference_update(other);
        return *this;
    }

    /* Equivalent to Python `set ^= other`. */
    template <typename T>
    inline Set& operator^=(T&& other) {
        symmetric_difference_update(std::forward<T>(other));
        return *this;
    }

    /* Equivalent to Python `set ^= <braced initializer list>`. */
    template <typename T>
    inline Set& operator^=(std::initializer_list<T> other) {
        symmetric_difference_update(other);
        return *this;
    }

};


#undef ANYSET_INTERFACE


namespace impl {

#define DICT_INTERFACE(cls)                                                             \
    struct Initializer {                                                                \
        Object key;                                                                     \
        Object value;                                                                   \
                                                                                        \
        template <typename K, typename V>                                               \
        Initializer(K&& key, V&& value) :                                               \
            key(detail::object_or_cast(std::forward<K>(key))),                          \
            value(detail::object_or_cast(std::forward<V>(value)))                       \
        {}                                                                              \
    };                                                                                  \
                                                                                        \
    static PyObject* convert_to_dict(PyObject* obj) {                                   \
        PyObject* result = PyObject_CallOneArg((PyObject*) &PyDict_Type, obj);          \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
public:                                                                                 \
    CONSTRUCTORS(cls, PyDict_Check, convert_to_dict);                                   \
                                                                                        \
    /* Default constructor.  Initializes to empty dict. */                              \
    inline cls() : Base([] {                                                            \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Pack the given arguments into a dictionary using an initializer list. */         \
    cls(const std::initializer_list<Initializer>& contents) : Base([&contents] {        \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& element : contents) {                                           \
                if (PyDict_SetItem(result, element.key.ptr(), element.value.ptr())) {   \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Unpack a generic container into a new dictionary. */                             \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<!std::is_base_of_v<pybind11::dict, T>, int> = 0                \
    >                                                                                   \
    explicit cls(T&& container) : Base([&container] {                                   \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            if constexpr (detail::is_pyobject<T>::value) {                              \
                for (const Handle& item : container) {                                  \
                    if (py::len(item) != 2) {                                           \
                        std::ostringstream msg;                                         \
                        msg << "expected sequence of length 2 (key, value), not: ";     \
                        msg << py::repr(item);                                          \
                        throw ValueError(msg.str());                                    \
                    }                                                                   \
                    auto it = py::iter(item);                                           \
                    Object key = *it;                                                   \
                    Object value = *(++it);                                             \
                    if (PyDict_SetItem(result, key.ptr(), value.ptr())) {               \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            } else {                                                                    \
                for (auto&& [k, v] : container) {                                       \
                    if (PyDict_SetItem(                                                 \
                        result,                                                         \
                        detail::object_or_cast(std::forward<decltype(k)>(k)).ptr(),     \
                        detail::object_or_cast(std::forward<decltype(v)>(v)).ptr()      \
                    )) {                                                                \
                        throw error_already_set();                                      \
                    }                                                                   \
                }                                                                       \
            }                                                                           \
            return result;                                                              \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
    /* Unpack a pybind11::dict into a new dictionary directly using the C API. */       \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<std::is_base_of_v<pybind11::dict, T>, int> = 0                 \
    >                                                                                   \
    explicit cls(T&& dict) : Base([&dict] {                                             \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        if (PyDict_Merge(result, dict.ptr(), 1)) {                                      \
            Py_DECREF(result);                                                          \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }(), stolen_t{}) {}                                                                 \
                                                                                        \
        /*    PyDict_ API    */                                                         \
                                                                                        \
    /* Get the size of the dict. */                                                     \
    inline size_t size() const noexcept {                                               \
        return static_cast<size_t>(PyDict_Size(this->ptr()));                           \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */       \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<std::is_base_of_v<pybind11::dict, T>, int> = 0                 \
    >                                                                                   \
    inline void merge(T&& items) {                                                      \
        if (PyDict_Merge(                                                               \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(items)).ptr(),                       \
            0                                                                           \
        )) {                                                                            \
            throw error_already_set();                                                  \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.update(items)`, but does not overwrite keys. */       \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<!std::is_base_of_v<pybind11::dict, T>, int> = 0                \
    >                                                                                   \
    inline void merge(T&& items) {                                                      \
        if (PyDict_MergeFromSeq2(                                                       \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(items)).ptr(),                       \
            0                                                                           \
        )) {                                                                            \
            throw error_already_set();                                                  \
        }                                                                               \
    }                                                                                   \
                                                                                        \
        /*    PYTHON INTERFACE    */                                                    \
                                                                                        \
    /* Equivalent to Python `dict.clear()`. */                                          \
    inline void clear() {                                                               \
        PyDict_Clear(this->ptr());                                                      \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.copy()`. */                                           \
    inline cls copy() const {                                                           \
        PyObject* result = PyDict_Copy(this->ptr());                                    \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<cls>(result);                                          \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.fromkeys(keys)`.  Values default to None. */          \
    template <typename K>                                                               \
    static inline cls fromkeys(K&& keys) {                                              \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& key : keys) {                                                   \
                if (PyDict_SetItem(                                                     \
                    result,                                                             \
                    detail::object_or_cast(std::forward<decltype(key)>(key)).ptr(),     \
                    Py_None                                                             \
                )) {                                                                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.fromkeys(<braced initializer list>)`. */              \
    template <typename K>                                                               \
    static inline cls fromkeys(std::initializer_list<K> keys) {                         \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const K& key : keys) {                                                 \
                if (PyDict_SetItem(                                                     \
                    result,                                                             \
                    detail::object_or_cast(key).ptr(),                                  \
                    Py_None                                                             \
                )) {                                                                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.fromkeys(keys, value)`. */                            \
    template <typename K, typename V>                                                   \
    static inline cls fromkeys(K&& keys, V&& value) {                                   \
        Object converted = detail::object_or_cast(std::forward<V>(value));              \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (auto&& key : keys) {                                                   \
                if (PyDict_SetItem(                                                     \
                    result,                                                             \
                    detail::object_or_cast(std::forward<decltype(key)>(key)).ptr(),     \
                    converted.ptr()                                                     \
                )) {                                                                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.fromkeys(<braced initializer list>, value)`. */       \
    template <typename K, typename V>                                                   \
    static inline cls fromkeys(std::initializer_list<K> keys, V&& value) {              \
        Object converted = detail::object_or_cast(std::forward<V>(value));              \
        PyObject* result = PyDict_New();                                                \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        try {                                                                           \
            for (const K& key : keys) {                                                 \
                if (PyDict_SetItem(                                                     \
                    result,                                                             \
                    detail::object_or_cast(key).ptr(),                                  \
                    converted.ptr()                                                     \
                )) {                                                                    \
                    throw error_already_set();                                          \
                }                                                                       \
            }                                                                           \
            return reinterpret_steal<cls>(result);                                      \
        } catch (...) {                                                                 \
            Py_DECREF(result);                                                          \
            throw;                                                                      \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.get(key)`.  Returns None if the key is not found. */  \
    template <typename K>                                                               \
    inline Object get(K&& key) const {                                                  \
        PyObject* result = PyDict_GetItemWithError(                                     \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr()                          \
        );                                                                              \
        if (result == nullptr) {                                                        \
            if (PyErr_Occurred()) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_borrow<Object>(Py_None);                                 \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.get(key, default_value)`. */                          \
    template <typename K, typename V>                                                   \
    inline Object get(K&& key, V&& default_value) const {                               \
        PyObject* result = PyDict_GetItemWithError(                                     \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr()                          \
        );                                                                              \
        if (result == nullptr) {                                                        \
            if (PyErr_Occurred()) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return detail::object_or_cast(std::forward<V>(default_value));              \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.pop(key)`.  Returns None if the key is not found. */  \
    template <typename K>                                                               \
    inline Object pop(K&& key) {                                                        \
        PyObject* result = PyDict_GetItemWithError(                                     \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr()                          \
        );                                                                              \
        if (result == nullptr) {                                                        \
            if (PyErr_Occurred()) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return reinterpret_borrow<Object>(Py_None);                                 \
        }                                                                               \
        if (PyDict_DelItem(this->ptr(), result)) {                                      \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.pop(key, default_value)`. */                          \
    template <typename K, typename V>                                                   \
    inline Object pop(K&& key, V&& default_value) {                                     \
        PyObject* result = PyDict_GetItemWithError(                                     \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr()                          \
        );                                                                              \
        if (result == nullptr) {                                                        \
            if (PyErr_Occurred()) {                                                     \
                throw error_already_set();                                              \
            }                                                                           \
            return detail::object_or_cast(std::forward<V>(default_value));              \
        }                                                                               \
        if (PyDict_DelItem(this->ptr(), result)) {                                      \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.popitem()`. */                                        \
    inline Object popitem() {                                                           \
        return this->attr("popitem")();                                                 \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.setdefault(key)`. */                                  \
    template <typename K>                                                               \
    inline Object setdefault(K&& key) {                                                 \
        PyObject* result = PyDict_SetDefault(                                           \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr(),                         \
            Py_None                                                                     \
        );                                                                              \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.setdefault(key, default_value)`. */                   \
    template <typename K, typename V>                                                   \
    inline Object setdefault(K&& key, V&& default_value) {                              \
        PyObject* result = PyDict_SetDefault(                                           \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<K>(key)).ptr(),                         \
            detail::object_or_cast(std::forward<V>(default_value)).ptr()                \
        );                                                                              \
        if (result == nullptr) {                                                        \
            throw error_already_set();                                                  \
        }                                                                               \
        return reinterpret_steal<Object>(result);                                       \
    }                                                                                   \
                                                                                        \
    /* TODO: overload update() to handle std::unordered_map, std::map, etc. */          \
                                                                                        \
    /* Equivalent to Python `dict.update()`, without an argument. */                    \
    inline void update() {}                                                             \
                                                                                        \
    /* Equivalent to Python `dict.update(items)`. */                                    \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<std::is_base_of_v<pybind11::dict, T>, int> = 0                 \
    >                                                                                   \
    inline void update(T&& items) {                                                     \
        if (PyDict_Merge(this->ptr(), items.ptr(), 1)) {                                \
            throw error_already_set();                                                  \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.update(items)`. */                                    \
    template <                                                                          \
        typename T,                                                                     \
        std::enable_if_t<!std::is_base_of_v<pybind11::dict, T>, int> = 0                \
    >                                                                                   \
    inline void update(T&& items) {                                                     \
        if (PyDict_MergeFromSeq2(                                                       \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(items)).ptr(),                       \
            1                                                                           \
        )) {                                                                            \
            throw error_already_set();                                                  \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.update(<braced initializer list>)`. */                \
    inline void update(std::initializer_list<Initializer> items) {                      \
        for (auto&& item : items) {                                                     \
            if (PyDict_SetItem(this->ptr(), item.key.ptr(), item.value.ptr())) {        \
                throw error_already_set();                                              \
            }                                                                           \
        }                                                                               \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.keys()`. */                                           \
    inline KeysView keys() const {                                                      \
        return this->attr("keys")();                                                    \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.values()`. */                                         \
    inline ValuesView values() const {                                                  \
        return this->attr("values")();                                                  \
    }                                                                                   \
                                                                                        \
    /* Equivalent to Python `dict.items()`. */                                          \
    inline ItemsView items() const {                                                    \
        return this->attr("items")();                                                   \
    }                                                                                   \
                                                                                        \
        /*    OPERATORS    */                                                           \
                                                                                        \
    inline auto begin() const { return pybind11::object::begin(); }                     \
    inline auto end() const { return pybind11::object::end(); }                         \
                                                                                        \
    /* Equivalent to Python `key in dict`. */                                           \
    template <typename T>                                                               \
    inline bool contains(T&& key) const {                                               \
        int result = PyDict_Contains(                                                   \
            this->ptr(),                                                                \
            detail::object_or_cast(std::forward<T>(key)).ptr()                          \
        );                                                                              \
        if (result == -1) {                                                             \
            throw error_already_set();                                                  \
        }                                                                               \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
    using Compare::operator==;                                                          \
    using Compare::operator!=;                                                          \
                                                                                        \
    template <typename T>                                                               \
    inline cls operator|(T&& other) const {                                             \
        cls result = copy();                                                            \
        result.update(std::forward<T>(other));                                          \
        return result;                                                                  \
    }                                                                                   \
                                                                                        \
    template <typename T>                                                               \
    inline cls& operator|=(T&& other) {                                                 \
        update(std::forward<T>(other));                                                 \
        return *this;                                                                   \
    }                                                                                   \

}  // namespace impl


class MappingProxy :
    public pybind11::object,
    public impl::EqualCompare<MappingProxy>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<MappingProxy>;

    inline static bool mappingproxy_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictProxy_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_mappingproxy(PyObject* obj) {
        PyObject* result = PyDictProxy_New(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(MappingProxy, mappingproxy_check, convert_to_mappingproxy);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `mappingproxy.copy()`. */
    inline Dict copy() const;  // out of line to avoid circular dependency.

    /* Equivalent to Python `mappingproxy.get(key)`. */
    template <typename K, typename V>
    inline Object get(K&& key) const {
        return this->attr("get")(std::forward<K>(key), py::None);
    }

    /* Equivalent to Python `mappingproxy.get(key, default)`. */
    template <typename K, typename V>
    inline Object get(K&& key, V&& default_value) const {
        return this->attr("get")(std::forward<K>(key), std::forward<V>(default_value));
    }

    /* Equivalent to Python `mappingproxy.keys()`. */
    inline KeysView keys() const;

    /* Equivalent to Python `mappingproxy.values()`. */
    inline ValuesView values() const;

    /* Equivalent to Python `mappingproxy.items()`. */
    inline ItemsView items() const;

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `key in mappingproxy`. */
    template <typename T>
    inline bool contains(T&& key) const;

    using Compare::operator==;
    using Compare::operator!=;

    // Operator overloads provided out of line to avoid circular dependency.

};


/* New subclass of pybind11::object representing a view into the keys of a dictionary
object. */
struct KeysView :
    public pybind11::object,
    public impl::FullCompare<KeysView>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<KeysView>;

    inline static bool keys_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictKeys_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_keys(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "keys");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    CONSTRUCTORS(KeysView, keys_check, convert_to_keys);

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `dict.keys().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Equivalent to Python `dict.keys().isdisjoint(other)`. */
    template <typename T>
    inline bool isdisjoint(T&& other) const {
        return this->attr("isdisjoint")(std::forward<T>(other)).template cast<bool>();
    }

    /* Get the length of the keys view. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to `key in dict.keys()`. */
    inline bool contains(const Handle& key) const {
        int result = PySequence_Contains(this->ptr(), key.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    template <typename T>
    inline Set operator|(T&& other) const {
        return Set(this->attr("__or__")(std::forward<T>(other)));
    }

    template <typename T>
    inline Set operator&(T&& other) const {
        return Set(this->attr("__and__")(std::forward<T>(other)));
    }

    template <typename T>
    inline Set operator-(T&& other) const {
        return Set(this->attr("__sub__")(std::forward<T>(other)));
    }

    template <typename T>
    inline Set operator^(T&& other) const {
        return Set(this->attr("__xor__")(std::forward<T>(other)));
    }

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;
};


/* New subclass of pybind11::object representing a view into the values of a dictionary
object. */
struct ValuesView :
    public pybind11::object,
    public impl::FullCompare<ValuesView>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<ValuesView>;

    inline static bool values_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictValues_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_values(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "values");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    CONSTRUCTORS(ValuesView, values_check, convert_to_values);

    /* Equivalent to Python `dict.values().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Get the length of the values view. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Equivalent to `value in dict.values()`. */
    inline bool contains(const Handle& value) const {
        int result = PySequence_Contains(this->ptr(), value.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;
};


/* New subclass of pybind11::object representing a view into the items of a dictionary
object. */
struct ItemsView :
    public pybind11::object,
    public impl::FullCompare<ItemsView>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<ItemsView>;

    inline static bool items_check(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &PyDictItems_Type);
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_items(PyObject* obj) {
        if (PyDict_Check(obj)) {
            PyObject* attr = PyObject_GetAttrString(obj, "items");
            if (attr == nullptr) {
                throw error_already_set();
            }
            PyObject* result = PyObject_CallNoArgs(attr);
            Py_DECREF(attr);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        } else {
            throw TypeError("expected a dict");
        }
    }

public:
    CONSTRUCTORS(ItemsView, items_check, convert_to_items);

    /* Equivalent to Python `dict.items().mapping`. */
    inline MappingProxy mapping() const {
        return this->attr("mapping");
    }

    /* Get the length of the values view. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyObject_Length(this->ptr()));
    }

    /* Equivalent to `value in dict.values()`. */
    inline bool contains(const Handle& value) const {
        int result = PySequence_Contains(this->ptr(), value.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;
};


/* Wrapper around pybind11::dict that allows it to be directly initialized using
std::initializer_list and enables extra C API functionality. */
class Dict :
    public pybind11::dict,
    public impl::EqualCompare<Dict>
{
    using Base = pybind11::dict;
    using Compare = impl::EqualCompare<Dict>;
    DICT_INTERFACE(Dict);
};


/* Subclass of Dict representing keyword arguments to a Python function.  For
compatibility with pybind11. */
struct Kwargs :
    public pybind11::kwargs,
    public impl::EqualCompare<Kwargs>
{
    using Base = pybind11::kwargs;
    using Compare = impl::EqualCompare<Kwargs>;
    DICT_INTERFACE(Kwargs);
};


/* Out of line definition for `MappingProxy.copy()`. */
inline Dict MappingProxy::copy() const{
    return this->attr("copy")();
}


/* Out of line definition for `MappingProxy.keys()`. */
inline KeysView MappingProxy::keys() const{
    return this->attr("keys")();
}


/* Out of line definition for `MappingProxy.values()`. */
inline ValuesView MappingProxy::values() const{
    return this->attr("values")();
}


/* Out of line definition for `MappingProxy.contains()`. */
template <typename T>
inline bool MappingProxy::contains(T&& key) const {
    return this->keys().contains(detail::object_or_cast(std::forward<T>(key)));
}


template <typename T>
inline Dict operator|(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__or__")(std::forward<T>(other));
}


template <typename T>
inline Dict operator&(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__and__")(std::forward<T>(other));
}


template <typename T>
inline Dict operator-(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__sub__")(std::forward<T>(other));
}


template <typename T>
inline Dict operator^(const MappingProxy& mapping, T&& other) {
    return mapping.attr("__xor__")(std::forward<T>(other));
}


#undef DICT_INTERFACE


/* Wrapper around pybind11::str that enables extra C API functionality. */
class Str :
    public pybind11::str,
    public impl::SequenceOps<Str>,
    public impl::FullCompare<Str>
{
    using Base = pybind11::str;
    using Ops = impl::SequenceOps<Str>;
    using Compare = impl::FullCompare<Str>;

    template <typename T>
    inline auto to_format_string(T&& arg) -> decltype(auto) {
        using U = std::decay_t<T>;
        if constexpr (std::is_base_of_v<pybind11::handle, U>) {
            return arg.ptr();
        } else if constexpr (std::is_base_of_v<std::string, U>) {
            return arg.c_str();
        } else if constexpr (std::is_base_of_v<std::string_view, U>) {
            return arg.data();
        } else {
            return std::forward<T>(arg);
        }
    }

    static PyObject* convert_to_str(PyObject* obj) {
        PyObject* result = PyObject_Str(obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Str, PyUnicode_Check, convert_to_str);

    /* Default constructor.  Initializes to empty string. */
    inline Str() : Base([] {
        PyObject* result = PyUnicode_FromString("");
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a unicode string from a printf-style format string.  See the python
    docs for `PyUnicode_FromFormat()` for more details.  Note that this can segfault
    if the argument types do not match the format code(s). */
    template <typename... Args>
    explicit Str(const char* format, Args&&... args) : Base([&] {
        PyObject* result = PyUnicode_FromFormat(
            format,
            to_format_string(std::forward<Args>(args))...
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args>
    explicit Str(const std::string& format, Args&&... args) : Str(
        format.c_str(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args>
    explicit Str(const std::string_view& format, Args&&... args) : Str(
        format.data(), std::forward<Args>(args)...
    ) {}

    /* Construct a unicode string from a printf-style format string.  See
    Str(const char*, ...) for more details. */
    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    explicit Str(const pybind11::str& format, Args&&... args) : Str(
        format.template cast<std::string>(), std::forward<Args>(args)...
    ) {}

    ///////////////////////////////////
    ////    PyUnicode_* METHODS    ////
    ///////////////////////////////////

    /* Get the underlying unicode buffer. */
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->ptr());
    }

    /* Get the length of the string in unicode code points. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(this->ptr()));
    }

    /* Get the kind of the string, indicating the size of the unicode points stored
    within. */
    inline int kind() const noexcept {
        return PyUnicode_KIND(this->ptr());
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    inline Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->ptr());
    }

    /* Fill the string with a given character.  The input must be convertible to a
    string with a single character. */
    template <typename T>
    void fill(T&& ch) {
        Str str = cast<Str>(std::forward<T>(ch));
        if (str.size() != 1) {
            std::ostringstream msg;
            msg << "fill character must be a single character, not '" << str << "'";
            throw ValueError(msg.str());
        }
        Py_UCS4 code = PyUnicode_ReadChar(str.ptr(), 0);
        if (code == (Py_UCS4)-1 && PyErr_Occurred()) {
            throw error_already_set();
        }
        if (PyUnicode_Fill(this->ptr(), 0, size(), code) == -1) {
            throw error_already_set();
        }
    }

    /* Fill the string with a given character, given as a raw Python unicode point. */
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_Fill(this->ptr(), 0, size(), ch) == -1) {
            throw error_already_set();
        }
    }

    /* Return a substring from this string. */
    inline Str substring(Py_ssize_t start = 0, Py_ssize_t end = -1) const {
        PyObject* result = PyUnicode_Substring(this->ptr(), start, end);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Equivalent to Python `str.capitalize()`. */
    inline Str capitalize() const {
        return this->attr("capitalize")();
    }

    /* Equivalent to Python `str.casefold()`. */
    inline Str casefold() const {
        return this->attr("casefold")();
    }

    /* Equivalent to Python `str.center(width)`. */
    template <typename... Args>
    inline Str center(Args&&... args) const {
        return this->attr("center")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.copy()`. */
    inline Str copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            throw error_already_set();
        }
        if (PyUnicode_CopyCharacters(result, 0, this->ptr(), 0, size())) {
            Py_DECREF(result);
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Count the number of occurrences of a substring within the string. */
    template <typename T>
    inline Py_ssize_t count(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Count(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
            start,
            stop
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.encode(encoding)`. */
    template <typename... Args>
    inline Bytes encode(Args&&... args) const {
        return this->attr("encode")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.endswith(suffix[, start[, end]])`. */
    template <typename T>
    inline bool endswith(
        T&& suffix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(suffix)).ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.expandtabs()`. */
    inline Str expandtabs(Py_ssize_t tabsize = 8) const {
        return this->attr("expandtabs")(tabsize);
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t find(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
            start,
            stop,
            1
        );
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, except that the substring
    is given as a single Python unicode character. */
    inline Py_ssize_t find(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
    }

    /* Equivalent to Python `str.format(*args, **kwargs)`. */
    template <typename... Args>
    inline Str format(Args&&... args) const {
        return this->attr("format")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.format_map(mapping)`. */
    template <typename T>
    inline Str format_map(T&& mapping) const {
        return this->attr("format_map")(std::forward<T>(mapping));
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`. */
    template <typename T>
    inline Py_ssize_t index(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr(),
            start,
            stop,
            1
        );
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.index(sub[, start[, end]])`, except that the substring
    is given as a single Python unicode character. */
    inline Py_ssize_t index(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, 1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.isalnum()`. */
    inline bool isalnum() const {
        return this->attr("isalnum")().cast<bool>();
    }

    /* Equivalent to Python `str.isalpha()`. */
    inline bool isalpha() const {
        return this->attr("isalpha")().cast<bool>();
    }

    /* Equivalent to Python `str.isascii()`. */
    inline bool isascii() const {
        return this->attr("isascii")().cast<bool>();
    }

    /* Equivalent to Python `str.isdecimal()`. */
    inline bool isdecimal() const {
        return this->attr("isdecimal")().cast<bool>();
    }

    /* Equivalent to Python `str.isdigit()`. */
    inline bool isdigit() const {
        return this->attr("isdigit")().cast<bool>();
    }

    /* Equivalent to Python `str.isidentifier()`. */
    inline bool isidentifier() const {
        return this->attr("isidentifier")().cast<bool>();
    }

    /* Equivalent to Python `str.islower()`. */
    inline bool islower() const {
        return this->attr("islower")().cast<bool>();
    }

    /* Equivalent to Python `str.isnumeric()`. */
    inline bool isnumeric() const {
        return this->attr("isnumeric")().cast<bool>();
    }

    /* Equivalent to Python `str.isprintable()`. */
    inline bool isprintable() const {
        return this->attr("isprintable")().cast<bool>();
    }

    /* Equivalent to Python `str.isspace()`. */
    inline bool isspace() const {
        return this->attr("isspace")().cast<bool>();
    }

    /* Equivalent to Python `str.istitle()`. */
    inline bool istitle() const {
        return this->attr("istitle")().cast<bool>();
    }

    /* Equivalent to Python `str.isupper()`. */
    inline bool isupper() const {
        return this->attr("isupper")().cast<bool>();
    }

    /* Equivalent of Python `str.join(iterable)`. */
    template <typename T>
    inline Str join(T&& iterable) const {
        PyObject* result = PyUnicode_Join(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(iterable)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent to Python `str.ljust(width[, fillchar])`. */
    template <typename... Args>
    inline Str ljust(Args&&... args) const {
        return this->attr("ljust")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.lower()`. */
    inline Str lower() const {
        return this->attr("lower")();
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <typename... Args>
    inline Str lstrip(Args&&... args) const {
        return this->attr("lstrip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python (static) `str.maketrans(x)`. */
    template <typename... Args> 
    inline static Dict maketrans(Args&&... args) {
        pybind11::type cls(
            reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type))
        );
        return cls.attr("maketrans")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <typename T>
    inline Tuple partition(T&& sep) const {
        return this->attr("partition")(std::forward<T>(sep));
    }

    /* Equivalent to Python `str.removeprefix(prefix)`. */
    template <typename T>
    inline Str removeprefix(T&& prefix) const {
        return this->attr("removeprefix")(std::forward<T>(prefix));
    }

    /* Equivalent to Python `str.removesuffix(suffix)`. */
    template <typename T>
    inline Str removesuffix(T&& suffix) const {
        return this->attr("removesuffix")(std::forward<T>(suffix));
    }

    /* Equivalent to Python `str.replace(old, new[, count])`. */
    template <typename T, typename U>
    inline Str replace(T&& substr, U&& replstr, Py_ssize_t maxcount = -1) const {
        PyObject* result = PyUnicode_Replace(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(substr)).ptr(),
            detail::object_or_cast(std::forward<U>(replstr)).ptr(),
            maxcount
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t rfind(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_Find(this->ptr(), sub, start, stop, -1);
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    inline Py_ssize_t rfind(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        return PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`. */
    template <typename T>
    inline Py_ssize_t rindex(
        T&& sub,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_Find(this->ptr(), sub, start, stop, -1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`, except that the
    substring is given as a single Python unicode character. */
    inline Py_ssize_t rindex(
        Py_UCS4 ch,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        Py_ssize_t result = PyUnicode_FindChar(this->ptr(), ch, start, stop, -1);
        if (result == -1) {
            throw ValueError("substring not found");
        }
        return result;
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <typename... Args>
    inline Str rjust(Args&&... args) const {
        return this->attr("rjust")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <typename T>
    inline Tuple rpartition(T&& sep) const {
        return this->attr("rpartition")(std::forward<T>(sep));
    }

    /* Equivalent to Python `str.rsplit([sep[, maxsplit]])`. */
    template <typename... Args>
    inline List rsplit(Args&&... args) const {
        return this->attr("rsplit")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <typename... Args>
    inline Str rstrip(Args&&... args) const {
        return this->attr("rstrip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.split()`. */
    inline List split(PyObject* separator = nullptr, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(this->ptr(), separator, maxsplit);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`. */
    template <typename T>
    inline List split(T&& separator, Py_ssize_t maxsplit = -1) const {
        PyObject* result = PyUnicode_Split(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(separator)).ptr(),
            maxsplit
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    inline List splitlines(bool keepends = false) const {
        PyObject* result = PyUnicode_Splitlines(this->ptr(), keepends);
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<List>(result);
    }

    /* Equivalent to Python `str.startswith(prefix[, start[, end]])`. */
    template <typename T>
    inline bool startswith(
        T&& prefix,
        Py_ssize_t start = 0,
        Py_ssize_t stop = -1
    ) const {
        int result = PyUnicode_Tailmatch(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(prefix)).ptr(),
            start,
            stop,
            -1
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Equivalent to Python `str.strip([chars])`. */
    template <typename... Args>
    inline Str strip(Args&&... args) const {
        return this->attr("strip")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `str.swapcase()`. */
    inline Str swapcase() const {
        return this->attr("swapcase")();
    }

    /* Equivalent to Python `str.title()`. */
    inline Str title() const {
        return this->attr("title")();
    }

    /* Equivalent to Python `str.translate(table)`. */
    template <typename T>
    inline Str translate(T&& table) const {
        return this->attr("translate")(std::forward<T>(table));
    }

    /* Equivalent to Python `str.upper()`. */
    inline Str upper() const {
        return this->attr("upper")();
    }

    /* Equivalent to Python `str.zfill(width)`. */
    inline Str zfill(Py_ssize_t width) const {
        return this->attr("zfill")(width);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Equivalent to Python `sub in str`. */
    template <typename T>
    inline bool contains(T&& sub) const {
        int result = PyUnicode_Contains(
            this->ptr(),
            detail::object_or_cast(std::forward<T>(sub)).ptr()
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    /* Concatenate this string with another. */
    template <typename T>
    inline Str concat(T&& other) const {
        PyObject* result = PyUnicode_Concat(
            this->ptr(), detail::object_or_cast(std::forward<T>(other)).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Str>(result);
    }

    using Base::operator[];
    using Ops::operator[];

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;

    using Ops::operator+;
    using Ops::operator*;
    using Ops::operator*=;

    template <typename T>
    inline Str& operator+=(T&& other) {
        *this = concat(std::forward<T>(other));
        return *this;
    }

};


/* Wrapper around a pybind11::type that enables extra C API functionality, such as the
ability to create new types on the fly by calling the type() metaclass, or directly
querying PyTypeObject* fields. */
class Type :
    public pybind11::type,
    public impl::EqualCompare<Type>
{
    using Base = pybind11::type;
    using Compare = impl::EqualCompare<Type>;

    static PyObject* convert_to_type(PyObject* obj) {
        return Py_NewRef(reinterpret_cast<PyObject*>(Py_TYPE(obj)));
    }

public:
    CONSTRUCTORS(Type, PyType_Check, convert_to_type);

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to the built-in type metaclass. */
    inline Type() : Base(reinterpret_borrow<Base>((PyObject*) &PyType_Type)) {}

    /* Dynamically create a new Python type by calling the type() metaclass. */
    template <typename T, typename U, typename V>
    explicit Type(T&& name, U&& bases, V&& dict) : Base([&name, &bases, &dict] {
        PyObject* result = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PyType_Type),
            detail::object_or_cast(std::forward<T>(name)).ptr(),
            detail::object_or_cast(std::forward<U>(bases)).ptr(),
            detail::object_or_cast(std::forward<V>(dict)).ptr(),
            nullptr
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Create a new heap type from a CPython PyType_Spec*.  Note that this is not
    exactly interchangeable with a standard call to the type metaclass directly, as it
    does not invoke any of the __init__(), __new__(), __init_subclass__(), or
    __set_name__() methods for the type or any of its bases. */
    explicit Type(PyType_Spec* spec) : Base([&spec] {
        PyObject* result = PyType_FromSpec(spec);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Create a new heap type from a CPython PyType_Spec and bases.  See
    Type(PyType_Spec*) for more information. */
    template <typename T>
    explicit Type(PyType_Spec* spec, T&& bases) : Base([&spec, &bases] {
        PyObject* result = PyType_FromSpecWithBases(spec, bases);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Create a new heap type from a module name, CPython PyType_Spec, and bases.
        See Type(PyType_Spec*) for more information. */
        template <typename T, typename U>
        explicit Type(T&& module, PyType_Spec* spec, U&& bases) : Base([&] {
            PyObject* result = PyType_FromModuleAndSpec(
                detail::object_or_cast(std::forward<T>(module)).ptr(),
                spec,
                detail::object_or_cast(std::forward<U>(bases)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }(), stolen_t{}) {}

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Create a new heap type from a full CPython metaclass, module name,
        PyType_Spec and bases.  See Type(PyType_Spec*) for more information. */
        template <typename T, typename U, typename V>
        explicit Type(T&& metaclass, U&& module, PyType_Spec* spec, V&& bases) : Base([&] {
            PyObject* result = PyType_FromMetaClass(
                detail::object_or_cast(std::forward<T>(metaclass)).ptr(),
                detail::object_or_cast(std::forward<U>(module)).ptr(),
                spec,
                detail::object_or_cast(std::forward<V>(bases)).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;;
        }(), stolen_t{}) {}

    #endif

    ///////////////////////////
    ////    PyType_ API    ////
    ///////////////////////////

    /* A proxy for the type's PyTypeObject* struct. */
    struct Slots {
        PyTypeObject* type_obj;

        #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

            /* Get the module that the type is defined in.  Can throw if called on a
            static type rather than a heap type (one that was created using
            PyType_FromModuleAndSpec() or higher). */
            inline Module module_() const noexcept {
                PyObject* result = PyType_GetModule(type_obj);
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Module>(result);
            }

        #endif

        /* Get type's tp_name slot. */
        inline const char* name() const noexcept {
            return type_obj->tp_name;
        }

        #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

            /* Get the type's qualified name. */
            inline Str qualname() const {
                PyObject* result = PyType_GetQualname(type_obj);
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Str>(result);
            }

        #endif

        /* Get the type's tp_basicsize slot. */
        inline Py_ssize_t basicsize() const noexcept {
            return type_obj->tp_basicsize;
        }

        /* Get the type's tp_itemsize slot. */
        inline Py_ssize_t itemsize() const noexcept {
            return type_obj->tp_itemsize;
        }

        /* Get the type's tp_dealloc slot. */
        inline destructor dealloc() const noexcept {
            return type_obj->tp_dealloc;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t tp_vectorcall_offset() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

        /* Get the type's tp_as_async slot. */
        inline PyAsyncMethods* as_async() const noexcept {
            return type_obj->tp_as_async;
        }

        /* Get the type's tp_repr slot. */
        inline reprfunc repr() const noexcept {
            return type_obj->tp_repr;
        }

        /* Get the type's tp_as_number slot. */
        inline PyNumberMethods* as_number() const noexcept {
            return type_obj->tp_as_number;
        }

        /* Get the type's tp_as_sequence slot. */
        inline PySequenceMethods* as_sequence() const noexcept {
            return type_obj->tp_as_sequence;
        }

        /* Get the type's tp_as_mapping slot. */
        inline PyMappingMethods* as_mapping() const noexcept {
            return type_obj->tp_as_mapping;
        }

        /* Get the type's tp_hash slot. */
        inline hashfunc hash() const noexcept {
            return type_obj->tp_hash;
        }

        /* Get the type's tp_call slot. */
        inline ternaryfunc call() const noexcept {
            return type_obj->tp_call;
        }

        /* Get the type's tp_str slot. */
        inline reprfunc str() const noexcept {
            return type_obj->tp_str;
        }

        /* Get the type's tp_getattro slot. */
        inline getattrofunc getattro() const noexcept {
            return type_obj->tp_getattro;
        }

        /* Get the type's tp_setattro slot. */
        inline setattrofunc setattro() const noexcept {
            return type_obj->tp_setattro;
        }

        /* Get the type's tp_as_buffer slot. */
        inline PyBufferProcs* as_buffer() const noexcept {
            return type_obj->tp_as_buffer;
        }

        /* Get the type's tp_flags slot. */
        inline unsigned long flags() const noexcept {
            return type_obj->tp_flags;
        }

        /* Get the type's tp_doc slot. */
        inline const char* doc() const noexcept {
            return type_obj->tp_doc;
        }

        /* Get the type's tp_traverse slot. */
        inline traverseproc traverse() const noexcept {
            return type_obj->tp_traverse;
        }

        /* Get the type's tp_clear slot. */
        inline inquiry clear() const noexcept {
            return type_obj->tp_clear;
        }

        /* Get the type's tp_richcompare slot. */
        inline richcmpfunc richcompare() const noexcept {
            return type_obj->tp_richcompare;
        }

        /* Get the type's tp_iter slot. */
        inline getiterfunc iter() const noexcept {
            return type_obj->tp_iter;
        }

        /* Get the type's tp_iternext slot. */
        inline iternextfunc iternext() const noexcept {
            return type_obj->tp_iternext;
        }

        /* Get the type's tp_methods slot. */
        inline PyMethodDef* methods() const noexcept {
            return type_obj->tp_methods;
        }

        /* Get the type's tp_members slot. */
        inline PyMemberDef* members() const noexcept {
            return type_obj->tp_members;
        }

        /* Get the type's tp_getset slot. */
        inline PyGetSetDef* getset() const noexcept {
            return type_obj->tp_getset;
        }

        /* Get the type's tp_base slot. */
        inline PyTypeObject* base() const noexcept {
            return type_obj->tp_base;
        }

        /* Get the type's tp_dict slot. */
        inline PyObject* dict() const noexcept {
            return type_obj->tp_dict;
        }

        /* Get the type's tp_descr_get slot. */
        inline descrgetfunc descr_get() const noexcept {
            return type_obj->tp_descr_get;
        }

        /* Get the type's tp_descr_set slot. */
        inline descrsetfunc descr_set() const noexcept {
            return type_obj->tp_descr_set;
        }

        /* Get the type's tp_bases slot. */
        inline Tuple bases() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_bases);
        }

        /* Get the type's tp_mro slot. */
        inline Tuple mro() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_mro);
        }

        /* Get the type's tp_finalize slot. */
        inline destructor finalize() const noexcept {
            return type_obj->tp_finalize;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t vectorcall() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

    };

    /* Access the type's internal slots. */
    inline Slots slots() const {
        return {reinterpret_cast<PyTypeObject*>(this->ptr())};
    }

    /* Finalize a type object in a C++ extension, filling in any inherited slots.  This
    should be called on a raw CPython type struct to finish its initialization. */
    inline static void READY(PyTypeObject* type) {
        if (PyType_Ready(type) < 0) {
            throw error_already_set();
        }
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method must
    be called after any manual modification to the attributes or this class or any of
    its bases. */
    inline void clear_cache() const noexcept {
        PyType_Modified(reinterpret_cast<PyTypeObject*>(this->ptr()));
    }

    /* Check whether this type is an actual subtype of a another type.  This avoids
    calling `__subclasscheck__()` on the parent type. */
    inline bool is_subtype(const pybind11::type& base) const {
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(this->ptr()),
            reinterpret_cast<PyTypeObject*>(base.ptr())
        );
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator==;
    using Compare::operator!=;

    // NOTE: indexing a Type object calls its __class_getitem__() method, just like
    // normal python.
};


namespace impl {

    // PyTypeObject* PyMethod_Type = nullptr;  // provided in Python.h
    PyTypeObject* PyClassMethod_Type = nullptr;
    PyTypeObject* PyStaticMethod_Type = nullptr;
    // PyTypeObject* PyProperty_Type = nullptr;  // provided in Python.h?

} // namespace impl


// TODO: class Code?




/* Wrapper around a pybind11::Function that allows it to be constructed from a C++
lambda or function pointer, and enables extra introspection via the C API. */
class Function : public pybind11::function {
    using Base = pybind11::function;

public:
    using Base::Base;
    using Base::operator=;

    template <typename Func>
    explicit Function(Func&& func) : Base([&func] {
        return pybind11::cpp_function(std::forward<Func>(func));
    }()) {}

    ///////////////////////////////
    ////    PyFunction_ API    ////
    ///////////////////////////////

    // TODO: introspection tools: name(), code(), etc.

    // /* Get the name of the file from which the code was compiled. */
    // inline std::string filename() const {
    //     return code().filename();
    // }


    // /* Get the first line number of the function. */
    // inline size_t line_number() const noexcept {
    //     return code().line_number();
    // }

    // /* Get the function's base name. */
    // inline std::string name() const {
    //     return code().name();
    // }


    // /* Get the module that the function is defined in. */
    // inline std::optional<Module<Ref::BORROW>> module_() const {
    //     PyObject* mod = PyFunction_GetModule(this->obj);
    //     if (mod == nullptr) {
    //         return std::nullopt;
    //     } else {
    //         return std::make_optional(Module<Ref::BORROW>(module));
    //     }
    // }



};


/* New subclass of pybind11::object that represents a bound method at the Python
level. */
class Method : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* New subclass of pybind11::object that represents a bound classmethod at the Python
level. */
class ClassMethod : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* Wrapper around a pybind11::StaticMethod that allows it to be constructed from a
C++ lambda or function pointer, and enables extra introspection via the C API. */
class StaticMethod : public pybind11::staticmethod {
    using Base = pybind11::staticmethod;

public:



};


/* New subclass of pybind11::object that represents a property descriptor at the
Python level. */
class Property : public pybind11::object {
    using Base = pybind11::object;

public:



};


// context manager type?


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/* NOTE: some operators are not reachable in C++ compared to Python simply due to
 * language limitations.  For instance, C++ has no `**`, `//`, or `@` operators, so
 * `pow()`, `floor_div()`, and `matrix_multiply()` must be used instead.
 */


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename T, typename U>
inline Object pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_Power(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename T, typename U, typename V>
inline Object pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_Power(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        detail::object_or_cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `base **= exp` (in-place exponentiation). */
template <typename T, typename U>
inline Object inplace_pow(T&& base, U&& exp) {
    PyObject* result = PyNumber_InPlacePower(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        Py_None
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* In-place modular exponentation.  Not reachable in Python, but implemented here for
completeness. */
template <typename T, typename U, typename V>
inline Object inplace_pow(T&& base, U&& exp, V&& mod) {
    PyObject* result = PyNumber_InPlacePower(
        detail::object_or_cast(std::forward<T>(base)).ptr(),
        detail::object_or_cast(std::forward<U>(exp)).ptr(),
        detail::object_or_cast(std::forward<V>(mod)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a // b` (floor division). */
template <typename T, typename U>
inline Object floor_div(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_FloorDivide(
        detail::object_or_cast(std::forward<T>(obj)).ptr(),
        detail::object_or_cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a //= b` (in-place floor division). */
template <typename T, typename U>
inline Object inplace_floor_div(T&& obj, U&& divisor) {
    PyObject* result = PyNumber_InPlaceFloorDivide(
        detail::object_or_cast(std::forward<T>(obj)).ptr(),
        detail::object_or_cast(std::forward<U>(divisor)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `divmod(a, b)`. */
template <typename T, typename U>
inline Tuple divmod(T&& a, U&& b) {
    PyObject* result = PyNumber_Divmod(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Tuple>(result);
}


/* Equivalent to Python `a @ b` (matrix multiplication). */
template <typename T, typename U>
inline Object matrix_multiply(T&& a, U&& b) {
    PyObject* result = PyNumber_MatrixMultiply(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `a @= b` (in-place matrix multiplication). */
template <typename T, typename U>
inline Object inplace_matrix_multiply(T&& a, U&& b) {
    PyObject* result = PyNumber_InPlaceMatrixMultiply(
        detail::object_or_cast(std::forward<T>(a)).ptr(),
        detail::object_or_cast(std::forward<U>(b)).ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* NOTE: pybind11 does not support all of Python's built-in functions by default, which
 * makes the code to be more verbose and less idiomatic.  This can sometimes lead users
 * to reach for the Python C API, which is both unfamiliar and error prone.  In keeping
 * with the Principle of Least Surprise, we attempt to replicate as much of the Python
 * standard library as possible here, so that users can more or less just copy/paste
 * from one to another.
 */


/* Equivalent to Python `import module` */
inline Module import(const char* module) {
    return pybind11::module_::import(module);
}


/* Equivalent to Python `locals()`. */
inline Dict locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_borrow<Dict>(result);
}


/* Equivalent to Python `type(obj)`. */
inline Type type(const pybind11::handle& obj) {
    return pybind11::type::of(obj);
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const pybind11::str& name, T&& bases, U&& dict) {
    return {name, std::forward<T>(bases), std::forward<U>(dict)};
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const char* name, T&& bases, U&& dict) {
    return {name, std::forward<T>(bases), std::forward<U>(dict)};
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const std::string& name, T&& bases, U&& dict) {
    return {name, std::forward<T>(bases), std::forward<U>(dict)};
}


/* Equivalent to Python `type(name, bases, namespace)`. */
template <typename T, typename U>
inline Type type(const std::string_view& name, T&& bases, U&& dict) {
    return {name, std::forward<T>(bases), std::forward<U>(dict)};
}


/* Equivalent to Python `issubclass(derived, base)`. */
template <typename T>
inline bool issubclass(const pybind11::type& derived, T&& base) {
    int result = PyObject_IsSubclass(
        derived.ptr(),
        detail::object_or_cast(std::forward<T>(base)).ptr()
    );
    if (result == -1) {
        throw error_already_set();
    }
    return result;
}


/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
inline List dir() {
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `dir(obj)`. */
inline List dir(const pybind11::handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `callable(obj)`. */
inline bool callable(const pybind11::handle& obj) {
    return PyCallable_Check(obj.ptr());
}


/* Equivalent to Python `range(stop)`. */
inline Range range(Py_ssize_t stop) {
    return Range(stop);
}


/* Equivalent to Python `range(start, stop[, step])`. */
inline Range range(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step = 1) {
    return Range(start, stop, step);
}


/* Equivalent to Python `aiter(obj)`. */
inline Object aiter(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "aiter"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `reversed(obj)`. */
inline Iterator reversed(const pybind11::handle& obj) {
    return obj.attr("__reversed__")();
}


/* Specialization of `reversed()` for Tuple and List objects that use direct array
access rather than going through the Python API. */
template <typename T, std::enable_if_t<impl::is_reverse_iterable<T>, int> = 0>
inline Iterator reversed(const T& obj) {
    using Iter = typename T::ReverseIterator;
    return pybind11::make_iterator(Iter(obj.data(), obj.size() - 1), Iter(-1));
}


/* Equivalent to Python `next(obj)`. */
inline Object next(const pybind11::iterator& iter) {
    PyObject* result = PyIter_Next(iter.ptr());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw error_already_set();
        }
        throw StopIteration();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `next(obj, default)`. */
template <typename T>
inline Object next(const pybind11::iterator& iter, T&& default_value) {
    PyObject* result = PyIter_Next(iter.ptr());
    if (result == nullptr) {
        if (PyErr_Occurred()) {
            throw error_already_set();
        }
        return detail::object_or_cast(std::forward<T>(default_value));
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `anext(obj)`. */
inline Object anext(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "anext"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `anext(obj, default)`. */
template <typename T>
inline Object anext(const pybind11::handle& obj, T&& default_value) {
    PyObject* result = PyObject_CallFunctionObjArgs(
        PyDict_GetItemString(PyEval_GetBuiltins(), "anext"),
        obj.ptr(),
        detail::object_or_cast(std::forward<T>(default_value)).ptr(),
        nullptr
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


// TODO: any(), all(), enumerate(), etc. ?  For full coverage.


/* Equivalent to Python `abs(obj)`. */
inline Object abs(const pybind11::handle& obj) {
    PyObject* result = PyNumber_Absolute(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `math.floor(number)`.  Used to implement the built-in `round()`
function. */
inline Object floor(const pybind11::handle& number) {
    return number.attr("__floor__")();
}


/* Equivalent to Python `math.ceil(number)`.  Used to implement the built-in `round()`
function. */
inline Object ceil(const pybind11::handle& number) {
    return number.attr("__ceil__")();
}


/* Equivalent to Python `math.trunc(number)`.  Used to implement the built-in `round()`
function. */
inline Object trunc(const pybind11::handle& number) {
    return number.attr("__trunc__")();
}


/* Equivalent to Python str(). */
inline Str str(const pybind11::handle& obj) {
    PyObject* string = PyObject_Str(obj.ptr());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ascii(obj)`.  Like `repr()`, but returns an ASCII-encoded
string. */
inline Str ascii(const pybind11::handle& obj) {
    PyObject* result = PyObject_ASCII(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(result);
}


/* Equivalent to Python `bin(obj)`.  Converts an integer or other object implementing
__index__() into a binary string representation. */
inline Str bin(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 2);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `oct(obj)`.  Converts an integer or other object implementing
__index__() into an octal string representation. */
inline Str oct(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 8);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `hex(obj)`.  Converts an integer or other object implementing
__index__() into a hexadecimal string representation. */
inline Str hex(const pybind11::handle& obj) {
    PyObject* string = PyNumber_ToBase(obj.ptr(), 16);
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `chr(obj)`.  Converts an integer or other object implementing
__index__() into a unicode character. */
inline Str chr(const pybind11::handle& obj) {
    PyObject* string = PyUnicode_FromFormat("%llc", obj.cast<long long>());
    if (string == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Str>(string);
}


/* Equivalent to Python `ord(obj)`.  Converts a unicode character into an integer
representation. */
Int ord(const pybind11::handle& obj) {
    PyObject* ptr = obj.ptr();
    if (ptr == nullptr) {
        throw TypeError("cannot call ord() on a null object");
    }

    if (!PyUnicode_Check(ptr)) {
        std::ostringstream msg;
        msg << "ord() expected a string of length 1, but ";
        msg << Py_TYPE(ptr)->tp_name << "found";
        throw TypeError(msg.str());
    }

    Py_ssize_t length = PyUnicode_GET_LENGTH(ptr);
    if (length != 1) {
        std::ostringstream msg;
        msg << "ord() expected a character, but string of length " << length;
        msg << " found";
        throw TypeError(msg.str());
    }

    return PyUnicode_READ_CHAR(ptr, 0);
}


/* Equivalent to Python `round(obj)`. */
inline Object round(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "round"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<Object>(result);
}


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const pybind11::handle& obj) {
    PyObject* result = PyObject_CallOneArg(
        PyDict_GetItemString(PyEval_GetBuiltins(), "sorted"),
        obj.ptr()
    );
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


}  // namespace py
}  // namespace bertrand


// TODO: implement type casters for range, MappingProxy, KeysView, ValuesView,
// ItemsView, Method, ClassMethod, StaticMethod, Property


// type casters for custom pybind11 types
namespace pybind11 {
namespace detail {

template <>
struct type_caster<bertrand::py::Complex> {
    PYBIND11_TYPE_CASTER(bertrand::py::Complex, _("Complex"));

    /* Convert a Python object into a Complex value. */
    bool load(handle src, bool convert) {
        if (PyComplex_Check(src.ptr())) {
            value = reinterpret_borrow<bertrand::py::Complex>(src.ptr());
            return true;
        }

        if (!convert) {
            return false;
        }

        Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());
        if (complex_struct.real == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }

        value = bertrand::py::Complex(complex_struct.real, complex_struct.imag);
        return true;
    }

    /* Convert a Complex value into a Python object. */
    inline static handle cast(
        const bertrand::py::Complex& src,
        return_value_policy /* policy */,
        handle /* parent */
    ) {
        return Py_XNewRef(src.ptr());
    }

};

/* NOTE: pybind11 already implements a type caster for the generic std::complex<T>, so
we can't override it directly.  However, we can specialize it at a lower level to
handle the specific std::complex<> types that we want, which effectively bypasses the
pybind11 implementation.  This is a bit of a hack, but it works. */
#define COMPLEX_CASTER(T)                                                               \
    template <>                                                                         \
    struct type_caster<std::complex<T>> {                                               \
        PYBIND11_TYPE_CASTER(std::complex<T>, _("complex"));                            \
                                                                                        \
        /* Convert a Python object into a std::complex<T> value. */                     \
        bool load(handle src, bool convert) {                                           \
            if (src.ptr() == nullptr) {                                                 \
                return false;                                                           \
            }                                                                           \
            if (!convert && !PyComplex_Check(src.ptr())) {                              \
                return false;                                                           \
            }                                                                           \
            Py_complex complex_struct = PyComplex_AsCComplex(src.ptr());                \
            if (complex_struct.real == -1.0 && PyErr_Occurred()) {                      \
                PyErr_Clear();                                                          \
                return false;                                                           \
            }                                                                           \
            value = std::complex<T>(                                                    \
                static_cast<T>(complex_struct.real),                                    \
                static_cast<T>(complex_struct.imag)                                     \
            );                                                                          \
            return true;                                                                \
        }                                                                               \
                                                                                        \
        /* Convert a std::complex<T> value into a Python object. */                     \
        inline static handle cast(                                                      \
            const std::complex<T>& src,                                                 \
            return_value_policy /* policy */,                                           \
            handle /* parent */                                                         \
        ) {                                                                             \
            return bertrand::py::Complex(src).release();                                \
        }                                                                               \
                                                                                        \
    };                                                                                  \

COMPLEX_CASTER(float);
COMPLEX_CASTER(double);
COMPLEX_CASTER(long double);

#undef COMPLEX_CASTER

} // namespace detail
} // namespace pybind11


#undef CONSTRUCTORS
#endif // BERTRAND_PYBIND_H
