// include guard: BERTRAND_STRUCTS_UTIL_CONTAINER_H
#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <cstddef>  // size_t
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "iter.h"  // iter()
#include "ops.h"  // repr()


namespace bertrand {
namespace util {


////////////////////////////////
////    BASIC CONTAINERS    ////
////////////////////////////////


/* These are wrappers around the basic data structures exposed by the CPython API.
 * They offer a much simpler/more intuitive interface as well as automatic reference
 * counting.
 */


/* An object-oriented wrapper around the CPython tuple API. */
class PyTuple {
    friend class Element;

    /* Adopt an existing Python tuple. */
    inline static PyObject* adopt(PyObject* tuple) {
        if (!PyTuple_Check(tuple)) throw TypeError("expected a tuple");
        return tuple;
    }

public:
    PyObject* obj;
    Py_ssize_t length;

    /* Construct a PyTuple by packing the specified values. */
    template <
        typename... Args,
        typename std::enable_if_t<std::conjunction_v<std::is_same<Args, PyObject*>...>>
    >
    PyTuple(Args&&... args) : obj(nullptr), length(sizeof...(Args)) {
        obj = PyTuple_Pack(length, std::forward<Args>(args)...);
        if (obj == nullptr) throw catch_python();
    }

    /* Construct an empty Python tuple of the specified size. */
    PyTuple(Py_ssize_t size) : obj(PyTuple_New(size)), length(size) {
        if (obj == nullptr) throw catch_python();
    }

    /* Construct a PyTuple around an existing CPython tuple.  Steals a reference. */
    PyTuple(PyObject* tuple) : obj(adopt(tuple)), length(PyTuple_GET_SIZE(tuple)) {}

    /* Copy constructor. */
    PyTuple(const PyTuple& other) :
        obj(PyTuple_New(other.length)), length(other.length)
    {
        if (obj == nullptr) throw catch_python();
        for (Py_ssize_t i = 0; i < length; ++i) {
            SET_ITEM(i, Py_NewRef(other.GET_ITEM(i)));
        }
    }

    /* Move constructor. */
    PyTuple(PyTuple&& other) : obj(other.obj), length(other.length) {
        other.obj = nullptr;
        other.length = 0;
    }

    /* Copy assignment. */
    PyTuple& operator=(const PyTuple& other) {
        if (this == &other) return *this;

        // create a new tuple
        PyObject* new_tuple = PyTuple_New(other.length);
        if (new_tuple == nullptr) throw catch_python();
        for (Py_ssize_t i = 0; i < other.length; ++i) {
            PyTuple_SET_ITEM(new_tuple, i, Py_NewRef(other.GET_ITEM(i)));
        }

        // release the old tuple and replace
        Py_XDECREF(obj);
        obj = new_tuple;
        length = other.length;
        return *this;
    }

    /* Move assignment. */
    PyTuple& operator=(PyTuple&& other) {
        if (this == &other) return *this;
        Py_XDECREF(obj);
        obj = other.obj;
        length = other.length;
        other.obj = nullptr;
        other.length = 0;
        return *this;
    }

    /* Release the Python tuple on destruction. */
    ~PyTuple() { Py_XDECREF(obj); }

    /* Get the size of the tuple. */
    inline size_t size() const {
        return static_cast<size_t>(length);
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(obj);
    }

    /* Iterate over the tuple. */
    inline auto begin() { return iter(obj).begin(); }
    inline auto begin() const { return iter(obj).begin(); }
    inline auto cbegin() const { return iter(obj).cbegin(); }
    inline auto end() { return iter(obj).end(); }
    inline auto end() const { return iter(obj).end(); }
    inline auto cend() const { return iter(obj).cend(); }
    inline auto rbegin() { return iter(obj).rbegin(); }
    inline auto rbegin() const { return iter(obj).rbegin(); }
    inline auto crbegin() const { return iter(obj).crbegin(); }
    inline auto rend() { return iter(obj).rend(); }
    inline auto rend() const { return iter(obj).rend(); }
    inline auto crend() const { return iter(obj).crend(); }

