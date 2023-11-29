// include guard: BERTRAND_STRUCTS_LINKED_DICT_H
#ifndef BERTRAND_STRUCTS_LINKED_DICT_H
#define BERTRAND_STRUCTS_LINKED_DICT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "core/view.h"  // DictView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface
#include "set.h"  // PySetInterface

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
// #include "algorithms/relative.h"
#include "algorithms/remove.h"
#include "algorithms/repr.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/set_compare.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"
#include "algorithms/swap.h"
#include "algorithms/union.h"
#include "algorithms/update.h"


namespace bertrand {
namespace structs {
namespace linked {


// TODO: final template config should follow std::unordered_map:
// LinkedDict<Key, Value, Flags = Config::DEFAULT, LockType = BasicLock>



/* A n ordered dictionary based on a combined linked list and hash table. */
template <typename NodeType, typename LockPolicy = util::BasicLock>
class LinkedDict : public LinkedBase<linked::DictView<NodeType>, LockPolicy> {
    using Base = LinkedBase<linked::DictView<NodeType>, LockPolicy>;

public:
    using View = typename Base::View;
    using Node = typename Base::Node;
    using Value = typename Base::Value;
    using MappedValue = typename View::MappedValue;

    template <linked::Direction dir>
    using Iterator = typename Base::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename Base::template ConstIterator<dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // inherit constructors from LinkedBase
    using Base::Base;
    using Base::operator=;

    //////////////////////////////
    ////    DICT INTERFACE    ////
    //////////////////////////////

    // TODO: fromkeys()

    /* Add an item to the end of the dictionary if it is not already present. */
    template <typename Pair>
    inline void add(const Pair& item) {
        linked::add(this->view, item);
    }

    /* Add a key-value pair to the end of the dictionary if it is not already
    present. */
    inline void add(const Value& key, const MappedValue& value) {
        linked::add(this->view, key, value);
    }

    /* Add an item to the beginning of the dictionary if it is not already present. */
    template <typename Pair>
    inline void add_left(const Pair& item) {
        linked::add_left(this->view, item);
    }

    /* Add a key-value pair to the beginning of the dictionary if it is not already
    present. */
    inline void add_left(const Value& key, const MappedValue& value) {
        linked::add_left(this->view, key, value);
    }

    /* Add an item to the front of the dictionary, evicting the last item if
    necessary and moving items that are already present. */
    template <typename Pair>
    inline void lru_add(const Pair& item) {
        linked::lru_add(this->view, item);
    }

    /* Add a key-value pair to the front of the dictionary, evicting the last item if
    necessary and moving items that are already present. */
    inline void lru_add(const Value& key, const MappedValue& value) {
        linked::lru_add(this->view, key, value);
    }

    /* Insert an item at a specific index of the dictionary. */
    template <typename Pair>
    inline void insert(long long index, const Pair& item) {
        linked::insert(this->view, index, item);
    }

    /* Insert a key-value pair at a specific index of the dictionary. */
    inline void insert(long long index, const Value& key, const MappedValue& value) {
        linked::insert(this->view, index, key, value);
    }

