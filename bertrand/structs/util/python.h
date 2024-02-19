#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <cstdio>  // std::FILE, std::fopen
#include <deque>  // std::deque
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair, std::move, etc.
#include <valarray>  // std::valarray
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "func.h"  // FuncTraits, identity
#include "iter.h"  // iter()
#include "pybind.h"  // namespace py::



// TODO: support pickling?  __reduce__(), __reduce_ex__(), __setstate__(), __getstate__(), etc.



namespace bertrand {
namespace python {





/* An extension of python::Object that represents a bound Python method. */
template <Ref ref = Ref::STEAL>
class Method : public Object<ref, Method> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Method. */
    Method(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyMethod_Check(obj)) {
            std::ostringstream msg;
            msg << "expected a method, got " << repr(obj);
            throw TypeError(msg.str());
        }
        return obj;
    }()) {}

    /* Create a new method by binding an object to a function as an implicit `self`
    argument. */
    explicit Method(PyObject* self, PyObject* function) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new method"
        );
        if (self == nullptr) {
            throw TypeError("self object must not be null");
        }
        this->obj = PyMethod_New(function, self);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    // /* Wrap a function as a method descriptor.  When attached to a class, the
    // descriptor passes `self` as the first argument to the function. */
    // explicit Method(
    //     PyTypeObject* type,
    //     const char* name,
    //     PyCFunction function,
    //     int flags = METH_VARARGS | METH_KEYWORDS,
    //     const char* doc = nullptr
    // ) {
    //     static_assert(
    //         ref != Ref::BORROW,
    //         "Cannot construct a non-owning reference to a new method"
    //     );
    //     PyMethodDef method = {name, function, flags, doc};
    //     this->obj = PyDescr_NewMethod(type, &method);
    //     if (this->obj == nullptr) {
    //         throw catch_python();
    //     }
    // }

    //////////////////////////////////////
    ////    PyMethod_* API METHODS    ////
    //////////////////////////////////////

    /* Get the function object associated with the method. */
    inline Function<Ref::BORROW> function() const {
        return Function<Ref::BORROW>(PyMethod_GET_FUNCTION(this->obj));
    }

    /* Get the instance to which the method is bound. */
    inline Object<Ref::BORROW> self() const {
        return Object<Ref::BORROW>(PyMethod_GET_SELF(this->obj));
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string filename() const {
        return function().filename();
    }

    /* Get the first line number of the method. */
    inline size_t line_number() const {
        return function().line_number();
    }

    /* Get the module that the method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module_() const {
        return function().module_();
    }

    /* Get the method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the code object wrapped by this method. */
    inline Code<Ref::BORROW> code() const {
        return function().code();
    }

    /* Get the closure associated with the method.  This is a tuple of cell objects
    containing data captured by the method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const {
        return function().closure();
    }

    /* Set the closure associated with the method.  Input must be Py_None or a tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the globals dictionary associated with the method object. */
    inline Dict<Ref::BORROW> globals() const {
        return function().globals();
    }

    /* Get the total number of positional arguments for the method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the method, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t n_positional() const {
        return function().n_positional();
    }

    /* Get the number of keyword-only arguments for the method, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t n_keyword() const {
        return function().n_keyword();
    }

    /* Get the type annotations for each argument.  This is a mutable dictionary or
    null if no annotations are present. */
    inline Dict<Ref::BORROW> annotations() const {
        return function().annotations();
    }

    /* Set the annotations for the method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(Dict<Ref::NEW> annotations) {
        function().annotations(annotations);
    }

    /* Get the default values for the method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const {
        return function().defaults();
    }

    /* Set the default values for the method's arguments.  Input must be Py_None or a
    tuple. */
    inline void defaults(Dict<Ref::NEW> defaults) {
        function().defaults(defaults);
    }

};




/////////////////////
////    OTHER    ////
/////////////////////


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
template <Ref ref = Ref::STEAL>
class FastSequence : public Object<ref, FastSequence> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a PySequence from an iterable or other sequence. */
    FastSequence(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj) && !PyList_Check(obj)) {
            throw TypeError("expected a tuple or list");
        }
    }

    /////////////////////////////////////////
    ////    PySequence_Fast_* METHODS    ////
    /////////////////////////////////////////

    /* Get the size of the sequence. */
    inline size_t size() const {
        return static_cast<size_t>(PySequence_Fast_GET_SIZE(this->obj));
    }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Directly get an item within the sequence without boundschecking.  Returns a
    borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PySequence_Fast_GET_ITEM(this->obj, index);
    }

    /* Get the value at a particular index of the sequence.  Returns a borrowed
    reference. */
    inline PyObject* operator[](size_t index) const {
        if (index >= size()) {
            throw IndexError("index out of range");
        }
        return GET_ITEM(index);
    }

};


}  // namespace python


/* A trait that controls which C++ types are passed through the sequence() helper
without modification.  These must be vector or array types that support a definite
`.size()` method as well as integer-based indexing via the `[]` operator.  If a type
does not appear here, then it is converted into a std::vector instead. */
template <typename T>
struct SequenceFilter : std::false_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::vector<T, Alloc>> : std::true_type {};

template <typename T, size_t N> 
struct SequenceFilter<std::array<T, N>> : std::true_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::deque<T, Alloc>> : std::true_type {};

template <>
struct SequenceFilter<std::string> : std::true_type {};
template <>
struct SequenceFilter<std::wstring> : std::true_type {};
template <>
struct SequenceFilter<std::u16string> : std::true_type {};
template <>
struct SequenceFilter<std::u32string> : std::true_type {};

template <>
struct SequenceFilter<std::string_view> : std::true_type {};
template <>
struct SequenceFilter<std::wstring_view> : std::true_type {};
template <>
struct SequenceFilter<std::u16string_view> : std::true_type {};
template <>
struct SequenceFilter<std::u32string_view> : std::true_type {};

template <typename T>
struct SequenceFilter<std::valarray<T>> : std::true_type {};

#if __cplusplux >= 202002L  // C++20 or later

    template <typename T>
    struct SequenceFilter<std::span<T>> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string_view> : std::true_type {};

#endif



/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
auto sequence(Iterable&& iterable) {

    if constexpr (is_pyobject<Iterable>) {
        PyObject* seq = PySequence_Fast(iterable, "expected a sequence");
        return python::FastSequence<python::Ref::STEAL>(seq);

    } else {
        using Traits = ContainerTraits<Iterable>;
        static_assert(Traits::forward_iterable, "container must be forward iterable");

        // if the container already supports random access, then return it directly
        if constexpr (
            SequenceFilter<
                std::remove_cv_t<
                    std::remove_reference_t<Iterable>
                >
            >::value
        ) {
            return std::forward<Iterable>(iterable);
        } else {
            auto proxy = iter(iterable);
            auto it = proxy.begin();
            auto end = proxy.end();
            using Deref = decltype(*std::declval<decltype(it)>());
            return std::vector<Deref>(it, end);
        }
    }
}


}  // namespace bertrand


#undef PYTHON_SIMPLIFIED_ERROR_STATE
#endif  // BERTRAND_STRUCTS_UTIL_CONTAINER_H