    /* An assignable proxy for a particular index of the tuple. */
    template <typename T>
    class Element {
        T& tuple;
        Py_ssize_t index;

        friend PyTuple;

        /* Construct an Element proxy for the specified index. */
        Element(T& tuple, size_t index) :
            tuple(tuple), index(static_cast<Py_ssize_t>(index))
        {}

    public:
        Element(const Element&) = delete;
        Element(Element&&) = delete;
        Element& operator=(const Element&) = delete;
        Element& operator=(Element&&) = delete;

        /* Get the item at this index. */
        inline PyObject* get() const {
            if (index >= tuple.length) throw IndexError("index out of range");
            return tuple.GET_ITEM(index);  // borrowed reference
        }

        /* Implicitly convert the proxy to its value during assignment/parameter
        passing. */
        inline operator PyObject*() const {
            return get();
        }

        /* Set the item at this index.  Steals a reference to `value`. */
        inline void set(PyObject* value) {
            if (index >= tuple.length) throw IndexError("index out of range");
            Py_XDECREF(tuple.GET_ITEM(index));
            tuple.SET_ITEM(index, value);
        }

        /* Assign to the proxy to overwrite its value. */
        inline Element& operator=(PyObject* value) {
            set(value);
            return *this;
        }

    };

    /* Index into a mutable Python tuple. */
    inline Element<PyTuple> operator[](size_t index) {
        return {*this, index};
    }

    /* Index into a const Python tuple. */
    inline const Element<const PyTuple> operator[](size_t index) const {
        return {*this, index};
    }

    /* Directly get an item within the tuple without boundschecking or proxying.
    Returns a borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PyTuple_GET_ITEM(obj, index);
    }

    /* Directly set an item within the tuple without boundschecking or proxying.  Steals
    a reference to `value` and does not clear the previous item if one is present. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(obj, index, value);
    }

    /* Get a new PyTuple representing a slice within this tuple. */
    inline PyTuple get_slice(size_t start, size_t stop) const {
        if (start > size()) throw IndexError("start index out of range");
        if (stop > size()) throw IndexError("stop index out of range");
        if (start > stop) throw IndexError("start index greater than stop index");
        return PyTuple(PyTuple_GetSlice(obj, start, stop));
    }

};


/* An object-oriented wrapper around the CPython list API. */
class PyList {
    friend class Element;

    /* adopt an existing Python list. */
    inline static PyObject* adopt(PyObject* list) {
        if (!PyList_Check(list)) throw TypeError("expected a list");
        return list;
    }

public:
    PyObject* obj;
    Py_ssize_t length;

    /* Construct an empty Python list of the specified size. */
    PyList(Py_ssize_t size) : obj(PyList_New(size)), length(size) {
        if (obj == nullptr) throw catch_python();
    }

    /* Construct a PyList around an existing CPython list.  Steals a reference. */
    PyList(PyObject* list) : obj(adopt(list)), length(PyList_GET_SIZE(list)) {}

    /* Copy constructor. */
    PyList(const PyList& other) :
        obj(PyList_New(other.length)), length(other.length)
    {
        if (obj == nullptr) throw catch_python();
        for (Py_ssize_t i = 0; i < length; ++i) {
            SET_ITEM(i, Py_NewRef(other.GET_ITEM(i)));
        }
    }

    /* Move constructor. */
    PyList(PyList&& other) : obj(other.obj), length(other.length) {
        other.obj = nullptr;
        other.length = 0;
    }

    /* Copy assignment. */
    PyList& operator=(const PyList& other) {
        if (this == &other) return *this;

        // create a new list
        PyObject* new_list = PyList_New(other.length);
        if (new_list == nullptr) throw catch_python();
        for (Py_ssize_t i = 0; i < other.length; ++i) {
            PyList_SET_ITEM(new_list, i, Py_NewRef(other.GET_ITEM(i)));
        }

        // release the old list and replace
        Py_XDECREF(obj);
        obj = new_list;
        length = other.length;
        return *this;
    }

