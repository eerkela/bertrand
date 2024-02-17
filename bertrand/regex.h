#ifndef BERTRAND_REGEX_H
#define BERTRAND_REGEX_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

#define PCRE2_CODE_UNIT_WIDTH 8  // for UTF8/ASCII strings
#include "pcre2.h"


namespace bertrand{


/* A thin wrapper around a compiled PCRE2 regular expression that provides a more
user-friendly, python-like interface. */
class Regex {
    std::string _pattern;
    uint32_t _flags;
    pcre2_code* code = nullptr;

public:

    /* Enumerated bitset describing compilation flags for PCRE2 regular expressions. */
    enum : uint32_t {
        DEFAULT = 0,
        JIT = PCRE2_JIT_COMPLETE,
        NO_UTF_CHECK = PCRE2_NO_UTF_CHECK,
        IGNORE_CASE = PCRE2_CASELESS,
        IGNORE_WHITESPACE = PCRE2_EXTENDED
    };

    /* Compile the pattern into a PCRE2 regular expression. */
    template <typename T>
    Regex(const T& pattern, uint32_t flags = DEFAULT) : _pattern(pattern), _flags(flags) {
        int err;
        PCRE2_SIZE err_offset;

        code = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(_pattern.c_str()),
            _pattern.size(),
            flags & ~(JIT),  // compile without JIT flags
            &err,
            &err_offset,
            nullptr  // use default compile context
        );

        if (code == nullptr) {
            PCRE2_UCHAR pcre_err[256];
            pcre2_get_error_message(err, pcre_err, sizeof(pcre_err));
            std::ostringstream err_msg;
            err_msg << "[invalid regex] " << pcre_err << "\n\n    ";
            err_msg << _pattern << "\n    ";
            err_msg << std::string(err_offset, ' ') << "^";
            throw std::runtime_error(err_msg.str());
        }

        if (flags & JIT) {
            int rc = pcre2_jit_compile(code, JIT);
            if (rc < 0 && rc != PCRE2_ERROR_JIT_BADOPTION) {
                pcre2_code_free(code);
                code = nullptr;
                std::ostringstream msg;
                msg << "pcre2_jit_compile() returned error code " << rc;
                throw std::runtime_error(msg.str());
            }
        }
    }

    // TODO: account for all fields during Move constructor

    /* Move constructor. */
    Regex(Regex&& other) noexcept :
        _pattern(std::move(other._pattern)), _flags(other._flags), code(other.code)
    {
        other.code = nullptr;
    }

    /* Move assignment operator. */
    Regex& operator=(Regex&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        _pattern = std::move(other._pattern);
        _flags = other._flags;
        pcre2_code* temp = code;
        code = other.code;
        other.code = nullptr;
        if (temp != nullptr) {
            pcre2_code_free(code);
        }
        return *this;
    }

    /* Copy constructors deleted for simplicity. */
    Regex(const Regex&) = delete;
    Regex operator=(const Regex&) = delete;

    /* Free the PCRE2 code object on deletion. */
    ~Regex() noexcept {
        if (code != nullptr) {
            pcre2_code_free(code);
        }
    }

    ///////////////////////////
    ////    BASIC MATCH    ////
    ///////////////////////////

    // TODO: separate pcre2_match_data_create_from_pattern from Match object so that
    // it can be reused during iterative matching.

    /* A match object allowing easy access to capture groups and match information. */
    class Match {
        friend Regex;
        pcre2_code* code;
        pcre2_match_data* match;
        std::string subject;
        PCRE2_SIZE* ovector;
        size_t _count;

        using GroupVec = std::vector<std::optional<std::string>>;
        using GroupDict = std::unordered_map<std::string, std::optional<std::string>>;
        using Initializer = std::variant<long long, std::string>;

        /* Get the group number associated with a named capture group. */
        inline int group_number(const char* name) const {
            return pcre2_substring_number_from_name(
                code,
                reinterpret_cast<PCRE2_SPTR>(name)
            );
        }

