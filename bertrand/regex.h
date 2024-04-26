#ifndef BERTRAND_REGEX_H
#define BERTRAND_REGEX_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

#define PCRE2_CODE_UNIT_WIDTH 8  // for UTF8/ASCII strings
#include "pcre2.h"


#include <iostream>


#include <pybind11/pybind11.h>


// TODO: compile-time regular expressions are in fact possible using PCRE.

// https://github.com/hanickadot/compile-time-regular-expressions



namespace bertrand {


namespace detail {

    template <typename T>
    concept group_id =
        std::convertible_to<T, size_t> ||
        std::convertible_to<T, const char*> ||
        std::convertible_to<T, const std::string&> ||
        std::convertible_to<T, const std::string_view&>;

}


/* A thin wrapper around a compiled PCRE2 regular expression that provides a Pythonic
interface for matching and searching strings.

This class is designed as a direct replacement for Python's `re.Pattern` objects, but
is implemented using PCRE2 for full C++ compatibility.  This makes it possible to
parse both Python and C++ strings interchangeably with the same syntax, which is
broadly similar to Python's `re` module.  Some differences exist, including (but not
limited to):
    - The absence of the `search()` or `fullmatch()` methods.  Instead, the default
      behavior of `Regex::match()` is equivalent to Python's `search()` method,
      returning the first match found in the target string.  To replicate Python's
      `match()`, users should prepend the pattern with `^`.  To replicate `fullmatch()`,
      users should also append the pattern with `$`.
    - There is no `escape()` or `expand()` methods, as PCRE2 does not provide
      equivalent functions.
    - Many of the flags available in Python's `re` module are not available here and
      vice versa.  PCRE2 provides a different and much expanded set of flags, which
      are too numerous to replicate here.  By default, all regular expressions are
      built with JIT compilation enabled for maximum performance.

Other than these differences, the `Regex` class should be familiar to anyone who has
used Python's `re` module.  PCRE2 has nearly identical syntax and supports all the same
features, as well as powerful additions like JIT compliation, partial matching, and
recursive patterns.

PCRE2 reference:
https://www.pcre.org/current/doc/html/index.html
*/
class Regex {
    std::string _pattern;
    uint32_t _flags;
    std::shared_ptr<pcre2_code> code;  // make this a shared ptr

    static bool normalize_indices(long long size, long long& start, long long& stop) {
        if (start < 0) {
            start += size;
            if (start < 0) {
                start = 0;
            }
        } else if (start >= size) {
            return false;
        }

        if (stop < 0) {
            stop += size;
            if (stop < 0) {
                return false;
            }
        } else if (stop > size) {
            stop = size;
        }

        if (stop <= start) {
            return false;
        }
        return true;
    }

    static std::runtime_error pcre_error(int err_code) {
        PCRE2_UCHAR buffer[256];
        int rc = pcre2_get_error_message(err_code, buffer, sizeof(buffer));
        if (rc < 0) {  // error while retrieving error
            return std::runtime_error(
                "pcre2_get_error_message() returned error code " +
                std::to_string(rc)
            );
        }
        return std::runtime_error(
            std::string(reinterpret_cast<char*>(buffer), rc)
        );
    }

    struct internal {};

    /* Compile the pattern into a PCRE2 regular expression. */
    template <typename T>
    Regex(const T& pattern, uint32_t flags, internal) :
        _pattern(pattern), _flags(flags), code(nullptr, pcre2_code_free)
    {
        int err;
        PCRE2_SIZE err_offset;

        // compile the pattern
        code = std::shared_ptr<pcre2_code> {
            pcre2_compile(
                reinterpret_cast<PCRE2_SPTR>(_pattern.c_str()),
                _pattern.size(),
                flags,
                &err,
                &err_offset,
                nullptr  // use default compile context
            ),
            pcre2_code_free
        };

        // pretty print errors
        if (code == nullptr) {
            PCRE2_UCHAR pcre_err[256];
            pcre2_get_error_message(err, pcre_err, sizeof(pcre_err));
            std::ostringstream err_msg;
            err_msg << "[invalid regex] " << pcre_err << "\n\n    ";
            err_msg << _pattern << "\n    ";
            err_msg << std::string(err_offset, ' ') << "^";
            throw std::runtime_error(err_msg.str());
        }

        // JIT compile the expression if possible
        uint32_t jit_flags = flags & (
            PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT | PCRE2_JIT_PARTIAL_HARD
        );
        if (jit_flags) {
            int rc = pcre2_jit_compile(code.get(), jit_flags);
            if (rc < 0 && rc != PCRE2_ERROR_JIT_BADOPTION) {
                pcre2_code_free(code.get());
                code = nullptr;
                throw pcre_error(rc);
            }
        }
    }

public:
    class Iterator;

