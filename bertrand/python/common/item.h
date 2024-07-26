#ifndef BERTRAND_PYTHON_COMMON_ITEM_H
#define BERTRAND_PYTHON_COMMON_ITEM_H

#include "declarations.h"
#include "except.h"
#include "object.h"
#include "ops.h"
#include "control.h"


namespace py {
namespace impl {


// TODO: rather than .value(), use a __declspec(property) to make it more consistent
// with other proxies across the codebase, like Args


/* A generic policy for getting, setting, or deleting an item at a particular
index of a Python container. */
template <typename Obj, typename Key>
struct ItemPolicy {
    Handle obj;
    Object key;

    ItemPolicy(Handle obj, const Key& key) : obj(obj), key(key) {}
    ItemPolicy(Handle obj, Key&& key) : obj(obj), key(std::move(key)) {}
    ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
    ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(std::move(other.key)) {}

    PyObject* get() const {
        PyObject* result = PyObject_GetItem(ptr(obj), ptr(key));
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

    void set(PyObject* value) {
        int result = PyObject_SetItem(ptr(obj), ptr(key), value);
        if (result < 0) {
            Exception::from_python();
        }
    }

    void del() {
        int result = PyObject_DelItem(ptr(obj), ptr(key));
        if (result < 0) {
            Exception::from_python();
        }
    }

};


/* A specialization of ItemPolicy that is specifically optimized for integer
indices into Python tuple objects. */
template <std::derived_from<TupleTag> Obj, std::integral Key>
struct ItemPolicy<Obj, Key> {
    Handle obj;
    Py_ssize_t key;

    ItemPolicy(Handle obj, Py_ssize_t key) : obj(obj), key(key) {}
    ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
    ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(other.key) {}

    PyObject* get() const {
        Py_ssize_t size = PyTuple_GET_SIZE(ptr(obj));
        Py_ssize_t norm = key + size * (key < 0);
        if (norm < 0 || norm >= size) {
            throw IndexError("tuple index out of range");
        }
        PyObject* result = PyTuple_GET_ITEM(ptr(obj), norm);
        if (result == nullptr) {
            throw ValueError(
                "item at index " + std::to_string(key) +
                " was found to be null"
            );
        }
        return Py_NewRef(result);
    }

};


/* A specialization of ItemPolicy that is specifically optimized for integer
indices into Python list objects. */
template <std::derived_from<ListTag> Obj, std::integral Key>
struct ItemPolicy<Obj, Key> {
    Handle obj;
    Py_ssize_t key;

    ItemPolicy(Handle obj, Py_ssize_t key) : obj(obj), key(key) {}
    ItemPolicy(const ItemPolicy& other) : obj(other.obj), key(other.key) {}
    ItemPolicy(ItemPolicy&& other) : obj(other.obj), key(other.key) {}

    Py_ssize_t normalize(Py_ssize_t index) const {
        Py_ssize_t size = PyList_GET_SIZE(ptr(obj));
        Py_ssize_t result = index + size * (index < 0);
        if (result < 0 || result >= size) {
            throw IndexError("list index out of range");
        }
        return result;
    }

    PyObject* get() const {
        PyObject* result = PyList_GET_ITEM(ptr(obj), normalize(key));
        if (result == nullptr) {
            throw ValueError(
                "item at index " + std::to_string(key) +
                " was found to be null"
            );
        }
        return Py_NewRef(result);
    }

    void set(PyObject* value) {
        Py_ssize_t normalized = normalize(key);
        PyObject* previous = PyList_GET_ITEM(ptr(obj), normalized);
        PyList_SET_ITEM(ptr(obj), normalized, Py_NewRef(value));
        Py_XDECREF(previous);
    }

    void del() {
        PyObject* index_obj = PyLong_FromSsize_t(normalize(key));
        if (PyObject_DelItem(ptr(obj), index_obj) < 0) {
            Exception::from_python();
        }
        Py_DECREF(index_obj);
    }

};


/* Describes the result of the array index (`[]`) operator on a python object.  This
uses the __getitem__, __setitem__, and __delitem__ control structs to selectively
enable/disable these operations for particular types, and to assign a corresponding
return type to which the proxy can be converted. */
template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
class Item : public ProxyTag {
public:
    using type = typename __getitem__<Obj, Key>::type;
    static_assert(
        std::derived_from<type, Object>,
        "index operator must return a subclass of py::Object.  Check your "
        "specialization of __getitem__ for these types and ensure the Return "
        "type is set to a subclass of py::Object."
    );

private:
    alignas (type) mutable unsigned char buffer[sizeof(type)];
    mutable bool initialized;
    ItemPolicy<Obj, Key> policy;

public:

    template <typename... Args>
    explicit Item(Args&&... args) : policy(std::forward<Args>(args)...) {}

    Item(const Item& other) : initialized(other.initialized), policy(other.policy) {
        if (initialized) {
            new (buffer) type(reinterpret_cast<type&>(other.buffer));
        }
    }

    Item(Item&& other) : initialized(other.initialized), policy(std::move(other.policy)) {
        if (initialized) {
            other.initialized = false;
            new (buffer) type(std::move(reinterpret_cast<type&>(other.buffer)));
        }
    }

    ~Item() {
        if (initialized) {
            reinterpret_cast<type&>(buffer).~type();
        }
    }

    [[nodiscard]] bool has_value() const {
        return initialized;
    }

    [[nodiscard]] type& value() {
        if (!initialized) {
            new (buffer) type(reinterpret_steal<type>(policy.get()));
            initialized = true;
        }
        return reinterpret_cast<type&>(buffer);
    }

    [[nodiscard]] const type& value() const {
        if (!initialized) {
            new (buffer) type(reinterpret_steal<type>(policy.get()));
            initialized = true;
        }
        return reinterpret_cast<type&>(buffer);
    }

    template <typename T> requires (__setitem__<Obj, Key, std::remove_cvref_t<T>>::enable)
    Item& operator=(T&& value) {
        using Return = typename __setitem__<Obj, Key, std::remove_cvref_t<T>>::type;
        static_assert(
            std::is_void_v<Return>,
            "index assignment operator must return void.  Check your "
            "specialization of __setitem__ for these types and ensure the Return "
            "type is set to void."
        );
        if constexpr (proxy_like<T>) {
            *this = value.value();
        } else {
            new (buffer) type(std::forward<T>(value));
            initialized = true;
            policy.set(ptr(reinterpret_cast<type&>(buffer)));
        }
        return *this;
    }

    template <typename T = Obj> requires (__delitem__<T, Key>::enable)
    void del() {
        using Return = typename __delitem__<T, Key>::type;
        static_assert(
            std::is_void_v<Return>,
            "index deletion operator must return void.  Check your specialization "
            "of __delitem__ for these types and ensure the Return type is set to "
            "void."
        );
        policy.del();
        if (initialized) {
            reinterpret_cast<type&>(buffer).~type();
            initialized = false;
        }
    }

    [[nodiscard]] explicit operator bool() const {
        return static_cast<bool>(value());
    }

    [[nodiscard]] operator type&() {
        return value();
    }

    [[nodiscard]] operator const type&() const {
        return value();
    }

    template <typename T>
        requires (!std::same_as<type, T> && std::convertible_to<type, T>)
    [[nodiscard]] operator T() const {
        return implicit_cast<T>(value());
    }

    template <typename T> requires (!std::convertible_to<type, T>)
    [[nodiscard]] explicit operator T() const {
        return static_cast<T>(value());
    }

    [[nodiscard]] type* operator->() {
        return &value();
    };

    [[nodiscard]] const type* operator->() const {
        return &value();
    };

    [[nodiscard]] PyObject* ptr() const {
        return ptr(value());
    }

    [[nodiscard]] Handle release() {
        Handle result = value().release();
        reinterpret_cast<type&>(buffer).~type();
        initialized = false;
        return result;
    }

    [[nodiscard]] bool is(Handle other) const {
        return value().is(other);
    }

    template <typename T> requires (__contains__<type, T>::enable)
    [[nodiscard]] auto contains(const T& key) const {
        return value().contains(key);
    }

    template <typename... Args>
    auto operator()(Args&&... args) const {
        return value()(std::forward<Args>(args)...);
    }

    template <typename T>
    auto operator[](T&& key) const {
        return value()[std::forward<T>(key)];
    }

    template <typename T = type> requires (__getitem__<T, Slice>::enable)
    auto operator[](const std::initializer_list<impl::SliceInitializer>& slice) const {
        return value()[slice];
    }

    template <typename T = type> requires (__iter__<T>::enable)
    [[nodiscard]] auto operator*() const {
        return *value();
    }

};


}  // namespace impl
}  // namespace py


#endif
