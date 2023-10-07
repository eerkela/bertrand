// include guard: BERTRAND_STRUCTS_UTIL_SLOT_H
#ifndef BERTRAND_STRUCTS_UTIL_SLOT_H
#define BERTRAND_STRUCTS_UTIL_SLOT_H

#include <stdexcept>  // std::runtime_error
#include <utility>  // std::move(), std::forward()


namespace bertrand {
namespace structs {
namespace util {


/* A raw memory buffer that supports delayed construction from C++/Cython.

NOTE: To be able to stack allocate members of Cython/Python extension types, the
object must be trivially constructible.  If this is not the case, then a Slot can be
used to allocate raw memory on the stack, into which values can be easily constructed
and/or moved.  This allows intuitive RAII semantics in Cython, and avoids unnecessary
heap allocations and indirection that they bring. */
template <typename T>
class Slot {
private:
    bool _constructed = false;
    alignas(T) char data[sizeof(T)];  // access via reinterpret_cast

public:

    /* Construct the value within the memory buffer using the given arguments,
    replacing any previous value it may have held. */
    template <typename... Args>
    inline void construct(Args&&... args) {
        this->destroy();
        new (data) T(std::forward<Args>(args)...);
        _constructed = true;
    }

    /* Indicates whether the slot currently holds a valid object. */
    inline bool constructed() const noexcept {
        return _constructed;
    }

    /* Destroy the value within the memory buffer. */
    inline void destroy() noexcept {
        if (_constructed) {
            reinterpret_cast<T&>(data).~T();
            _constructed = false;
        }
    }

    /* Trivial constructor for delayed construction. */
    Slot() noexcept {}

    /* Move constructor. */
    Slot(Slot&& other) noexcept : _constructed(other._constructed) {
        if (_constructed) {
            this->construct(std::move(other.operator*()));
        }
    }

    /* Move assignment operator. */
    inline Slot& operator=(Slot&& other) {
        *this = std::move(other.operator*());  // forward to rvalue overload
        return *this;
    }

    /* Assign a new value to the memory buffer using ordinary C++ move semantics. */
    inline Slot& operator=(T&& val) {
        this->construct(std::move(val));
        return *this;
    }

    /* Destroy the value within the memory buffer. */
    ~Slot() noexcept {
        this->destroy();
    }

    /* Dereference to get the value currently stored in the memory buffer. */
    inline T& operator*() {
        if (!_constructed) {
            throw std::runtime_error("Slot is not initialized");
        }
        return reinterpret_cast<T&>(data);
    }

    /* Get a pointer to the value currently stored in the memory buffer.

    NOTE: Cython sometimes has trouble with the above dereference operator and value
    semantics in general, so this method can be used as an alternative. */
    inline T* ptr() {
        return &(this->operator*());
    }

    /* Assign a new value into the memory buffer using Cython-compatible move semantics.

    NOTE: this has to accept a pointer to a temporary object because Cython does not
    support rvalue references.  This means that the referenced object will be in an
    undefined state after this method is called, as if it had been moved from. */
    inline void move_ptr(T* val) {
        *this = std::move(*val);  // forward to rvalue assignment
    }

};


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_SLOT_H