        Match(
            pcre2_code* code,
            const std::string& subject,
            size_t start_index
        ) : code(code), match(pcre2_match_data_create_from_pattern(code, nullptr)),
            subject(subject), ovector(nullptr), _count(0)
        {
            // populate the match object
            int rc = pcre2_match(
                code,
                reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                subject.length(),
                start_index,
                0,  // use default options
                match,  // preallocated block for storing the result
                nullptr  // use default match context
            );

            // check for errors and dump match if found
            if (rc < 0) {
                pcre2_match_data* temp = match;
                match = nullptr;
                pcre2_match_data_free(temp);
            } else {
                ovector = pcre2_get_ovector_pointer(match);
                _count = pcre2_get_ovector_count(match);
            }
        }

    public:
        Match() = default;

        ~Match() noexcept {
            if (match != nullptr) {
                pcre2_match_data_free(match);
            }
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
        inline size_t start(size_t index = 0) const {
            if (match == nullptr || index >= count()) {
                std::ostringstream msg;
                msg << "no capture group at index " << index;
                throw std::out_of_range(msg.str());
            }
            return ovector[2 * index];
        }

        /* Get the start index of a numbered capture group. */
        inline size_t stop(size_t index = 0) const {
            if (match == nullptr || index >= count()) {
                std::ostringstream msg;
                msg << "no capture group at index " << index;
                throw std::out_of_range(msg.str());
            }
            return ovector[2 * index + 1];
        }

        /* Return a pair containing both the start and end indices of a numbered
        capture group. */
        inline std::pair<size_t, size_t> span(size_t index = 0) const {
            return {start(index), stop(index)};
        }

        /* Extract a numbered capture group. */
        std::optional<std::string> group(size_t index = 0) const {
            if (match == nullptr || index >= count()) {
                return std::nullopt;
            }
            size_t start = ovector[2 * index];
            size_t end = ovector[2 * index + 1];
            if (start == end) {
                return std::nullopt;
            } else {
                return std::string(subject.c_str() + start, end - start);
            }
        }

        /* Extract several capture groups at once. */
        template <typename... Args, std::enable_if_t<(sizeof...(Args) > 1), int> = 0>
        inline GroupVec group(Args&&... args) const {
            return {group(std::forward<Args>(args))...};
        }

        /* Extract all capture groups into a std::vector. */
        inline GroupVec groups() const {
            GroupVec result;
            size_t n = count();
            result.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                result.push_back(this->group(i));
            }
            return result;
        }

        ////////////////////////////////////
        ////    NAMED CAPTURE GROUPS    ////
        ////////////////////////////////////