    /* Move assignment. */
    PyList& operator=(PyList&& other) {
        if (this == &other) return *this;
        Py_XDECREF(obj);
        obj = other.obj;
        length = other.length;
        other.obj = nullptr;
        other.length = 0;
        return *this;
    }

    /* Release the Python list on destruction. */
    ~PyList() { Py_XDECREF(obj); }

    /* Get the size of the list. */
    inline size_t size() const {
        return static_cast<size_t>(length);
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(obj);
    }

    /* Iterate over the list. */
    inline auto begin() { return iter(obj).begin(); }
    inline auto begin() const { return iter(obj).begin(); }
    inline auto cbegin() const { return iter(obj).cbegin(); }
    inline auto end() { return iter(obj).end(); }
    inline auto end() const { return iter(obj).end(); }
    inline auto cend() const { return iter(obj).cend(); }
    inline auto rbegin() { return iter(obj).rbegin(); }
    inline auto rbegin() const { return iter(obj).rbegin(); }
    inline auto crbegin() const { return iter(obj).crbegin(); }
    inline auto rend() { return iter(obj).rend(); }
    inline auto rend() const { return iter(obj).rend(); }
    inline auto crend() const { return iter(obj).crend(); }

    /* Append an element to a mutable list. */
    inline void append(PyObject* value) {
        if (PyList_Append(obj, value)) throw catch_python();
    }

    /* Insert an element into a mutable list. */
    inline void insert(size_t index, PyObject* value) {
        if (PyList_Insert(obj, index, value)) throw catch_python();
    }

    /* Sort a mutable list. */
    inline void sort() {
        if (PyList_Sort(obj)) throw catch_python();
    }

    /* Reverse a mutable list. */
    inline void reverse() {
        if (PyList_Reverse(obj)) throw catch_python();
    }

    /* Convert the list into an equivalent tuple. */
    inline PyTuple as_tuple() const {
        return PyTuple(PySequence_Tuple(obj));
    }

    /* An assignable proxy for a particular index of the list. */
    template <typename T>
    class Element {
        T& list;
        Py_ssize_t index;

        friend PyList;

        /* Construct an Element proxy for the specified index. */
        Element(T& list, size_t index) :
            list(list), index(static_cast<Py_ssize_t>(index))
        {}

    public:
        Element(const Element&) = delete;
        Element(Element&&) = delete;
        Element& operator=(const Element&) = delete;
        Element& operator=(Element&&) = delete;

        /* Get the item at this index. */
        inline PyObject* get() const {
            if (index >= list.length) throw IndexError("index out of range");
            return list.GET_ITEM(index);  // borrowed reference
        }

        /* Implicitly convert the proxy to its value during assignment/parameter
        passing. */
        inline operator PyObject*() const {
            return get();
        }

        /* Set the item at this index. */
        inline void set(PyObject* value) {
            if (index >= list.length) throw IndexError("index out of range");
            Py_XDECREF(list.GET_ITEM(index));
            list.SET_ITEM(index, Py_NewRef(value));
        }

        /* Assign to the proxy to overwrite its value. */
        inline Element& operator=(PyObject* value) {
            set(value);
            return *this;
        }

    };

    /* Index into a mutable Python list. */
    inline Element<PyList> operator[](size_t index) {
        return {*this, index};
    }

    /* Index into a const Python list. */
    inline const Element<const PyList> operator[](size_t index) const {
        return {*this, index};
    }

    /* Directly get an item within the list without boundschecking or proxying.
    Returns a borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PyList_GET_ITEM(obj, index);
    }

    /* Directly set an item within the list without boundschecking or proxying.  Steals
    a reference to `value` and does not clear the previous item if one is present. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyList_SET_ITEM(obj, index, value);
    }

    /* Get a new PyList representing a slice within this list. */
    inline PyList get_slice(size_t start, size_t stop) const {
        if (start > size()) throw IndexError("start index out of range");
        if (stop > size()) throw IndexError("stop index out of range");
        if (start > stop) throw IndexError("start index greater than stop index");
        return PyList(PyList_GetSlice(obj, start, stop));
    }

