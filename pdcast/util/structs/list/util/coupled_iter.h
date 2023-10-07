// include guard: BERTRAND_STRUCTS_UTIL_COUPLED_ITER_H
#ifndef BERTRAND_STRUCTS_UTIL_COUPLED_ITER_H
#define BERTRAND_STRUCTS_UTIL_COUPLED_ITER_H

#include <iterator>  // std::iterator_traits
#include <utility>  // std::move, std::declval


namespace bertrand {
namespace structs {
namespace util {


/* NOTE: CoupledIterators are used to share state between the begin() and end()
 * iterators in a loop and generally simplify the iterator interface.  They act like
 * pass-through decorators for the begin() iterator, and contain their own end()
 * iterator to terminate the loop.  This means we can write loops as follows:
 *
 * for (auto iter = view.iter(); iter != iter.end(); ++iter) {
 *     // full access to iter
 * }
 * 
 * Rather than the more verbose:
 * 
 * for (auto iter = view.begin(), end = view.end(); iter != end; ++iter) {
 *      // same as above
 * }
 * 
 * Both generate identical code, but the former is more concise and easier to read.  It
 * also allows any arguments provided to the call operator to be passed through to both
 * the begin() and end() iterators, which can be used to share state between the two.
 */


/* A coupled pair of begin() and end() iterators to simplify the iterator interface. */
template <typename IteratorType>
class CoupledIterator {
public:
    using Iterator = IteratorType;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename Iterator::iterator_category;
    using difference_type       = typename Iterator::difference_type;
    using value_type            = typename Iterator::value_type;
    using pointer               = typename Iterator::pointer;
    using reference             = typename Iterator::reference;

    // couple the begin() and end() iterators into a single object
    CoupledIterator(const Iterator& first, const Iterator& second) :
        first(std::move(first)), second(std::move(second))
    {}

    // allow use of the CoupledIterator in a range-based for loop
    Iterator& begin() { return first; }
    Iterator& end() { return second; }

    // pass iterator protocol through to begin()
    inline value_type operator*() const { return *first; }
    inline CoupledIterator& operator++() { ++first; return *this; }
    inline bool operator!=(const Iterator& other) const { return first != other; }

    // conditionally compile all other methods based on Iterator interface.
    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the CoupledIterator interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    template <typename T = Iterator>
    inline auto prev() const -> decltype(std::declval<T>().prev()) {
        return first.prev();
    }

    template <typename T = Iterator>
    inline auto curr() const -> decltype(std::declval<T>().curr()) {
        return first.curr();
    }

    template <typename T = Iterator>
    inline auto next() const -> decltype(std::declval<T>().next()) {
        return first.next();
    }

    template <typename T = Iterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        return first.insert(value);  // void
    }

    template <typename T = Iterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        return first.remove();
    }

    template <typename T = Iterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        return first.replace(value);
    }

    template <typename T = Iterator>
    inline auto index() -> decltype(std::declval<T>().index()) const {
        return first.index();
    }

    template <typename T = Iterator>
    inline auto idx() -> decltype(std::declval<T>().idx()) const {
        return first.idx();
    }

protected:
    Iterator first, second;
};


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_COUPLED_ITER_H
