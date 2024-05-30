#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_PROXIES_H
#define BERTRAND_PYTHON_COMMON_PROXIES_H

#include "declarations.h"
#include "except.h"
#include "object.h"
#include "ops.h"
#include "control.h"


// TODO: something about the way these are handled calls the copy assignment operator
// somewhere along the way, which is not optimal.


namespace bertrand {
namespace py {
namespace impl {


/* Base class for all accessor proxies.  Stores an arbitrary object in a buffer and
forwards its interface using pointer semantics. */
template <typename Obj, typename Derived>
class Proxy : public ProxyTag {
public:
    using type = Obj;

protected:
    alignas (type) mutable unsigned char buffer[sizeof(type)];
    mutable bool initialized;

private:

    type& get_value() {
        return static_cast<Derived&>(*this).value();
    }

    const type& get_value() const {
        return static_cast<const Derived&>(*this).value();
    }

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Creates an empty proxy */
    Proxy() : initialized(false) {}

    /* Forwarding copy constructor for wrapped object. */
    Proxy(const type& other) : initialized(true) {
        new (buffer) type(other);
    }

    /* Forwarding move constructor for wrapped object. */
    Proxy(type&& other) : initialized(true) {
        new (buffer) type(std::move(other));
    }

    /* Copy constructor for proxy. */
    Proxy(const Proxy& other) : initialized(other.initialized) {
        if (initialized) {
            new (buffer) type(reinterpret_cast<type&>(other.buffer));
        }
    }

    /* Move constructor for proxy. */
    Proxy(Proxy&& other) : initialized(other.initialized) {
        if (initialized) {
            other.initialized = false;
            new (buffer) type(std::move(reinterpret_cast<type&>(other.buffer)));
        }
    }

    /* Forwarding copy assignment for wrapped object. */
    Proxy& operator=(const type& other) {
        if (initialized) {
            reinterpret_cast<type&>(buffer) = other;
        } else {
            new (buffer) type(other);
            initialized = true;
        }
        return *this;
    }

    /* Forwarding move assignment for wrapped object. */
    Proxy& operator=(type&& other) {
        if (initialized) {
            reinterpret_cast<type&>(buffer) = std::move(other);
        } else {
            new (buffer) type(std::move(other));
            initialized = true;
        }
        return *this;
    }

    /* Copy assignment operator. */
    Proxy& operator=(const Proxy& other) {
        if (&other != this) {
            if (initialized) {
                initialized = false;
                reinterpret_cast<type&>(buffer).~type();
            }
            if (other.initialized) {
                new (buffer) type(reinterpret_cast<type&>(other.buffer));
                initialized = true;
            }
        }
        return *this;
    }

    /* Move assignment operator. */
    Proxy& operator=(Proxy&& other) {
        if (&other != this) {
            if (initialized) {
                initialized = false;
                reinterpret_cast<type&>(buffer).~type();
            }
            if (other.initialized) {
                other.initialized = false;
                new (buffer) type(
                    std::move(reinterpret_cast<type&>(other.buffer))
                );
                initialized = true;
            }
        }
        return *this;
    }

    /* Destructor.  Can be avoided by manually clearing the initialized flag. */
    ~Proxy() {
        if (initialized) {
            reinterpret_cast<type&>(buffer).~type();
        }
    }

    ///////////////////////////
    ////    DEREFERENCE    ////
    ///////////////////////////

    [[nodiscard]] bool has_value() const {
        return initialized;
    }

    [[nodiscard]] type& value() {
        if (!initialized) {
            throw ValueError(
                "attempt to dereference an uninitialized accessor.  Either the "
                "accessor was moved from or not properly constructed to begin with."
            );
        }
        return reinterpret_cast<type&>(buffer);
    }

    [[nodiscard]] const type& value() const {
        if (!initialized) {
            throw ValueError(
                "attempt to dereference an uninitialized accessor.  Either the "
                "accessor was moved from or not properly constructed to begin with."
            );
        }
        return reinterpret_cast<const type&>(buffer);
    }

    ////////////////////////////////////
    ////    FORWARDING INTERFACE    ////
    ////////////////////////////////////

    [[nodiscard]] auto operator*() {
        return *get_value();
    }