    /* Default constructor.  Yields a null pattern, which should not be used in
    matching.  This constructor exists only to make the Regex class trivially
    constructable, which is a requirement for pybind11 type casters. */
    Regex() : _pattern(), _flags(0), code(nullptr, pcre2_code_free) {}

    template <size_t N>
    explicit Regex(const char(&pattern)[N], uint32_t flags = PCRE2_JIT_COMPLETE) :
        Regex(pattern, flags, internal{})
    {}

    explicit Regex(const std::string& pattern, uint32_t flags = PCRE2_JIT_COMPLETE) :
        Regex(pattern, flags, internal{})
    {}

    explicit Regex(const std::string_view& pattern, uint32_t flags = PCRE2_JIT_COMPLETE) :
        Regex(pattern, flags, internal{})
    {}

    /* Copy constructor. */
    Regex(const Regex& other) :
        _pattern(other._pattern), _flags(other._flags),
        code(other.code)
    {}

    /* Move constructor. */
    Regex(Regex&& other) noexcept :
        _pattern(std::move(other._pattern)), _flags(other._flags),
        code(std::move(other.code))
    {}

    /* Copy assignment operator. */
    Regex& operator=(const Regex& other) {
        if (&other != this) {
            _pattern = other._pattern;
            _flags = other._flags;
            code = other.code;
        }
        return *this;
    }

    /* Move assignment operator. */
    Regex& operator=(Regex&& other) noexcept {
        if (&other != this) {
            _pattern = std::move(other._pattern);
            _flags = other._flags;
            code = std::move(other.code);
        }
        return *this;
    }

    //////////////////////
    ////    CONFIG    ////
    //////////////////////

    /* Get the pattern used to construct the regular expression. */
    inline std::string pattern() const noexcept {
        return _pattern;
    }

    /* Get the PCRE flags that were used to build the regular expression. */
    inline uint32_t flags() const noexcept {
        return _flags;
    }

    /* Check whether a particular flag is set for the regular expression. */
    inline bool has_flag(uint32_t flag) const noexcept {
        return (flags() & flag) != 0;
    }

    /* Get the number of capture groups in the regular expression. */
    inline size_t groupcount() const noexcept {
        if (code == nullptr) {
            return 0;
        }
        uint32_t count;
        pcre2_pattern_info(code.get(), PCRE2_INFO_CAPTURECOUNT, &count);
        return count;
    }

