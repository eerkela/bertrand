// include guard: BERTRAND_STRUCTS_UTIL_SEQUENCE_H
#ifndef BERTRAND_STRUCTS_UTIL_SEQUENCE_H
#define BERTRAND_STRUCTS_UTIL_SEQUENCE_H

#include <cstddef>  // size_t
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python<>, TypeError
#include "iter.h"  // iter()


/* NOTE: This file contains a universal way to unpack a C++ or Python iterator into a
 * simple container with a definite size and random access support.  It is mostly used
 * in slice operations where the length of the slice must be known in advance.
 */


// TODO: If the input to sequence() already supports .size() and operator[], then we
// can return it directly as a no-op.  Otherwise, we unpack it into a vector and return
// that.  Similarly, if a Python sequence is passed that already supports 


namespace bertrand {
namespace util {


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
template <typename T>
class PySequence {
    PyObject* sequence;
    size_t length;

    /* Unpack a Python iterable into a fast sequence. */
    inline PyObject* unpack(const T& iterable) {
        PyObject* seq = PySequence_Fast(iterable, "could not unpack Python iterable");
        if (seq == nullptr) {
            throw catch_python<TypeError>();
        }
        return seq;
    }

public:

    /* Construct a PySequence from an iterable or other sequence. */
    PySequence(const T& items) :
        sequence(unpack(items)),
        length(static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence)))
    {}

    /* Release the Python sequence on destruction. */
    ~PySequence() { Py_DECREF(sequence); }

    /* Iterate over the sequence. */
    inline auto begin() const { return iter(this->sequence).begin(); }
    inline auto cbegin() const { return iter(this->sequence).cbegin(); }
    inline auto end() const { return iter(this->sequence).end(); }
    inline auto cend() const { return iter(this->sequence).cend(); }
    inline auto rbegin() const { return iter(this->sequence).rbegin(); }
    inline auto crbegin() const { return iter(this->sequence).crbegin(); }
    inline auto rend() const { return iter(this->sequence).rend(); }
    inline auto crend() const { return iter(this->sequence).crend(); }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const { return PySequence_Fast_ITEMS(sequence); }
    inline size_t size() const { return length; }

    /* Get the value at a particular index of the sequence. */
    inline PyObject* operator[](size_t index) const {
        if (index >= length) {
            throw std::out_of_range("index out of range");
        }
        return PySequence_Fast_GET_ITEM(sequence, index);  // borrowed reference
    }

};


/*  */
template <typename T>
class CppSequence {
    using Iterator = decltype(iter(std::declval<T>()).begin());
    using deref_type = decltype(*std::declval<Iterator>());
    std::vector<deref_type> sequence;

    /* Unpack a C++ iterable into a fast sequence. */
    inline std::vector<deref_type> unpack(const T& iterable) {
        return std::vector<deref_type>(
            iter(iterable).begin(),
            iter(iterable).end()
        );
    }

public:

    /* Construct a CppSequence from an iterable or other sequence. */
    CppSequence(const T& items) : sequence(unpack(items)) {}

    /* Iterate over the sequence. */
    inline auto begin() const { return sequence.begin(); }
    inline auto cbegin() const { return sequence.cbegin(); }
    inline auto end() const { return sequence.end(); }
    inline auto cend() const { return sequence.cend(); }
    inline auto rbegin() const { return sequence.rbegin(); }
    inline auto crbegin() const { return sequence.crbegin(); }
    inline auto rend() const { return sequence.rend(); }
    inline auto crend() const { return sequence.crend(); }

    /* Get the underlying value array. */
    inline deref_type* data() const { return sequence.data(); }
    inline size_t size() const { return sequence.size(); }

    /* Get the value at a particular index of the sequence. */
    inline deref_type operator[](size_t index) const {
        if (index >= sequence.size()) {
            throw std::out_of_range("index out of range");
        }
        return sequence[index];
    }

};


/* Unpack an arbitrary Python iterable or C++ container into a  */
template <typename Iterable>
inline auto sequence(Iterable&& iterable) {
    if constexpr (is_pyobject<Iterable>) {
        return PySequence(iterable);
    } else {
        return CppSequence(iterable);
    }
}


}  // namespace util
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_SEQUENCE_H