    /* Extend a dictionary by adding elements from one or more iterables that are not
    already present. */
    template <typename... Containers>
    inline void update(Containers&&... items) {
        (linked::update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a dictionary by left-adding elements from one or more iterables that are
    not already present. */
    template <typename... Containers>
    inline void update_left(Containers&&... items) {
        (linked::update_left(this->view, std::forward<Containers>(items)), ...);
    }

    /* Extend a dictionary by adding or moving items to the head of the dictionary and
    possibly evicting the tail to make room. */
    template <typename... Containers>
    inline void lru_update(Containers&&... items) {
        (linked::lru_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        (linked::difference_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        (linked::intersection_update(this->view, std::forward<Containers>(items)), ...);
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(Container&& items) {
        linked::symmetric_difference_update(
            this->view, std::forward<Container>(items)
        );
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both.  Appends to the head of the dictionary rather than
    the tail. */
    template <typename Container>
    inline void symmetric_difference_update_left(Container&& items) {
        linked::symmetric_difference_update_left(
            this->view, std::forward<Container>(items)
        );
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Value& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index(this->view, key, start, stop);
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Value& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count(this->view, key, start, stop);
    }

    /* Check if the dictionary contains a certain key. */
    inline bool contains(const Value& key) const {
        return linked::contains(this->view, key);
    }

    /* Check if the dictionary contains a certain key and move it to the front of the
    dictionary if so. */
    inline bool lru_contains(const Value& key) {
        return linked::lru_contains(this->view, key);
    }

    /* Remove a key from the dictionary. */
    inline void remove(const Value& key) {
        linked::remove(this->view, key);
    }

    /* Remove a key from the dictionary if it is present. */
    inline void discard(const Value& key) {
        linked::discard(this->view, key);
    }

    /* Remove a key from the dictionary and return its value. */
    inline MappedValue pop(
        const Value& key,
        std::optional<MappedValue> default_value = std::nullopt
    ) {
        return linked::pop(this->view, key, default_value);
    }

    // TODO: popitem should be able to take an index which defaults to -1 just like
    // pop() for the other containers

    /* Remove and return a key, value pair from the dictionary. */
    inline std::pair<Value, MappedValue> popitem() {
        return linked::popitem(this->view);
    }

    /* Remove all elements from the dictionary. */
    inline void clear() {
        linked::clear(this->view);
    }

    /* Get a value from the dictionary using an optional default. */
    inline MappedValue get(
        const Value& key,
        std::optional<MappedValue> default_value = std::nullopt
    ) const {
        return linked::get(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it if it is not already present. */
    inline void setdefault(
        const Value& key,
        const MappedValue& default_value = MappedValue()
    ) {
        linked::setdefault(this->view, key, default_value);
    }

    /* Return a shallow copy of the dictionary. */
    inline LinkedDict copy() const {
        return linked::copy(this->view);
    }

    /* Sort the keys within the dictionary in-place according to an optional key
    func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse=false) {
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of keys within the dictionary in-place. */
    inline void reverse() {
        linked::reverse(this->view);
    }

    /* Shift all keys in the dictionary to the right by the specified number of
    steps. */
    inline void rotate(long long steps = 1) {
        linked::rotate(this->view, steps);
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers. */
    template <typename... Containers>
    inline LinkedDict union_(Containers&&... items) const {
        return LinkedDict(
            linked::union_(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers.  Appends to the head of the dictionary rather than the tail. */
    template <typename... Containers>
    inline LinkedDict union_left(Containers&&... items) const {
        return LinkedDict(
            linked::union_left(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements common to this dictionary and all other
    containers. */
    template <typename... Containers>
    inline LinkedDict intersection(Containers&&... items) const {
        return LinkedDict(
            linked::intersection(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements from this dictionary that are common to
    any other containers. */
    template <typename... Containers>
    inline LinkedDict difference(Containers&&... items) const {
        return LinkedDict(
            linked::difference(this->view, std::forward<Containers>(items)...)
        );
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both. */
    template <typename Container>
    inline LinkedDict symmetric_difference(Container&& items) const {
        return LinkedDict(
            linked::symmetric_difference(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both.  Appends to the head of the dictionary rather than the
    tail. */
    template <typename Container>
    inline LinkedDict symmetric_difference_left(Container&& items) const {
        return LinkedDict(
            linked::symmetric_difference_left(
                this->view, std::forward<Container>(items)
            )
        );
    }

    /* Check whether all keys within this dictionary are also present in another
    container. */
    template <typename Container>
    inline bool issubset(Container&& items) const {
        return linked::issubset(
            this->view, std::forward<Container>(items), false
        );
    }

    /* Check whether all keys within another container are also present in this
    dictionary. */
    template <typename Container>
    inline bool issuperset(Container&& items) const {
        return linked::issuperset(
            this->view, std::forward<Container>(items), false
        );
    }

    /* Get the linear distance between two keys within the dictionary. */
    inline long long distance(const Value& from, const Value& to) const {
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two keys within the dictionary. */
    inline void swap(const Value& key1, const Value& key2) {
        linked::swap(this->view, key1, key2);
    }

    /* Move a key within the dictionary by the specified number of steps. */
    inline void move(const Value& key, long long steps) {
        linked::move(this->view, key, steps);
    }

    /* Move a key within the dictionary to a specific index. */
    inline void move_to_index(const Value& key, long long index) {
        linked::move_to_index(this->view, key, index);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    // TODO: keys(), values(), items(), map(), position(), slice()

    /* Get a proxy for a value at a particular index of the dictionary. */
    inline linked::ElementProxy<View> position(long long index) {
        return linked::position(this->view, index);
    }

    /* Get a proxy for a slice within the dictionary. */
    template <typename... Args>
    inline linked::SliceProxy<View, LinkedDict> slice(Args&&... args) {
        return linked::slice<View, LinkedDict>(
            this->view,
            std::forward<Args>(args)...
        );
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    // TODO: index operator uses map() proxy


};


/////////////////////////////////////
////    STRING REPRESENTATION    ////
/////////////////////////////////////


// TODO: account for mapped values in specialization and final string.  This should
// be a specialization of linked::repr()


/* Override the << operator to print the abbreviated contents of a dictionary to an
output stream (equivalent to Python repr()). */
template <typename... Ts>
std::ostream& operator<<(std::ostream& stream, const LinkedDict<Ts...>& dict) {
    stream << linked::repr(
        set.view,
        "LinkedDict",
        "{",
        "}",
        64
    );
    return stream;
}


//////////////////////////////
////    SET ARITHMETIC    ////
//////////////////////////////


/* Get the union between a LinkedDict and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...> operator|(const LinkedDict<Ts...>& dict, const Container& other) {
    return dict.union_(other);
}


/* Get the difference between a LinkedDict and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...> operator-(const LinkedDict<Ts...>& dict, const Container& other) {
    return dict.difference(other);
}


/* Get the intersection between a LinkedDict and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...> operator&(const LinkedDict<Ts...>& dict, const Container& other) {
    return dict.intersection(other);
}


/* Get the symmetric difference between a LinkedDict and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...> operator^(const LinkedDict<Ts...>& dict, const Container& other) {
    return dict.symmetric_difference(other);
}


/* Update a LinkedDict in-place, replacing it with the union of it and an arbitrary
container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...>& operator|=(LinkedDict<Ts...>& dict, const Container& other) {
    dict.update(other);
    return dict;
}


/* Update a LinkedDict in-place, replacing it with the difference between it and an
arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...>& operator-=(LinkedDict<Ts...>& dict, const Container& other) {
    dict.difference_update(other);
    return dict;
}


/* Update a LinkedDict in-place, replacing it with the intersection between it and an
arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...>& operator&=(LinkedDict<Ts...>& dict, const Container& other) {
    dict.intersection_update(other);
    return dict;
}


/* Update a LinkedDict in-place, replacing it with the symmetric difference between it
and an arbitrary container. */
template <typename Container, typename... Ts>
LinkedDict<Ts...>& operator^=(LinkedDict<Ts...>& dict, const Container& other) {
    dict.symmetric_difference_update(other);
    return dict;
}


//////////////////////////////
////    SET COMPARISON    ////
//////////////////////////////


/* Check whether a LinkedDict is a proper subset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator<(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::issubset(dict.view, other, true);
}


/* Check whether a LinkedDict is a subset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator<=(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::issubset(dict.view, other, false);
}


/* Check whether a LinkedDict is equal to an arbitrary container. */
template <typename Container, typename... Ts>
bool operator==(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::set_equal(dict.view, other);
}


/* Check whether a LinkedDict is not equal to an arbitrary container. */
template <typename Container, typename... Ts>
bool operator!=(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::set_not_equal(dict.view, other);
}


/* Check whether a LinkedDict is a superset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator>=(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::issuperset(dict.view, other, false);
}


/* Check whether a LinkedDict is a proper superset of an arbitrary container. */
template <typename Container, typename... Ts>
bool operator>(const LinkedDict<Ts...>& dict, const Container& other) {
    return linked::issuperset(dict.view, other, true);
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the Python dict interface for a linked data structure. */
template <typename Derived>
class PyDictInterface {
public:



protected:

    /* docstrings for public Python attributes. */
    struct docs {

        // TODO: add() overloads for key-value pairs?
        // -> maybe these should just be on the map() proxy itself.
        // dict[key].lru_get()
        // dict[key].lru_set()
        // dict[key].set_left()

        static constexpr std::string_view fromkeys {R"doc(

)doc"
        };

        static constexpr std::string_view pop {R"doc(

)doc"
        };

        static constexpr std::string_view popitem {R"doc(

)doc"
        };

        static constexpr std::string_view get {R"doc(

)doc"
        };

        static constexpr std::string_view setdefault {R"doc(

)doc"
        };

    };

};


/* A discriminated union of templated `LinkedDict` types that can be used from
Python. */
class PyLinkedDict :
    public PyLinkedBase<PyLinkedDict>,
    public PyListInterface<PyLinkedDict>,
    public PySetInterface<PyLinkedDict>,
    public PyDictInterface<PyLinkedDict>
{
    using Base = PyLinkedBase<PyLinkedDict>;
    using IList = PyListInterface<PyLinkedDict>;
    using ISet = PySetInterface<PyLinkedDict>;
    using IDict = PyDictInterface<PyLinkedDict>;

    /* A std::variant representing all the LinkedDict implementations that are
    constructable from Python. */
    // TODO

    friend Base;
    friend IList;
    friend ISet;
    friend IDict;
    Variant variant;

    /* Construct a PyLinkedDict around an existing C++ LinkedDict. */
    template <typename Set>
    inline void from_cpp(Set&& set) {
        new (&variant) Variant(std::forward<Set>(set));
    }

    /* Construct a PyLinkedDict from scratch using the given constructor arguments. */
    static void construct(
        PyLinkedDict* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked
    ) {
        if (iterable == nullptr) {
            if (singly_linked) {
                new (&self->variant) Variant(SingleDict(max_size, spec));
            } else {
                new (&self->variant) Variant(DoubleDict(max_size, spec));
            }
        } else {
            if (singly_linked) {
                new (&self->variant) Variant(
                    SingleDict(iterable, max_size, spec, reverse)
                );
            } else {
                new (&self->variant) Variant(
                    DoubleDict(iterable, max_size, spec, reverse)
                );
            }
        }
    }

public:

    /* Initialize a LinkedDict instance from Python. */
    static int __init__(PyLinkedDict* self, PyObject* args, PyObject* kwargs) {
        using Args = util::PyArgs<util::CallProtocol::KWARGS>;
        using util::ValueError;
        static constexpr std::string_view meth_name{"__init__"};
        try {
            // parse arguments
            Args pyargs(meth_name, args, kwargs);
            PyObject* iterable = pyargs.parse(
                "iterable", util::none_to_null, (PyObject*)nullptr
            );
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) return std::nullopt;
                    long long result = util::parse_int(obj);
                    if (result < 0) throw ValueError("max_size cannot be negative");
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", util::none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", util::is_truthy, false);
            bool singly_linked = pyargs.parse("singly_linked", util::is_truthy, false);
            pyargs.finalize();

            // initialize
            construct(self, iterable, max_size, spec, reverse, singly_linked);

            // exit normally
            return 0;

        // translate C++ exceptions into Python eerrors
        } catch (...) {
            util::throw_python();
            return -1;
        }
    }

    // TODO: account for mapped values in final string.

    /* Implement `LinkedDict.__str__()` in Python. */
    static PyObject* __str__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            stream << "{";
            std::visit(
                [&stream](auto& set) {
                    auto it = set.begin();
                    if (it != set.end()) {
                        stream << util::repr(*it);
                        ++it;
                    }
                    for (; it != set.end(); ++it) {
                        stream << ", " << util::repr(*it);
                    }
                },
                self->variant
            );
            stream << "}";
            return PyUnicode_FromString(stream.str().c_str());

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedDict.__repr__()` in Python. */
    static PyObject* __repr__(PyLinkedDict* self) {
        try {
            std::ostringstream stream;
            std::visit(
                [&stream](auto& set) {
                    stream << set;
                },
                self->variant
            );
            return PyUnicode_FromString(stream.str().c_str());

        // translate C++ errors into Python exceptions
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

private:

    /* docstrings for public Python attributes. */
    struct docs {

        static constexpr std::string_view LinkedDict {R"doc(

)doc"
        };

    };

    ////////////////////////////////
    ////    PYTHON INTERNALS    ////
    ////////////////////////////////

    #define BASE_PROPERTY(NAME) \
        { #NAME, (getter) Base::NAME, NULL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define BASE_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) Base::NAME, ARG_PROTOCOL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define LIST_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IList::NAME, ARG_PROTOCOL, PyDoc_STR(IList::docs::NAME.data()) } \

    #define SET_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) ISet::NAME, ARG_PROTOCOL, PyDoc_STR(ISet::docs::NAME.data()) } \

    #define DICT_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IDict::NAME, ARG_PROTOCOL, PyDoc_STR(IDict::docs::NAME.data()) } \

    /* Vtable containing Python @property definitions for the LinkedDict */
    inline static PyGetSetDef properties[] = {
        BASE_PROPERTY(SINGLY_LINKED),
        BASE_PROPERTY(DOUBLY_LINKED),
        // BASE_PROPERTY(XOR),  // not yet implemented
        BASE_PROPERTY(DYNAMIC),
        BASE_PROPERTY(PACKED),
        BASE_PROPERTY(STRICTLY_TYPED),
        BASE_PROPERTY(lock),
        BASE_PROPERTY(capacity),
        BASE_PROPERTY(max_size),
        BASE_PROPERTY(frozen),
        BASE_PROPERTY(nbytes),
        BASE_PROPERTY(specialization),
        {NULL}  // sentinel
    };

    // TODO: add dictionary-specific methods

    /* Vtable containing Python method definitions for the LinkedDict. */
    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        BASE_METHOD(__class_getitem__, METH_CLASS | METH_O),
        LIST_METHOD(insert, METH_FASTCALL),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(remove, METH_O),
        LIST_METHOD(pop, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(add, METH_O),
        SET_METHOD(add_left, METH_O),
        SET_METHOD(lru_add, METH_O),
        SET_METHOD(discard, METH_O),
        SET_METHOD(lru_contains, METH_O),
        {
            "union",  // renamed
            (PyCFunction) union_,
            METH_FASTCALL,
            PyDoc_STR(ISet::docs::union_.data())
        },
        SET_METHOD(union_left, METH_FASTCALL),
        SET_METHOD(update, METH_FASTCALL),
        SET_METHOD(update_left, METH_FASTCALL),
        SET_METHOD(lru_update, METH_FASTCALL),
        SET_METHOD(difference, METH_FASTCALL),
        SET_METHOD(difference_update, METH_FASTCALL),
        SET_METHOD(intersection, METH_FASTCALL),
        SET_METHOD(intersection_update, METH_FASTCALL),
        SET_METHOD(symmetric_difference, METH_O),
        SET_METHOD(symmetric_difference_left, METH_O),
        SET_METHOD(symmetric_difference_update, METH_O),
        SET_METHOD(symmetric_difference_update_left, METH_O),
        SET_METHOD(isdisjoint, METH_O),
        SET_METHOD(issubset, METH_O),
        SET_METHOD(issuperset, METH_O),
        SET_METHOD(distance, METH_FASTCALL),
        SET_METHOD(swap, METH_FASTCALL),
        SET_METHOD(move, METH_FASTCALL),
        SET_METHOD(move_to_index, METH_FASTCALL),
        {NULL}  // sentinel
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD
    #undef DICT_METHOD

    // TODO: use dictionary-specific mapping methods

    /* Vtable containing special methods related to Python's mapping protocol. */
    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) Base::__len__;
        slots.mp_subscript = (binaryfunc) IList::__getitem__;
        slots.mp_ass_subscript = (objobjargproc) IList::__setitem__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's sequence protocol. */
    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) Base::__len__;
        slots.sq_item = (ssizeargfunc) IList::__getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) IList::__setitem_scalar__;
        slots.sq_contains = (objobjproc) IList::__contains__;
        return slots;
    }();

    /* Vtable containing special methods related to Python's number protocol. */
    inline static PyNumberMethods number = [] {
        PyNumberMethods slots;
        slots.nb_or = (binaryfunc) ISet::__or__;
        slots.nb_inplace_or = (binaryfunc) ISet::__ior__;
        slots.nb_subtract = (binaryfunc) ISet::__sub__;
        slots.nb_inplace_subtract = (binaryfunc) ISet::__isub__;
        slots.nb_and = (binaryfunc) ISet::__and__;
        slots.nb_inplace_and = (binaryfunc) ISet::__iand__;
        slots.nb_xor = (binaryfunc) ISet::__xor__;
        slots.nb_inplace_xor = (binaryfunc) ISet::__ixor__;
        return slots;
    }();

    /* Initialize a PyTypeObject to represent the set in Python. */
    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.structs.LinkedDict",
            .tp_basicsize = sizeof(PyLinkedDict),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) __repr__,
            .tp_as_number = &number,
            .tp_as_sequence = &sequence,
            .tp_as_mapping = &mapping,
            .tp_hash = (hashfunc) PyObject_HashNotImplemented,  // not hashable
            .tp_str = (reprfunc) __str__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_SEQUENCE
                // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
            ),
            .tp_doc = PyDoc_STR(docs::LinkedDict.data()),
            .tp_traverse = (traverseproc) Base::__traverse__,
            .tp_clear = (inquiry) Base::__clear__,
            .tp_richcompare = (richcmpfunc) IList::__richcompare__,
            .tp_iter = (getiterfunc) Base::__iter__,
            .tp_methods = methods,
            .tp_getset = properties,
            .tp_init = (initproc) __init__,
            .tp_new = (newfunc) Base::__new__,
        };
    };

public:

    /* The final Python type. */
    inline static PyTypeObject Type = build_type();

    /* Check whether another PyObject* is of this type. */
    inline static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) throw util::catch_python();
        return static_cast<bool>(result);
    }

};


/* Python module definition. */
static struct PyModuleDef module_dict = {
    PyModuleDef_HEAD_INIT,
    .m_name = "dict",
    .m_doc = (
        "This module contains an optimized LinkedDict data structure for use "
        "in Python.  The exact same data structure is also available in C++ "
        "under the same header path (bertrand/structs/linked/dict.h)."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_dict(void) {
    // initialize type objects
    if (PyType_Ready(&PyLinkedDict::Type) < 0) return nullptr;

    // initialize module
    PyObject* mod = PyModule_Create(&module_dict);
    if (mod == nullptr) return nullptr;

    // link type to module
    Py_INCREF(&PyLinkedDict::Type);
    if (PyModule_AddObject(mod, "LinkedDict", (PyObject*) &PyLinkedDict::Type) < 0) {
        Py_DECREF(&PyLinkedDict::Type);
        Py_DECREF(mod);
        return nullptr;
    }
    return mod;
}


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_DICT_H