    /* Get a dictionary mapping symbolic group names to their corresponding numbers. */
    inline std::unordered_map<std::string, size_t> groupindex() const {
        std::unordered_map<std::string, size_t> result;
        if (code == nullptr) {
            return result;
        }

        // retrieve name table
        PCRE2_SPTR table;
        uint32_t name_count;
        uint32_t name_entry_size;
        pcre2_pattern_info(code.get(), PCRE2_INFO_NAMETABLE, &table);
        pcre2_pattern_info(code.get(), PCRE2_INFO_NAMECOUNT, &name_count);
        pcre2_pattern_info(code.get(), PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

        // extract all named capture groups
        for (uint32_t i = 0; i < name_count; ++i) {
            uint16_t group_number = (table[0] << 8) | table[1];
            const char* name = reinterpret_cast<const char*>(table + 2);
            result[name] = group_number;
            table += name_entry_size;
        }
        return result;
    }

    ///////////////////////////
    ////    BASIC MATCH    ////
    ///////////////////////////

    /* A match object allowing easy access to capture groups and match information. */
    class Match {
        friend Regex;
        friend Iterator;
        pcre2_code* code;
        std::shared_ptr<pcre2_match_data> match;
        std::string subject;
        PCRE2_SIZE* ovector;
        size_t _count;

        /* Get the group number associated with a named capture group. */
        inline int groupnum(const char* name) const {
            return pcre2_substring_number_from_name(
                code,
                reinterpret_cast<PCRE2_SPTR>(name)
            );
        }

        Match(
            pcre2_code* code,
            const std::string& subject,
            pcre2_match_data* match,
            bool owns_match
        ) : code(code), match(match, pcre2_match_data_free), subject(subject),
            ovector(nullptr), _count(0)
        {
            if (match != nullptr) {
                ovector = pcre2_get_ovector_pointer(match);
                _count = pcre2_get_ovector_count(match);
            }
        }

    public:
        Match() = default;

        /* Copy constructor. */
        Match(const Match& other) noexcept :
            code(other.code), match(other.match), subject(other.subject),
            ovector(other.ovector), _count(other._count)
        {}

        /* Move constructor. */
        Match(Match&& other) noexcept :
            code(other.code), match(std::move(other.match)),
            subject(std::move(other.subject)), ovector(other.ovector),
            _count(other._count)
        {
            other.code = nullptr;
            other.ovector = nullptr;
        }

        /* Copy assignment operator. */
        Match& operator=(const Match& other) noexcept {
            if (&other != this) {
                code = other.code;
                subject = other.subject;
                ovector = other.ovector;
                _count = other._count;
                match = other.match;
            }
            return *this;
        }

        /* Move assignment operator. */
        Match& operator=(Match&& other) noexcept {
            if (&other != this) {
                code = other.code;
                subject = std::move(other.subject);
                ovector = other.ovector;
                _count = other._count;
                match = std::move(other.match);
                other.code = nullptr;
                other.ovector = nullptr;
            }
            return *this;
        }

        inline operator bool() const {
            return match != nullptr;
        }

        /* Get the number of captured substrings, including the full match. */
        inline size_t count() const noexcept {
            return _count;
        }

        ///////////////////////////////////////
        ////    NUMBERED CAPTURE GROUPS    ////
        ///////////////////////////////////////

        /* Get the start index of a numbered capture group. */
        inline std::optional<size_t> start(size_t index = 0) const noexcept {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }
            return std::make_optional(ovector[index * 2]);
        }

        /* Get the start index of a numbered capture group. */
        inline std::optional<size_t> stop(size_t index = 0) const noexcept {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }
            return std::make_optional(ovector[index * 2 + 1]);
        }

        /* Return a pair containing both the start and end indices of a numbered
        capture group. */
        inline auto span(size_t index = 0) const noexcept
            -> std::optional<std::pair<size_t, size_t>>
        {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }
            size_t adj_index = index * 2;
            return std::make_pair(ovector[adj_index], ovector[adj_index + 1]);
        }

        // TODO: pipe this into the iterator class as its dereference operator.  The
        // iterator can then just maintain a pointer to the ovector and the current
        // index

