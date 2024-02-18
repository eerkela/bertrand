#ifndef BERTRAND_PYTHON_CORE_H
#define BERTRAND_PYTHON_CORE_H

#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
#include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>



// TODO: Decimal?



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
class Set;  // done
class FrozenSet;  // done
class KeysView;  // done
class ItemsView;  // done
class ValuesView;  // done
class Dict;  // done
class MappingProxy;  // done
class Str;  // done
class Type;  // done
class Function;
class Method;
class ClassMethod;
class StaticMethod;
class Property;
class Timedelta;  // done
class Timezone;  // done
class Date;
class Time;
class Datetime;


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


//////////////////////////
////    EXCEPTIONS    ////
//////////////////////////








///////////////////////
////    HELPERS    ////
///////////////////////


// impl namespace is different from detail in order to avoid conflicts with pybind11
namespace impl {

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

        template <
            typename T,
            std::enable_if_t<!std::is_same_v<pybind11::handle, std::decay_t<T>>, int> = 0
        >
        Initializer(T&& value) : value(detail::object_or_cast(std::forward<T>(value))) {}
    };

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
        inline auto operator[](const std::initializer_list<SliceIndex>& slice) const {
            if (slice.size() > 3) {
                throw ValueError(
                    "slices must be of the form {[start[, stop[, step]]]}"
                );
            }
            size_t i = 0;
            std::array<Object, 3> params {None, None, None};
            for (const SliceIndex& item : slice) {
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
        cls& operator=(const std::initializer_list<T>& init) {                          \
            Base::operator=(cls(init));                                                 \
            return *this;                                                               \
        }                                                                               \

}  // namespace impl


/* New subclass of pybind11::object that represents Python's global NotImplemented
type. */
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


////////////////////////////////
////    GLOBAL FUNCTIONS    ////
////////////////////////////////


/* Equivalent to Python `import module` */
inline Module import(const char* module) {
    return std::move(pybind11::module_::import(module));
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


}  // namespace py
}  // namespace bertrand


// TODO: implement type casters for range, MappingProxy, KeysView, ValuesView,
// ItemsView, Method, ClassMethod, StaticMethod, Property


// #undef CONSTRUCTORS  // TODO: uncommenting this makes it impossible to use the
// CONSTRUCTORS macro in other classes.  Probably 

#endif // BERTRAND_PYTHON_CORE_H
