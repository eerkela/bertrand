#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_DECLARATIONS_H
#define BERTRAND_PYTHON_COMMON_DECLARATIONS_H

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <complex>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <ostream>
#include <ranges>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/functional.h>
#include <pybind11/iostream.h>
// #include <pybind11/numpy.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <bertrand/common.h>
#include "bertrand/static_str.h"


namespace bertrand {
namespace py {


///////////////////////////////////////
////    INHERITED FROM PYBIND11    ////
///////////////////////////////////////


/* Pybind11 documentation:
*     https://pybind11.readthedocs.io/en/stable/
*/

// TODO: account for all relevant binding functions inherited from pybind11

// binding functions
using pybind11::init;
using pybind11::init_alias;
using pybind11::implicitly_convertible;
using pybind11::args_are_all_keyword_or_ds;
using pybind11::initialize_interpreter;
using pybind11::scoped_interpreter;
// PYBIND11_MODULE                      <- macros don't respect namespaces
// PYBIND11_EMBEDDED_MODULE
// PYBIND11_OVERRIDE
// PYBIND11_OVERRIDE_PURE
// PYBIND11_OVERRIDE_NAME
// PYBIND11_OVERRIDE_PURE_NAME
using pybind11::get_override;
using pybind11::scoped_ostream_redirect;
using pybind11::scoped_estream_redirect;
using pybind11::add_ostream_redirect;


// annotations
using pybind11::overload_cast;
using pybind11::const_;
using pybind11::args;
using pybind11::kwargs;
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


/////////////////////
////    TYPES    ////
/////////////////////


template <typename... Args>
using Class = pybind11::class_<Args...>;
using Handle = pybind11::handle;
using WeakRef = pybind11::weakref;
using Capsule = pybind11::capsule;
using Buffer = pybind11::buffer;  // TODO: delete this and force users to use memoryview instead
using MemoryView = pybind11::memoryview;  // TODO: place in buffer.h along with memoryview
class Object;
class NoneType;
class NotImplementedType;
class EllipsisType;
class Slice;
class Module;
class Bool;
class Int;
class Float;
class Complex;
class Range;
template <typename Val = Object>
class List;
template <typename Val = Object>
class Tuple;
template <typename Key = Object>
class Set;
template <typename Key = Object>
class FrozenSet;
template <typename Key = Object, typename Val = Object>
class Dict;
template <typename Map>
class KeyView;
template <typename Map>
class ValueView;
template <typename Map>
class ItemView;
template <typename Map>
class MappingProxy;
class Str;
class Bytes;
class ByteArray;
class Type;
class Super;
class Code;
class Frame;
class Function;  // TODO: template on return and arguments, with dynamic as CTAD default?
class ClassMethod;  // TODO: template on function type
class StaticMethod;  // TODO: template on function type
class Property;  // NOTE: no need to template because getters/setters/deleters have consistent signatures
class Timedelta;
class Timezone;
class Date;
class Time;
class Datetime;


namespace impl {

    struct InitializerTag {};
    struct ProxyTag {};

    struct TupleTag {
        static const Type type;
    };
    struct ListTag {
        static const Type type;
    };
    struct SetTag {
        static const Type type;
    };
    struct FrozenSetTag {
        static const Type type;
    };
    struct KeyTag {
        static const Type type;
    };
    struct ValueTag {
        static const Type type;
    };
    struct ItemTag {
        static const Type type;
    };
    struct DictTag {
        static const Type type;
    };
    struct MappingProxyTag {
    protected:
        static const Object placeholder;  // TODO: is this necessary?

    public:
        static const Type type;
    };

    // TODO: remove SliceInitializer or make it into a std::variant?
    struct SliceInitializer;

}


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


template <std::derived_from<Object> T>
T reinterpret_borrow(Handle obj);
template <std::derived_from<Object> T>
T reinterpret_steal(Handle obj);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_DECLARATIONS_H