    /* Set a slice within a mutable list. */
    inline void set_slice(size_t start, size_t stop, PyObject* value) {
        if (start > size()) throw IndexError("start index out of range");
        if (stop > size()) throw IndexError("stop index out of range");
        if (start > stop) throw IndexError("start index greater than stop index");
        if (PyList_SetSlice(obj, start, stop, value)) throw catch_python();
    }

};


/* An object-oriented wrapper around the CPython set/frozenset API. */
class PySet {

    /* adopt an existing Python set. */
    inline static PyObject* adopt(PyObject* set) {
        if (!PyAnySet_Check(set)) throw TypeError("expected a set");
        return set;
    }

public:
    PyObject* obj;
    Py_ssize_t length;

    /* Construct an empty Python set. */
    PySet() : obj(PySet_New(nullptr)), length(0) {
        if (obj == nullptr) throw catch_python();
    }

    /* Construct a PySet around an existing CPython set.  Steals a reference. */
    PySet(PyObject* set) : obj(adopt(set)), length(PySet_GET_SIZE(set)) {}

    /* Copy constructor. */
    PySet(const PySet& other) : obj(PySet_New(other.obj)), length(other.length) {
        if (obj == nullptr) throw catch_python();
    }

    /* Move constructor. */
    PySet(PySet&& other) : obj(other.obj), length(other.length) {
        other.obj = nullptr;
        other.length = 0;
    }

    /* Copy assignment. */
    PySet& operator=(const PySet& other) {
        if (this == &other) return *this;

        // create a new set
        PyObject* new_set = PySet_New(other.obj);
        if (new_set == nullptr) throw catch_python();

        // release the old set and replace
        Py_XDECREF(obj);
        obj = new_set;
        length = other.length;
        return *this;
    }

    /* Move assignment. */
    PySet& operator=(PySet&& other) {
        if (this == &other) return *this;
        Py_XDECREF(obj);
        obj = other.obj;
        length = other.length;
        other.obj = nullptr;
        other.length = 0;
        return *this;
    }

    /* Release the Python set on destruction. */
    ~PySet() { Py_XDECREF(obj); }

    /* Get the size of the set. */
    inline size_t size() const {
        return static_cast<size_t>(length);
    }

    /* Iterate over the set. */
    inline auto begin() { return iter(obj).begin(); }
    inline auto begin() const { return iter(obj).begin(); }
    inline auto cbegin() const { return iter(obj).cbegin(); }
    inline auto end() { return iter(obj).end(); }
    inline auto end() const { return iter(obj).end(); }
    inline auto cend() const { return iter(obj).cend(); }
    inline auto rbegin() { return iter(obj).rbegin(); }
    inline auto rbegin() const { return iter(obj).rbegin(); }
    inline auto crbegin() const { return iter(obj).crbegin(); }
    inline auto rend() { return iter(obj).rend(); }
    inline auto rend() const { return iter(obj).rend(); }
    inline auto crend() const { return iter(obj).crend(); }

    /* Check if the set contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PySet_Contains(obj, key);
        if (result == -1) throw catch_python();
        return result;
    }

    /* Add an element to a mutable set. */
    inline void add(PyObject* value) {
        if (PySet_Add(obj, value)) throw catch_python();
    }

    /* Remove an element from a mutable set. */
    inline void remove(PyObject* value) {
        int result = PySet_Discard(obj, value);
        if (result == -1) throw catch_python();
        if (result == 0) {
            std::ostringstream msg;
            msg << repr(value);
            throw KeyError(msg.str());
        }
    }

    /* Discard an element from a mutable set. */
    inline void discard(PyObject* value) {
        if (PySet_Discard(obj, value) == -1) throw catch_python();
    }

    /* Pop an item from a mutable set. */
    inline PyObject* pop() {
        PyObject* value = PySet_Pop(obj);
        if (value == nullptr) throw catch_python();
        return value;
    }