        /* Extract a numbered capture group. */
        inline std::optional<std::string> group(size_t index = 0) const noexcept {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }
            size_t adj_index = index * 2;
            size_t start = ovector[adj_index];
            size_t end = ovector[adj_index + 1];
            if (start == end) {
                return std::nullopt;
            } else {
                return std::string(subject.c_str() + start, end - start);
            }
        }

        /* Extract several capture groups at once using an initializer list. */
        inline std::vector<std::optional<std::string>> group(
            const std::initializer_list<std::variant<size_t, const char*>>& groups
        ) const noexcept {
            std::vector<std::optional<std::string>> result;
            result.reserve(groups.size());
            for (auto&& item : groups) {
                std::visit(
                    [&result, this](auto&& arg) {
                        result.push_back(group(arg));
                    },
                    item
                );
            }
            return result;
        }

        /* Extract several capture groups at once, as called from Python using variadic
        positional arguments. */
        inline auto group(const pybind11::args& args) const
            -> std::vector<std::optional<std::string>>;

        /* Extract all capture groups into a std::vector. */
        inline std::vector<std::optional<std::string>> groups(
            std::optional<std::string> default_value = std::nullopt
        ) const noexcept {
            std::vector<std::optional<std::string>> result;
            size_t n = count();
            result.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                std::optional<std::string> grp = group(i);
                result.push_back(grp.has_value() ? grp : default_value);
            }
            return result;
        }

        ////////////////////////////////////
        ////    NAMED CAPTURE GROUPS    ////
        ////////////////////////////////////

        /* Get a capture group's name from its index number. */
        inline std::optional<std::string> groupname(size_t index) const noexcept {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }

            // retrieve name table
            PCRE2_SPTR table;
            uint32_t name_count;
            uint32_t name_entry_size;
            pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &table);
            pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &name_count);
            pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

            // search for the name
            for (uint32_t i = 0; i < name_count; ++i) {
                // group number is stored in the first 2 bytes of the name entry
                uint16_t group_number = (table[0] << 8) | table[1];

                // name is stored right after group number
                if (group_number == index) {
                    const char* name = reinterpret_cast<const char*>(table + 2);
                    return std::string(name);
                }

                // move to the next name entry
                table += name_entry_size;
            }
            return std::nullopt;
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> start(const char* name) const noexcept {
            int number = groupnum(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return start(number);
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> start(const std::string& name) const noexcept {
            return start(name.c_str());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> start(const std::string_view& name) const noexcept {
            return start(name.data());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const char* name) const noexcept {
            int number = groupnum(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return stop(number);
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const std::string& name) const noexcept {
            return stop(name.c_str());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const std::string_view& name) const noexcept {
            return stop(name.data());
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const char* name) const noexcept
            -> std::optional<std::pair<size_t, size_t>>
        {
            int number = groupnum(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return std::make_pair(start(number).value(), stop(number).value());
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const std::string& name) const noexcept
            -> std::optional<std::pair<size_t, size_t>>
        {
            return span(name.c_str());
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const std::string_view& name) const noexcept
            -> std::optional<std::pair<size_t, size_t>>
        {
            return span(name.data());
        }

        /* Extract a named capture group. */
        inline auto group(const char* name) const noexcept
            -> std::optional<std::string>
        {
            int number = groupnum(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return group(number);
        }

        /* Extract a named capture group. */
        inline auto group(const std::string& name) const noexcept
            -> std::optional<std::string>
        {
            return group(name.c_str());
        }

        /* Extract a named capture group. */
        inline auto group(const std::string_view& name) const noexcept
            -> std::optional<std::string>
        {
            return group(name.data());
        }

        /* Extract all named capture groups into a std::unordered_map. */
        inline std::unordered_map<std::string, std::optional<std::string>> groupdict(
            std::optional<std::string> default_value = std::nullopt
        ) const noexcept {
            std::unordered_map<std::string, std::optional<std::string>> result;
            if (match == nullptr) {
                return result;
            }

            // retrieve name table
            PCRE2_SPTR table;
            uint32_t name_count;
            uint32_t name_entry_size;
            pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &table);
            pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &name_count);
            pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

            // extract all named capture groups
            for (uint32_t i = 0; i < name_count; ++i) {
                uint16_t group_number = (table[0] << 8) | table[1];
                const char* name = reinterpret_cast<const char*>(table + 2);
                std::optional<std::string> grp = group(group_number);
                result[name] = grp.has_value() ? grp : default_value;
                table += name_entry_size;
            }
            return result;
        }

        ///////////////////////////////
        ////    SYNTACTIC SUGAR    ////
        ///////////////////////////////

        /* Iterator over the non-empty capture groups that were found in the subject
        string.  Yields pairs where the first value is the group index and the second
        is the matching substring. */
        class GroupIter {
            friend Match;
            const Match& match;
            size_t index;
            size_t count;

            GroupIter(const Match& match, size_t index) :
                match(match), index(index), count(match.count())
            {}

            GroupIter(const Match& match) :
                match(match), index(match.count()), count(index)
            {}

        public:
            using iterator_category     = std::input_iterator_tag;
            using difference_type       = std::ptrdiff_t;
            using value_type            = std::pair<size_t, std::string>;
            using pointer               = value_type*;
            using reference             = value_type&;

            GroupIter(const GroupIter& other) :
                match(other.match), index(other.index), count(other.count)
            {}

            GroupIter(GroupIter&& other) :
                match(other.match), index(other.index), count(other.count)
            {}

            // GroupIter& operator=(const GroupIter& other) {
            //     if (&other != this) {
            //         match = other.match;
            //         index = other.index;
            //         count = other.count;
            //     }
            //     return *this;
            // }

            // GroupIter& operator=(GroupIter&& other) {
            //     if (&other != this) {
            //         match = other.match;
            //         index = other.index;
            //         count = other.count;
            //     }
            //     return *this;
            // }

            /* Dereference to access the current capture group. */
            inline value_type operator*() const {
                return {index, match[index].value()};  // current group is never empty
            }

            /* Dereference to access an attribute on the current capture group. */
            inline value_type operator->() const {
                return {index, match[index].value()};  // current group is never empty
            }

            /* Increment to advance to the next non-empty capture group. */
            inline GroupIter& operator++() {
                while (!match[++index].has_value() && index < count) {
                    // skip empty capture groups
                }
                return *this;
            }

            inline GroupIter operator++(int) {
                GroupIter copy = *this;
                ++(*this);
                return copy;
            }

            /* Equality comparison to terminate the sequence. */
            inline bool operator==(const GroupIter& other) const noexcept {
                return index == other.index;
            }

            /* Inequality comparison to terminate the sequence. */
            inline bool operator!=(const GroupIter& other) const noexcept {
                return index != other.index;
            }

        };

        inline GroupIter begin() const noexcept {
            return {*this, 0};
        }

        inline GroupIter end() const noexcept {
            return {*this};
        }

        /* Syntactic sugar for match.group(). */
        inline std::optional<std::string> operator[](size_t index) const {
            return group(index);
        }

        /* Syntactic sugar for match.group(). */
        inline std::optional<std::string> operator[](const char* name) const {
            return group(name);
        }

        /* Syntactic sugar for match.group(). */
        inline std::optional<std::string> operator[](const std::string& name) const {
            return group(name);
        }

        /* Syntactic sugar for match.group(). */
        inline std::optional<std::string> operator[](const std::string_view& name) const {
            return group(name);
        }

        /* Syntactic sugar for match.group() using an initializer list to extract
        multiple groups. */
        inline std::vector<std::optional<std::string>> operator[](
            const std::initializer_list<std::variant<size_t, const char*>>& groups
        ) {
            return group(groups);
        }

        /* Dump a string representation of a match object to an output stream. */
        inline friend std::ostream& operator<<(std::ostream& stream, const Match& match) {
            if (!match) {
                stream << "<No Match>";
                return stream;
            }

            std::optional<std::pair<size_t, size_t>> span = match.span();
            if (!span.has_value()) {
                stream << "<Match span=(), groups=()>";
                return stream;
            }

            stream << "<Match span=(" << span->first << ", " << span->second << "), groups=(";
            auto it = match.begin();
            auto end = match.end();
            if (it != end) {
                stream << (*it).second;
                ++it;
                while (it != end) {
                    stream << ", " << (*it).second;
                    ++it;
                }
            }
            stream << ")>";
            return stream;
        }

    };

    /* Evaluate the regular expression against a target string and return the first
    match.  Yields a (possibly false) Match struct that can be used to access and
    iterate over capture groups. */
    inline Match match(const std::string& subject) const {
        pcre2_match_data* data = pcre2_match_data_create_from_pattern(code.get(), nullptr);
        if (data == nullptr) {
            throw std::bad_alloc();
        }

        int rc = pcre2_match(
            code.get(),
            reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
            subject.length(),
            0,  // start at beginning of string
            0,  // use default options
            data,  // preallocated block for storing the result
            nullptr  // use default match context
        );

        // check for errors and dump match if found
        if (rc < 0) {
            pcre2_match_data_free(data);
            if (rc == PCRE2_ERROR_NOMATCH) {
                return {};
            } else {
                throw pcre_error(rc);
            }
        }
        return {code.get(), subject, data, true};
    }

    /* Evaluate the regular expression against a target string and return the first
    match.  Yields a (possibly false) Match struct that can be used to access and
    iterate over capture groups. */
    inline Match match(const std::string& subject, long long start, long long stop = -1) const {
        if (!normalize_indices(subject.length(), start, stop)) {
            return {};
        }

        pcre2_match_data* data = pcre2_match_data_create_from_pattern(code.get(), nullptr);
        if (data == nullptr) {
            throw std::bad_alloc();
        }

        int rc = pcre2_match(
            code.get(),
            reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
            stop,
            start,
            0,  // use default options
            data,  // preallocated block for storing the result
            nullptr  // use default match context
        );

        // check for errors and dump match if found
        if (rc < 0) {
            pcre2_match_data_free(data);
            if (rc == PCRE2_ERROR_NOMATCH) {
                return {};
            } else {
                throw pcre_error(rc);
            }
        }
        return {code.get(), subject, data, true};
    }

    ///////////////////////////////
    ////    ITERATIVE MATCH    ////
    ///////////////////////////////

    /* An iterator that yields successive matches within the target string. */
    class Iterator {
        friend Regex;
        pcre2_code* code;
        std::string subject;
        size_t stop;
        pcre2_match_data* match;

        /* Allocate shared match data struct and extract first match. */
        Iterator(
            pcre2_code* code,
            const std::string& subject,
            size_t start,
            size_t stop
        ) : code(code), subject(subject), stop(stop),
            match(pcre2_match_data_create_from_pattern(code, nullptr))
        {
            if (match == nullptr) {
                throw std::bad_alloc();
            }

            int rc = pcre2_match(
                code,
                reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                stop,
                start,
                0,  // use default options
                match,  // preallocated block for storing the result
                nullptr  // use default match context
            );

            if (rc < 0) {
                pcre2_match_data_free(match);
                if (rc == PCRE2_ERROR_NOMATCH) {
                    match = nullptr;
                } else {
                    throw pcre_error(rc);
                }
            }
        }

        /* Construct an empty iterator to terminate the sequence. */
        Iterator() : code(nullptr), subject(), stop(0), match(nullptr) {}

    public:

        /* Move constructor. */
        Iterator(Iterator&& other) noexcept :
            code(other.code), subject(std::move(other.subject)), stop(other.stop),
            match(other.match)
        {
            other.code = nullptr;
            other.match = nullptr;
        }

        /* Move assignment operator. */
        Iterator& operator=(Iterator&& other) noexcept {
            if (this == &other) {
                return *this;
            }
    
            code = other.code;
            subject = std::move(other.subject);
            stop = other.stop;

            pcre2_match_data* temp = match;
            match = other.match;
            other.match = nullptr;
            if (temp != nullptr) {
                pcre2_match_data_free(temp);
            }

            return *this;
        }

        /* Copy constructor/assignment operators deleted for simplicity. */
        Iterator(const Iterator&) = delete;
        Iterator operator=(const Iterator&) = delete;

        /* Deallocate shared match struct on deletion. */
        ~Iterator() noexcept {
            if (match != nullptr) {
                pcre2_match_data_free(match);
            }
        }

        /* Dereference to access the current match. */
        inline Regex::Match operator*() const {
            return {code, subject, match, false};
        }

        /* Increment to advance to the next match. */
        Iterator& operator++() {
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
            PCRE2_SIZE start = ovector[1];

            // account for zero-length matches
            if (start == ovector[0]) {
                ++start;
            }

            int rc = pcre2_match(
                code,
                reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                stop,
                start,  // start at end of previous match
                0,  // use default options
                match,  // preallocated block for storing the result
                nullptr  // use default match context
            );

            if (rc < 0) {
                pcre2_match_data* temp = match;
                match = nullptr;
                pcre2_match_data_free(temp);
                if (rc != PCRE2_ERROR_NOMATCH) {
                    throw pcre_error(rc);
                }
            }
            return *this;
        }

        /* End of sequence indicated by null match struct. */
        inline bool operator==(const Iterator& other) const noexcept {
            return match == other.match;
        }

        /* End of sequence indicated by null match struct. */
        inline bool operator!=(const Iterator& other) const noexcept {
            return match != other.match;
        }

    };

    /* A temporary proxy object that allows the result of `Regex::finditer()` to be
    self-iterable.  Note that once an iterator is retrieved from this object, the
    caller now owns the only valid instance of that iterator. */
    class IterProxy {
        friend Regex;
        Iterator first;
        Iterator second;

        IterProxy(Iterator&& first, Iterator&& second) :
            first(std::move(first)), second(std::move(second))
        {}

    public:
        IterProxy(const IterProxy&) = delete;
        IterProxy(IterProxy&&) = delete;
        IterProxy operator=(const IterProxy&) = delete;
        IterProxy operator=(IterProxy&&) = delete;

        inline Iterator begin() {
            return std::move(first);
        }

        inline Iterator end() {
            return std::move(second);
        }

    };

    /* Return an iterator that produces successive matches within the target string.
    Note that the return value is a temporary object that can only be iterated over
    once.  The caller must not store the result of this function. */
    inline IterProxy finditer(const std::string& subject) const {
        return {
            Iterator(
                code.get(),
                subject,
                0,
                subject.length()
            ),
            Iterator()
        };
    }

    /* Return an iterator that produces successive matches within the target string.
    Note that the return value is a temporary object that can only be iterated over
    once.  The caller must not store the result of this function. */
    inline IterProxy finditer(
        const std::string& subject,
        long long start,
        long long stop = -1
    ) const {
        if (!normalize_indices(subject.length(), start, stop)) {
            return {Iterator(), Iterator()};
        }
        return {
            Iterator(code.get(),
                subject,
                start,
                stop
            ),
            Iterator()
        };
    }

    /* Extract all matches of the regular expression against a target string.  Returns
    a vector containing the extracted strings. */
    inline std::vector<std::string> findall(const std::string& subject) const {
        std::vector<std::string> result;
        for (auto&& match : finditer(subject)) {
            result.push_back(match.group().value());
        }
        return result;
    }

    /* Extract all matches of the regular expression against a target string.  Returns
    a vector containing the extracted strings. */
    inline std::vector<std::string> findall(
        const std::string& subject,
        long long start,
        long long stop = -1
    ) const {
        std::vector<std::string> result;
        for (auto&& match : finditer(subject, start, stop)) {
            result.push_back(match.group().value());
        }
        return result;
    }

    /* Split the target string at each match of the regular expression.  Returns a
    vector containing the split substrings. */
    inline std::vector<std::string> split(
        const std::string& subject,
        size_t maxsplit = 0
    ) const {
        std::vector<std::string> result;
        size_t last = 0;
        if (maxsplit == 0) {
            for (auto&& match : finditer(subject)) {
                std::pair<size_t, size_t> span = match.span().value();
                result.push_back(subject.substr(last, span.first - last));
                last = span.second;
            }
        } else {
            size_t count = 0;
            for (auto&& match : finditer(subject)) {
                std::pair<size_t, size_t> span = match.span().value();
                result.push_back(subject.substr(last, span.first - last));
                last = span.second;
                if (++count == maxsplit) {
                    break;
                }
            }
        }
        result.push_back(subject.substr(last));
        return result;
    }

    /* Replace every occurrence of the regular expression in the target string.
    Returns a new string with the replaced result. */
    inline std::string sub(
        const std::string& replacement,
        const std::string& subject
    ) const {
        return subn(replacement, subject).first;
    }

    /* Replace a maximum number of occurrences of the regular expression in the target
    string.  Returns a new string with the replaced result. */
    inline std::string sub(
        const std::string& replacement,
        const std::string& subject,
        size_t count
    ) const {
        return subn(replacement, subject, count).first;
    }

    /* Replace every occurrence of the regular expression in the target string.
    Returns a (string, count) pair containing the new string and the number of
    replacements that were made. */
    inline std::pair<std::string, size_t> subn(
        const std::string& replacement,
        const std::string& subject
    ) const {
        // allocate a dynamic buffer to store result of substitution
        PCRE2_SIZE buflen = subject.length() * 2;
        std::vector<PCRE2_UCHAR> output(buflen);
        PCRE2_SIZE outlen = buflen;

        size_t retries = 0;
        while (true) {
            int n = pcre2_substitute(
                code.get(),
                reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                subject.length(),
                0,  // start at beginning of string
                PCRE2_SUBSTITUTE_GLOBAL,  // replace all matches
                nullptr,  // no match data needed
                nullptr,  // use default match context
                reinterpret_cast<PCRE2_SPTR>(replacement.c_str()),
                replacement.length(),
                output.data(),
                &outlen
            );

            // grow buffer and try again
            if (n == PCRE2_ERROR_NOMEMORY) {
                if (++retries > 1000) {
                    throw std::runtime_error("subn() growth limit exceeded");
                }
                buflen *= 2;
                output.resize(buflen);
                outlen = buflen;
                continue;
            }

            // raise any other error
            if (n < 0) {
                throw pcre_error(n);
            }

            // extract result and return as (string, count)
            return {std::string(reinterpret_cast<char*>(output.data()), outlen), n};
        }
    }

    /* Replace a maximum number of occurrences of the regular expression in the target
    string.  Returns a (string, count) pair containing the new string and the number of
    replacements that were made. */
    inline std::pair<std::string, size_t> subn(
        const std::string& replacement,
        const std::string& subject,
        size_t count
    ) const {
        // fastpath if count is zero
        if (count == 0) {
            return {subject, 0};
        }

        // allocate a dynamic buffer to store result of substitution
        PCRE2_SIZE buflen = subject.length() * 2;
        std::vector<PCRE2_UCHAR> output(buflen);
        PCRE2_SIZE bufpos = 0;
        PCRE2_SIZE remaining = buflen;

        // allocate a match struct to store the most recent match
        pcre2_match_data* match = pcre2_match_data_create_from_pattern(code.get(), nullptr);
        if (match == nullptr) {
            throw std::bad_alloc();
        }

        // substitute each match individually, writing the result to the output buffer
        // starting from the end of the previous match.  Break if we reach end of
        // string, maximum number of replacements, or need to grow the buffer
        size_t retries = 0;
        while (true) {
            size_t n = 0;
            size_t last = 0;

            int ret_val;
            while (n < count) {
                PCRE2_SIZE outlen = remaining;
                // PCRE2_SIZE delta = subject.length() - last;
                ret_val = pcre2_substitute(
                    code.get(),
                    reinterpret_cast<PCRE2_SPTR>(subject.c_str() + last),
                    subject.length() - last,
                    0,  // start at beginning of substring
                    0,  // replace only the first match
                    match,  // store most recent match
                    nullptr,  // use default match context
                    reinterpret_cast<PCRE2_SPTR>(replacement.c_str()),
                    replacement.length(),
                    output.data() + bufpos,
                    &outlen  // length written to output buffer
                );

                // NOTE: pcre2_substitute writes the rest of the string to the buffer
                // after every substitution.  This remaining space might be relevant
                // in future iterations, so we need to adjust the buffer position to
                // chop it off.
                last += pcre2_get_ovector_pointer(match)[1];
                outlen -= subject.length() - last;
                bufpos += outlen;
                remaining -= outlen;

                // break if there are no further matches or buffer is full
                if (ret_val == 0 || ret_val == PCRE2_ERROR_NOMEMORY) {
                    break;
                } else if (ret_val < 0) {  // error not related to buffer size
                    pcre2_match_data_free(match);
                    throw pcre_error(ret_val);
                }
                ++n;
            }

            // if buffer error, grow buffer and try again
            if (ret_val == PCRE2_ERROR_NOMEMORY) {
                if (++retries > 1000) {
                    pcre2_match_data_free(match);
                    throw std::runtime_error("subn() growth limit exceeded");
                }
                buflen *= 2;
                output.resize(buflen);
                bufpos = 0;
                remaining = buflen;
                continue;
            }

            // extract result from buffer and return as (string, count)
            pcre2_match_data_free(match);
            return {
                std::string(
                    reinterpret_cast<char*>(output.data()),
                    bufpos + (subject.length() - last)
                ),
                n
            };
        }
    }

    /* Print a string representation of a Regex pattern to an output stream. */
    inline friend std::ostream& operator<<(std::ostream& stream, const Regex& regex) {
        stream << "<Regex: " << regex.pattern() << ">";
        return stream;
    }

};


}  // namespace bertrand


#endif  // BERTRAND_REGEX_H