    // all attributes of wrapped type are forwarded using the arrow operator.  Just
    // replace all instances of `.` with `->`
    [[nodiscard]] type* operator->() {
        return &get_value();
    };

    [[nodiscard]] const type* operator->() const {
        return &get_value();
    };

    // TODO: make sure that converting proxy to wrapped object does not create a copy.
    // -> This matters when modifying kwonly defaults in func.h

    [[nodiscard]] operator type&() {
        return get_value();
    }

    [[nodiscard]] operator const type&() const {
        return get_value();
    }

    template <typename T>
        requires (!std::same_as<type, T> && std::convertible_to<type, T>)
    [[nodiscard]] operator T() const {
        return implicit_cast<T>(get_value());
    }

    template <typename T> requires (!std::convertible_to<type, T>)
    [[nodiscard]] explicit operator T() const {
        return static_cast<T>(get_value());
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    template <typename... Args>
    auto operator()(Args&&... args) const {
        return get_value()(std::forward<Args>(args)...);
    }

    template <typename T>
    auto operator[](T&& key) const {
        return get_value()[std::forward<T>(key)];
    }

    template <typename T = type> requires (__getitem__<T, Slice>::enable)
    auto operator[](const std::initializer_list<impl::SliceInitializer>& slice) const;

    template <typename T>
    [[nodiscard]] auto contains(const T& key) const { return get_value().contains(key); }
    [[nodiscard]] auto size() const { return get_value().size(); }
    [[nodiscard]] auto begin() const { return get_value().begin(); }
    [[nodiscard]] auto end() const { return get_value().end(); }
    [[nodiscard]] auto rbegin() const { return get_value().rbegin(); }
    [[nodiscard]] auto rend() const { return get_value().rend(); }

};


/* A subclass of Proxy that replaces the result of pybind11's `.attr()` method.
These attributes accept the attribute name as a compile-time template parameter,
allowing them to enforce strict type safety through the __getattr__, __setattr__,
and __delattr__ control structs.  If no specialization of these control structs
exist for a given attribute name, then attempting to access it will result in a
compile-time error. */
template <typename Obj, StaticStr name> requires (__getattr__<Obj, name>::enable)
class Attr : public Proxy<typename __getattr__<Obj, name>::type, Attr<Obj, name>> {
public:
    using type = typename __getattr__<Obj, name>::type;
    static_assert(
        std::derived_from<type, Object>,
        "Attribute accessor must return a py::Object subclass.  Check your "
        "specialization of __getattr__ for this type and ensure the Return type is "
        "set to a subclass of py::Object."
    );

private:
    using Base = Proxy<type, Attr>;
    Object obj;