    /* Clear a mutable set. */
    inline void clear() {
        if (!PySet_Clear(obj)) throw catch_python();
    }

};


/* An object-oriented wrapper around the CPython dict API. */
class PyDict {

    /* adopt an existing Python dictionary. */
    inline static PyObject* adopt(PyObject* dict) {
        if (!PyDict_Check(dict)) throw TypeError("expected a dict");
        return dict;
    }

public:
    PyObject* obj;
    Py_ssize_t length;

    /* Construct an empty Python dictionary. */
    PyDict() : obj(PyDict_New()), length(0) {
        if (obj == nullptr) throw catch_python();
    }

    /* Construct a PyDict around an existing CPython dictionary.  Steals a reference. */
    PyDict(PyObject* dict) : obj(adopt(dict)), length(PyDict_Size(dict)) {}

    /* Copy constructor. */
    PyDict(const PyDict& other) : obj(PyDict_New()), length(other.length) {
        if (obj == nullptr) throw catch_python();
        update(other.obj);  // copy the contents of the other dict
    }

    /* Move constructor. */
    PyDict(PyDict&& other) : obj(other.obj), length(other.length) {
        other.obj = nullptr;
        other.length = 0;
    }

    /* Copy assignment. */
    PyDict& operator=(const PyDict& other) {
        if (this == &other) return *this;

        // create a new dict
        PyObject* new_dict = PyDict_New();
        if (new_dict == nullptr) throw catch_python();
        update(other.obj);  // copy the contents of the other dict

        // release the old dict and replace
        Py_XDECREF(obj);
        obj = new_dict;
        length = other.length;
        return *this;
    }

    /* Move assignment. */
    PyDict& operator=(PyDict&& other) {
        if (this == &other) return *this;
        Py_XDECREF(obj);
        obj = other.obj;
        length = other.length;
        other.obj = nullptr;
        other.length = 0;
        return *this;
    }

    /* Release the Python dict on destruction. */
    ~PyDict() { Py_XDECREF(obj); }

    /* Get the size of the dict. */
    inline size_t size() const {
        return static_cast<size_t>(length);
    }

    /* A custom iterator over a Python dictionary that yields key-value pairs just
    like std::unordered_map. */
    template <typename T>
    class Iterator {
        T& dict;
        Py_ssize_t pos;  // required by PyDict_Next
        std::pair<PyObject*, PyObject*> curr;

        friend PyDict;

        /* Construct an Iterator over the specified dictionary. */
        Iterator(T& dict) : dict(dict), pos(0) {
            if (!PyDict_Next(dict.obj, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
        }

    public:
        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::pair<PyObject*, PyObject*>;
        using pointer               = value_type*;
        using reference             = value_type&;

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            dict(other.dict), pos(other.pos), curr(other.curr)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            dict(other.dict), pos(other.pos), curr(other.curr)
        {
            other.curr.first = nullptr;
            other.curr.second = nullptr;
        }

        /* Get the current key-value pair. */
        inline std::pair<PyObject*, PyObject*>& operator*() {
            return curr;
        }

        /* Get the current key-value pair for a const dictionary. */
        inline const std::pair<PyObject*, PyObject*>& operator*() const {
            return curr;
        }

        /* Advance to the next item within the dictionary. */
        inline Iterator& operator++() {
            if (!PyDict_Next(dict.obj, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
            return *this;
        }

        /* Terminate the loop. */
        inline bool operator!=(const Iterator& other) const {
            return curr.first != other.curr.first && curr.second != other.curr.second;
        }

    };

    /* Iterate over the dictionary. */
    inline auto begin() { return Iterator<PyDict>(*this); }
    inline auto begin() const { return Iterator<const PyDict>(*this); }
    inline auto cbegin() const { return Iterator<const PyDict>(*this); }
    inline auto end() { return Iterator<PyDict>(*this); }
    inline auto end() const { return Iterator<const PyDict>(*this); }
    inline auto cend() const { return Iterator<const PyDict>(*this); }

    /* Check if the dictionary contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PyDict_Contains(obj, key);
        if (result == -1) throw catch_python();
        return result;
    }

    /* Return a new PyDict containing a copy of this dictionary. */
    inline PyDict copy() const {
        return PyDict(PyDict_Copy(obj));
    }

    /* Clear the dictionary. */
    inline void clear() {
        PyDict_Clear(obj);
        length = 0;
    }

    /* Get the value associated with a key or set it to the default value if it is not
    already present. */
    inline PyObject* setdefault(PyObject* key, PyObject* default_value) {
        return PyDict_SetDefault(obj, key, default_value);
    }

    /* Update this dictionary with another Python mapping, overriding the current
    values on collision. */
    inline void update(PyObject* other) {
        if (PyDict_Merge(obj, other, 1)) throw catch_python();
    }

    /* Update this dictionary with another Python container that is known to contain
    key-value pairs of length 2, overriding the current values on collision. */
    inline void update_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(obj, other, 1)) throw catch_python();
    }

    /* Update this dictionary with another Python mapping, keeping the current values
    on collision. */
    inline void merge(PyObject* other) {
        if (PyDict_Merge(obj, other, 0)) throw catch_python();
    }

    /* Update this dictionary with another Python container that is known to contain
    key-value pairs of length 2, keeping the current values on collision. */
    inline void merge_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(obj, other, 0)) throw catch_python();
    }