        /* Get a capture group's name from its index number. */
        std::optional<std::string> groupname(size_t index) const {
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
        inline std::optional<size_t> start(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return start(number);
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> start(const std::string& name) const {
            return start(name.c_str());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> start(const std::string_view& name) const {
            return start(name.data());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return stop(number);
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const std::string& name) const {
            return stop(name.c_str());
        }

        /* Get the start index of a named capture group. */
        inline std::optional<size_t> stop(const std::string_view& name) const {
            return stop(name.data());
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const char* name) const
            -> std::optional<std::pair<size_t, size_t>>
        {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return std::make_pair(start(number), stop(number));
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const std::string& name) const
            -> std::optional<std::pair<size_t, size_t>>
        {
            return span(name.c_str());
        }

        /* Return a pair containing both the start and end indices of a named
        capture group. */
        inline auto span(const std::string_view& name) const
            -> std::optional<std::pair<size_t, size_t>>
        {
            return span(name.data());
        }

        /* Extract a named capture group. */
        std::optional<std::string> group(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return group(number);
        }

        /* Extract a named capture group. */
        std::optional<std::string> group(const std::string& name) const {
            return group(name.c_str());
        }

        /* Extract a named capture group. */
        std::optional<std::string> group(const std::string_view& name) const {
            return group(name.data());
        }

        /* Extract all named capture groups into a std::unordered_map. */
        GroupDict groupdict() const {
            GroupDict result;
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
                result[name] = group(group_number);
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
        class Iterator {
            friend Match;
            const Match& match;
            size_t index;
            size_t count;

            Iterator(const Match& match, size_t index) :
                match(match), index(index), count(match.count())
            {}

            Iterator(const Match& match) :
                match(match), index(match.count()), count(index)
            {}

        public:
            using iterator_category     = std::forward_iterator_tag;
            using difference_type       = std::ptrdiff_t;
            using value_type            = std::pair<size_t, std::string>;
            using pointer               = value_type*;
            using reference             = value_type&;

            Iterator(const Iterator&) = default;
            Iterator(Iterator&&) = default;

            /* Dereference to access the current capture group. */
            inline std::pair<size_t, std::string> operator*() const {
                return {index, match[index].value()};  // current group is never empty
            }

            /* Increment to advance to the next non-empty capture group. */
            inline Iterator& operator++() {
                while (!match[++index].has_value() && index < count) {
                    // skip empty capture groups
                }
                return *this;
            }

            /* Equality comparison to terminate the sequence. */
            inline bool operator==(const Iterator& other) const noexcept {
                return index == other.index;
            }

            /* Inequality comparison to terminate the sequence. */
            inline bool operator!=(const Iterator& other) const noexcept {
                return index != other.index;
            }

        };

        Iterator begin() const noexcept {
            return {*this, 0};
        }

        Iterator end() const noexcept {
            return {*this};
        }

        /* Syntactic sugar for match.group(). */
        template <typename T>
        inline std::optional<std::string> operator[](T&& index) const {
            return group(std::forward<T>(index));
        }

        /* Syntactic sugar for match.group() using initializer list syntax to extract
        multiple groups. */
        GroupVec operator[](const std::initializer_list<Initializer>& groups) {
            GroupVec result;
            result.reserve(groups.size());
            for (auto&& item : groups) {
                std::visit([&result, this](auto&& arg) {
                    result.push_back(group(arg));
                }, item);
                // result.push_back(group(item));
            }
            return result;
        }

    };

    /* Match the regular expression against the start of a target string, returning a
    Match object encapsulating the results. */
    Match match(const std::string& subject) const noexcept {
        return {code, subject, 0};
    }

    /* Search a string to find the first match for this regular expression.  The only
    difference between this and match() is that for this function, the expression can
    match anywhere in the string. */
    Match search(const std::string& subject) const noexcept {
        // TODO: go back to calculating the match at this level and then passing it
        // into the match constructor.
        return {code, subject, 0};
    }

    ///////////////////////////////
    ////    ITERATIVE MATCH    ////
    ///////////////////////////////

    class Iterator {
        friend Regex;

    public:

        auto operator*() const {
            return 0;
        }

        Iterator& operator++() {
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept {
            return true;
        }

        bool operator!=(const Iterator& other) const noexcept {
            return false;
        }

    };

    std::vector<std::string> findall(const std::string& subject) const noexcept {
        // pcre2_match_data* data = pcre2_match_data_create_from_pattern
        // reuse the same data pointer for the whole operation.
        return {};
    }

    Iterator finditer(const std::string& subject) const noexcept {
        return {};
    }

    ////////////////////
    ////    MISC    ////
    ////////////////////

    std::vector<std::string> split(const std::string& subject) const noexcept {
        return {};
    }

    std::string sub(const std::string& subject, const std::string& replacement) const noexcept {
        return {};
    }

    /* Get the pattern used to construct the regular expression. */
    inline std::string pattern() const {
        return _pattern;
    }

    /* Get the PCRE flags that were used to build the regular expression. */
    inline uint32_t flags() const noexcept {
        return _flags;
    }

    /* Check whether a particular flag is set for the compiled regular expression. */
    inline bool has_flag(uint32_t flag) const noexcept {
        return (flags() & flag) != 0;
    }

};


/* Dump a string representation of a Regex object to an output stream. */
std::ostream& operator<<(std::ostream& stream, const Regex& regex) {
    stream << "<Regex: " << regex.pattern() << ">";
    return stream;
}


/* Dump a string representation of a match object to an output stream. */
std::ostream& operator<<(std::ostream& stream, const Regex::Match& match) {
    if (!match) {
        stream << "<No Match>";
        return stream;
    }

    stream << "<Match: " << match.group().value() << ", groups=(";
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


}  // namespace bertrand


#endif  // BERTRAND_REGEX_H