    void get_attr() const {
        if (obj.ptr() == nullptr) {
            throw ValueError(
                "attempt to dereference an uninitialized accessor.  Either the "
                "accessor was moved from or not properly constructed to begin with."
            );
        }
        PyObject* result = PyObject_GetAttr(obj.ptr(), TemplateString<name>::ptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        new (Base::buffer) type(reinterpret_steal<type>(result));
        Base::initialized = true;
    }

public:

    Attr() {}  // TODO: compiler explodes if this is not defined

    explicit Attr(const Object& obj) : obj(obj) {}
    Attr(const Attr& other) : Base(other), obj(other.obj) {}
    Attr(Attr&& other) : Base(std::move(other)), obj(std::move(other.obj)) {}

    /* pybind11's attribute accessors only perform the lookup when the accessor is
     * converted to a value, which we hook to provide string type safety.  In this
     * case, the accessor is treated like a generic object, and will forward all
     * conversions to py::Object.  This allows us to write code like this:
     *
     *      py::Object obj = ...;
     *      py::Int i = obj.attr<"some_int">();  // runtime type check
     *
     * But not like this:
     *
     *      py::Str s = obj.attr<"some_int">();  // runtime error, some_int is not a string
     *
     * Unfortunately, it is not possible to promote these errors to compile time,
     * since Python attributes are inherently dynamic and can't be known in
     * advance.  This is the best we can do without creating a custom type and
     * strictly enforcing attribute types at the C++ level.  If this cannot be
     * done, then the only way to avoid extra runtime overhead is to use
     * reinterpret_steal to bypass the type check, which can be dangerous.
     *
     *      py::Int i = reinterpret_steal<py::Int>(obj.attr<"some_int">().release());
     */

    [[nodiscard]] type& value() {
        if (!Base::initialized) {
            get_attr();
        }
        return reinterpret_cast<type&>(Base::buffer);
    }

    [[nodiscard]] const type& value() const {
        if (!Base::initialized) {
            get_attr();
        }
        return reinterpret_cast<type&>(Base::buffer);
    }

    /* Similarly, assigning to a pybind11 wrapper corresponds to a Python
     * __setattr__ call.  Due to the same restrictions as above, we can't enforce
     * strict typing here, but we can at least make the syntax more consistent and
     * intuitive in mixed Python/C++ code.
     *
     *      py::Object obj = ...;
     *      obj.attr<"some_int">() = 5;  // valid: translates to Python.
     */

    template <typename T> requires (__setattr__<Obj, name, std::remove_cvref_t<T>>::enable)
    Attr& operator=(T&& value) {
        using Return = typename __setattr__<Obj, name, std::remove_cvref_t<T>>::type;
        static_assert(
            std::is_void_v<Return>,
            "attribute assignment operator must return void.  Check your "
            "specialization of __setattr__ for these types and ensure the Return "
            "type is set to void."
        );
        if constexpr (proxy_like<T>) {
            *this = value.value();
        } else {
            new (Base::buffer) type(std::forward<T>(value));
            Base::initialized = true;
            PyObject* value_ptr = reinterpret_cast<type&>(Base::buffer).ptr();
            if (PyObject_SetAttr(obj.ptr(), TemplateString<name>::ptr, value_ptr)) {
                Exception::from_python();
            }
        }
        return *this;
    }

    /* C++'s delete operator does not directly correspond to Python's `del`
     * statement, so we can't piggyback off it here.  Instead, we offer a separate
     * `.del()` method that behaves the same way.
     *
     *      py::Object obj = ...;
     *      obj.attr<"some_int">().del();  // Equivalent to Python `del obj.some_int`
     */

    template <typename T = Obj> requires (__delattr__<T, name>::enable) 
    void del() {
        using Return = typename __delattr__<T, name>::type;
        static_assert(
            std::is_void_v<Return>,
            "attribute deletion operator must return void.  Check your "
            "specialization of __delattr__ for these types and ensure the Return "
            "type is set to void."
        );
        if (PyObject_DelAttr(obj.ptr(), TemplateString<name>::ptr)) {
            Exception::from_python();
        }
        if (Base::initialized) {
            reinterpret_cast<type&>(Base::buffer).~type();
            Base::initialized = false;
        }
    }

};


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
        PyObject* result = PyObject_GetItem(obj.ptr(), key.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

    void set(PyObject* value) {
        int result = PyObject_SetItem(obj.ptr(), key.ptr(), value);
        if (result < 0) {
            Exception::from_python();
        }
    }

    void del() {
        int result = PyObject_DelItem(obj.ptr(), key.ptr());
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
        Py_ssize_t size = PyTuple_GET_SIZE(obj.ptr());
        Py_ssize_t norm = key + size * (key < 0);
        if (norm < 0 || norm >= size) {
            throw IndexError("tuple index out of range");
        }
        PyObject* result = PyTuple_GET_ITEM(obj.ptr(), norm);
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
        Py_ssize_t size = PyList_GET_SIZE(obj.ptr());
        Py_ssize_t result = index + size * (index < 0);
        if (result < 0 || result >= size) {
            throw IndexError("list index out of range");
        }
        return result;
    }

    PyObject* get() const {
        PyObject* result = PyList_GET_ITEM(obj.ptr(), normalize(key));
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
        PyObject* previous = PyList_GET_ITEM(obj.ptr(), normalized);
        PyList_SET_ITEM(obj.ptr(), normalized, Py_NewRef(value));
        Py_XDECREF(previous);
    }

    void del() {
        PyObject* index_obj = PyLong_FromSsize_t(normalize(key));
        if (PyObject_DelItem(obj.ptr(), index_obj) < 0) {
            Exception::from_python();
        }
        Py_DECREF(index_obj);
    }

};


/* A subclass of Proxy that replaces the result of pybind11's array index (`[]`)
operator.  This uses the __getitem__, __setitem__, and __delitem__ control structs
to selectively enable/disable these operations for particular types, and to assign
a corresponding return type to which the proxy can be converted. */
template <typename Obj, typename Key> requires (__getitem__<Obj, Key>::enable)
class Item : public Proxy<typename __getitem__<Obj, Key>::type, Item<Obj, Key>> {
public:
    using type = typename __getitem__<Obj, Key>::type;
    static_assert(
        std::derived_from<type, Object>,
        "index operator must return a subclass of py::Object.  Check your "
        "specialization of __getitem__ for these types and ensure the Return "
        "type is set to a subclass of py::Object."
    );

private:
    using Base = Proxy<type, Item>;
    ItemPolicy<Obj, Key> policy;

public:

    template <typename... Args>
    explicit Item(Args&&... args) : policy(std::forward<Args>(args)...) {}
    Item(const Item& other) : Base(other), policy(other.policy) {}
    Item(Item&& other) : Base(std::move(other)), policy(std::move(other.policy)) {}

    /* pybind11's item accessors only perform the lookup when the accessor is
     * converted to a value, which we can hook to provide strong type safety.  In
     * this case, the accessor is only convertible to the return type specified by
     * __getitem__, and forwards all other conversions to that type specifically.
     * This allows us to write code like this:
     *
     *      template <>
     *      struct impl::__getitem__<List, Slice> : impl::Returns<List> {};
     *
     *      py::List list = {1, 2, 3, 4};
     *      py::List slice = list[{1, 3}];
     *
     *      void foo(const std::vector<int>& vec) {}
     *      foo(list[{1, 3}]);  // List is implicitly convertible to vector
     *
     * But not like this:
     *
     *      py::Int item = list[{1, 3}];  // compile error, List is not convertible to Int
     */

    [[nodiscard]] type& value() {
        if (!Base::initialized) {
            new (Base::buffer) type(reinterpret_steal<type>(policy.get()));
            Base::initialized = true;
        }
        return reinterpret_cast<type&>(Base::buffer);
    }

    [[nodiscard]] const type& value() const {
        if (!Base::initialized) {
            new (Base::buffer) type(reinterpret_steal<type>(policy.get()));
            Base::initialized = true;
        }
        return reinterpret_cast<type&>(Base::buffer);
    }

    /* Similarly, assigning to a pybind11 wrapper corresponds to a Python
     * __setitem__ call, and we can carry strong type safety here as well.  By
     * specializing __setitem__ for the accessor's key type, we can constrain the
     * types that can be assigned to the container, allowing us to enforce
     * compile-time type safety.  We can thus write code like this:
     *
     *      template <impl::list_like Value>
     *      struct impl::__setitem__<List, Slice, Value> : impl::Returns<void> {};
     *
     *      py::List list = {1, 2, 3, 4};
     *      list[{1, 3}] = py::List{5, 6};
     *      list[{1, 3}] = std::vector<int>{7, 8};
     *
     * But not like this:
     *
     *      list[{1, 3}] = 5;  // compile error, int is not list-like
     */

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
            new (Base::buffer) type(std::forward<T>(value));
            Base::initialized = true;
            policy.set(reinterpret_cast<type&>(Base::buffer).ptr());
        }
        return *this;
    }

    /* C++'s delete operator does not directly correspond to Python's `del`
     * statement, so we can't piggyback off it here.  Instead, we offer a separate
     * `.del()` method that behaves the same way and is only enabled if the
     * __delitem__ struct is specialized for the accessor's key type.
     *
     *      template <impl::int_like T>
     *      struct impl::__delitem__<List, T> : impl::Returns<void> {};
     *      template <>
     *      struct impl::__delitem__<List, Slice> : impl::Returns<void> {};
     *
     *      py::List list = {1, 2, 3, 4};
     *      list1[0].del();  // valid, single items can be deleted
     *      list[{0, 2}].del();  // valid, slices can be deleted
     *
     * If __delitem__ is not specialized for a given key type, the `.del()` method
     * will result in a compile error, giving full control over the types that can
     * be deleted from the container.
     */

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
        if (Base::initialized) {
            reinterpret_cast<type&>(Base::buffer).~type();
            Base::initialized = false;
        }
    }

};


}  // namespace impl
}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_PROXIES_H