    /* An assignable proxy for a particular key within the dictionary. */
    template <typename T, typename U>
    class Element {
        T& dict;
        U key;

        friend PyDict;

        /* Construct an Element proxy for the specified key. */
        Element(T& dict, U& key) : dict(dict), key(key) {}

    public:
        Element(const Element&) = delete;
        Element(Element&&) = delete;
        Element& operator=(const Element&) = delete;
        Element& operator=(Element&&) = delete;

        /* Get the value associated with the given key.  Returns nullptr if the key is
        not in the dictionary. */
        inline PyObject* get() const {
            PyObject* value;
            if constexpr (std::is_same_v<U, const char *>) {
                value = PyDict_GetItemString(dict.obj, key);
            } else {
                value = PyDict_GetItem(dict.obj, key);
            }
            return value;
        }

        /* Implicitly convert the proxy to its value during assignment/parameter
        passing. */
        inline operator PyObject*() const {
            return get();
        }

        /* Set the value associated with the given key.  Steals a reference to `value`
        and does not clear the previous value if one is present. */
        inline void set(PyObject* value) {
            if constexpr (std::is_same_v<U, const char *>) {
                if (PyDict_SetItemString(dict.obj, key, value)) throw catch_python();
            } else {
                if (PyDict_SetItem(dict.obj, key, value)) throw catch_python();
            }
        }

        /* Assign to the proxy to overwrite its value. */
        inline Element& operator=(PyObject* value) {
            set(value);
            return *this;
        }

        /* Delete the key from the dictionary. */
        inline void del() {
            if constexpr (std::is_same_v<U, const char *>) {
                if (PyDict_DelItemString(dict.obj, key)) throw catch_python();
            } else {
                if (PyDict_DelItem(dict.obj, key)) throw catch_python();
            }
        }

    };

    /* Get a proxy for a PyObject* key within the dictionary*. */
    inline Element<PyDict, PyObject*> operator[](PyObject* key) {
        return {*this, key};
    }

    /* Get a const proxy for a PyObject* key within a const dictionary. */
    inline auto operator[](const PyObject* key) const
        -> const Element<const PyDict, const PyObject*>
    {
        return {*this, key};
    }

    /* Get a proxy for a const char* key within the dictionary*. */
    inline Element<PyDict, const char*> operator[](const char* key) {
        return {*this, key};
    }

    /* Get a const proxy for a const char* key within a const dictionary. */
    inline const Element<const PyDict, const char*> operator[](const char* key) const {
        return {*this, key};
    }

};


/////////////////////////////
////    FAST SEQUENCE    ////
/////////////////////////////


/* Sometimes we need to unpack an iterable into a fast sequence so that it has a
 * definite size and supports random access.  The sequence() helper does that for both
 * Python iterables (using PySequence_Fast) and C++ iterables (using std::vector).
 */


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
class PyFastSequence {

