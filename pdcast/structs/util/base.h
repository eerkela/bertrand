#ifndef BERTRAND_STRUCTS_UTIL_BASE_H
#define BERTRAND_STRUCTS_UTIL_BASE_H

#include <type_traits>  // std::is_pointer_v<>, std::is_convertible_v<>, etc.
#include <Python.h>  // CPython API


namespace bertrand {


/* Check if a type is convertible to PyObject*. */
template <typename T>
inline constexpr bool is_pyobject = (
    std::is_pointer_v<std::remove_reference_t<T>> &&
    std::is_convertible_v<
        std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<T>>>,
        PyObject
    >
);


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_BASE_H
