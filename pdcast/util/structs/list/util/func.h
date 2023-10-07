// include guard: BERTRAND_STRUCTS_UTIL_FUNC_H
#ifndef BERTRAND_STRUCTS_UTIL_FUNC_H
#define BERTRAND_STRUCTS_UTIL_FUNC_H

#include <type_traits>  // std::enable_if_t<>, std::invoke_result_t<>, etc.
#include <Python.h>  // CPython API


/* NOTE: This file contains utilities for working with C++ function pointers and Python
 * callables, including signature inspection and return type inference.
 */


namespace bertrand {
namespace structs {
namespace util {


/* A trait that infers the return type of a C++ function pointer that accepts the given
arguments. */
template <typename Func, typename Enable = void, typename... Args>
struct _ReturnType {
    using type = std::invoke_result_t<Func, Args...>;
};


/* A trait that represents the return type of a python callable (always PyObject*). */
template <typename Func, typename... Args>
struct _ReturnType<
    Func,
    std::enable_if_t<std::is_convertible_v<Func, PyObject*>>,
    Args...
> {
    using type = PyObject*;
};


/* Infer the return type of a Python callable or C++ function with the given
arguments. */
template <typename Func, typename... Args>
using ReturnType = typename _ReturnType<Func, void, Args...>::type;


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_FUNC_H