    /* Unpack a Python iterable into a fast sequence. */
    inline PyObject* unpack(PyObject* iterable) {
        // don't unpack if iterable is already a tuple or list
        if (PyTuple_Check(iterable) || PyList_Check(iterable)) {
            return Py_NewRef(iterable);
        }

        // unpack into a fast sequence
        PyObject* seq = PySequence_Fast(iterable, "could not unpack Python iterable");
        if (seq == nullptr) throw catch_python();
        return seq;
    }

public:
    PyObject* obj;
    Py_ssize_t length;

    /* Construct a PySequence from an iterable or other sequence. */
    PyFastSequence(PyObject* items) :
        obj(unpack(items)), length(PySequence_Fast_GET_SIZE(obj))
    {}

    /* Copy constructor. */
    PyFastSequence(const PyFastSequence& other) :
        obj(unpack(other.obj)), length(PySequence_Fast_GET_SIZE(obj))
    {}

    /* Move constructor. */
    PyFastSequence(PyFastSequence&& other) : obj(other.obj), length(other.length) {
        other.obj = nullptr;
        other.length = 0;
    }

    /* Copy assignment. */
    PyFastSequence& operator=(const PyFastSequence& other) {
        if (this == &other) return *this;
        PyObject* seq = unpack(other.obj);
        Py_XDECREF(obj);
        obj = seq;
        length = PySequence_Fast_GET_SIZE(obj);
        return *this;
    }

    /* Move assignment. */
    PyFastSequence& operator=(PyFastSequence&& other) {
        if (this == &other) return *this;
        Py_XDECREF(obj);
        obj = other.obj;
        length = other.length;
        other.obj = nullptr;
        other.length = 0;
        return *this;
    }

    /* Release the Python sequence on destruction. */
    ~PyFastSequence() { Py_XDECREF(obj); }

    /* Get the size of the sequence. */
    inline size_t size() const {
        return static_cast<size_t>(length);
    }

    /* Iterate over the sequence. */
    inline auto begin() const { return iter(obj).begin(); }
    inline auto cbegin() const { return iter(obj).cbegin(); }
    inline auto end() const { return iter(obj).end(); }
    inline auto cend() const { return iter(obj).cend(); }
    inline auto rbegin() const { return iter(obj).rbegin(); }
    inline auto crbegin() const { return iter(obj).crbegin(); }
    inline auto rend() const { return iter(obj).rend(); }
    inline auto crend() const { return iter(obj).crend(); }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(obj);
    }

    /* Get the value at a particular index of the sequence.  Returns a borrowed
    reference. */
    inline PyObject* operator[](size_t index) const {
        if (index >= size()) throw IndexError("index out of range");
        return GET_ITEM(index);
    }

    /* Directly get an item within the sequence without boundschecking.  Returns a
    borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PySequence_Fast_GET_ITEM(obj, index);
    }

};


/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
inline auto sequence(Iterable&& iterable) {
    if constexpr (is_pyobject<Iterable>) {
        return PyFastSequence(std::forward<Iterable>(iterable));
    } else {
        using Traits = ContainerTraits<Iterable>;
        static_assert(Traits::forward_iterable, "container must be forward iterable");

        // if the container already supports random access, then return it directly
        if constexpr (Traits::has_size && Traits::indexable) {
            return std::forward<Iterable>(iterable);
        } else {
            auto iter = iter(iterable);
            auto it = iter.begin();
            auto end = iter.end();
            using Deref = decltype(*std::declval<decltype(it)>());
            return std::vector<Deref>(it, end);
        }
    }
}


}  // namespace util


/* Export to base namespace. */
using util::PyTuple;
using util::PyList;
using util::PyDict;
using util::PyFastSequence;
using util::sequence;


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_CONTAINER_H
