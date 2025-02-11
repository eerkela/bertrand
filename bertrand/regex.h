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

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"


/// TODO: compile-time regular expressions are in fact possible using PCRE.

/// https://github.com/hanickadot/compile-time-regular-expressions


/// TODO: Should use explicit code unit widths for PCRE2 interactions, since that
/// allows me to link against all 3 versions of the library, and include automatic
/// support for u16string and u32strings.  That means I can't use generic PCRE2 types


namespace bertrand {


namespace impl {
    struct regex_tag {};
    struct regex_match_tag {};

    /* Convert a PCRE2 error code into a corresponding runtime error with a proper
    traceback. */
    inline RuntimeError pcre_error(int err_code) noexcept {
        PCRE2_UCHAR buffer[256];
        int rc = pcre2_get_error_message(err_code, buffer, sizeof(buffer));
        if (rc < 0) {  // error while retrieving error
            return RuntimeError(
                "pcre2_get_error_message() returned error code " +
                std::to_string(rc)
            );
        }
        return RuntimeError(std::string(reinterpret_cast<char*>(buffer), rc));
    }

    /* An iterator that yields successive, non-overlapping matches of a regular
    expression against a given input string. */
    template <typename CRTP>
    struct regex_match : regex_match_tag {
    protected:
        struct Deleter {
            static void operator()(pcre2_match_data* data) noexcept {
                pcre2_match_data_free(data);
            }
        };

        std::shared_ptr<pcre2_code> m_code;
        std::unique_ptr<pcre2_match_data, Deleter> m_data;
        size_t m_start = 0;
        size_t m_stop = 0;

        /// TODO: matches should have another method that gets the whole substring
        /// match, without any extra optional wrappers.  That could be returned from
        /// a findall() method as another transform_view.

        /* A regex match object allowing easy access to capture groups and match
        information for a given input string. */
        struct match {
            struct iterator;

        private:
            friend regex_match;
            friend iterator;
            const CRTP* m_self = nullptr;
            pcre2_code* m_code = nullptr;
            PCRE2_SIZE* m_ovector = nullptr;
            size_t m_count = 0;
            std::string_view m_prev;

            /* Get the group number associated with a named capture group. */
            size_t groupnum(const char* name) const noexcept {
                int number = pcre2_substring_number_from_name_8(
                    m_code,
                    reinterpret_cast<PCRE2_SPTR8>(name)
                );
                if (number == PCRE2_ERROR_NOSUBSTRING) {
                    throw KeyError(name);
                }
                return static_cast<size_t>(number);
            }

            match(
                std::string_view prev,
                const CRTP* self,
                pcre2_code* code,
                pcre2_match_data* data
            ) :
                m_self(self),
                m_code(code),
                m_ovector(pcre2_get_ovector_pointer(data)),
                m_count(pcre2_get_ovector_count(data)),
                m_prev(prev)
            {}

        public:
            match(std::string_view prev = "") noexcept : m_prev(prev) {};

            /* Returns true if the match is valid. */
            [[nodiscard]] explicit operator bool() const noexcept {
                return m_count;
            }

            /* Return the full string that was supplied to the `regex.match()`
            method. */
            [[nodiscard]] std::string_view string() const noexcept {
                return m_self->subject;
            }

            /* Return the start index that was supplied to the `regex.match()`
            method.  Defaults to zero. */
            [[nodiscard]] size_t pos() const noexcept {
                return m_self->m_start;
            }

            /* Return the stop index that was supplied to the `regex.match()`
            method.  Defaults to the size of the input string. */
            [[nodiscard]] size_t endpos() const noexcept {
                return m_self->m_stop;
            }

            /* Return the substring immediately preceding this regular expression
            match.  This is used to implement efficient, regex-based string splitting
            with the full context of the match. */
            [[nodiscard]] std::string_view split() const noexcept {
                return m_prev;
            }

            /* Get the start index of the matched substring. */
            [[nodiscard]] size_t start() const noexcept {
                return m_ovector[0];
            }

            /* Get the start index of a numbered capture group or nullopt if the
            numbered group did not participate in the match.  Throws an `IndexError`
            if the index is out of bounds for the regular expression. */
            [[nodiscard]] std::optional<size_t> start(size_t index) const {
                if (index >= m_count) {
                    throw IndexError(std::to_string(index));
                }
                size_t i = m_ovector[index * 2];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the start index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] std::optional<size_t> start(const char* name) const {
                size_t i = m_ovector[groupnum(name) * 2];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the start index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] std::optional<size_t> start(std::string_view name) const {
                size_t i = m_ovector[groupnum(name.data()) * 2];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the stop index of the matched substring. */
            [[nodiscard]] size_t stop() const noexcept {
                return m_ovector[1];
            }

            /* Get the stop index of a numbered capture group or nullopt if the
            numbered group did not participate in the match.  Throws an `IndexError`
            if the index is out of bounds for the regular expression. */
            [[nodiscard]] std::optional<size_t> stop(size_t index) const {
                if (index >= m_count) {
                    throw IndexError(std::to_string(index));
                }
                size_t i = m_ovector[index * 2 + 1];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the stop index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] std::optional<size_t> stop(const char* name) const {
                size_t i = m_ovector[groupnum(name) * 2 + 1];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the stop index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] std::optional<size_t> stop(std::string_view name) const {
                size_t i = m_ovector[groupnum(name.data()) * 2 + 1];
                return i == PCRE2_UNSET ? std::nullopt : std::make_optional(i);
            }

            /* Get the start and stop indices of the matched substring as a pair. */
            [[nodiscard]] std::pair<size_t, size_t> span() const noexcept {
                return {start(), stop()};
            }

            /* Return a pair containing both the start and stop indices of a numbered
            capture group or nullopt if the numbered group did not participate in the
            match.  Throws an `IndexError` if the index is out of bounds for the
            regular expression. */
            [[nodiscard]] auto span(size_t index) const
                -> std::optional<std::pair<size_t, size_t>>
            {
                if (index >= m_count) {
                    throw IndexError(std::to_string(index));
                }
                size_t x = index * 2;
                size_t i = m_ovector[x];
                return i == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::make_pair(i, m_ovector[x + 1]));
            }

            /* Get the stop index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] auto span(const char* name) const
                -> std::optional<std::pair<size_t, size_t>>
            {
                size_t x = groupnum(name) * 2;
                size_t i = m_ovector[x];
                return i == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::make_pair(i, m_ovector[x + 1]));
            }

            /* Get the stop index of a named capture group or nullopt if the group
            did not participate in the match.  Throws a `KeyError` if the named capture
            group is not recognized. */
            [[nodiscard]] auto span(std::string_view name) const
                -> std::optional<std::pair<size_t, size_t>>
            {
                size_t x = groupnum(name.data()) * 2;
                size_t i = m_ovector[x];
                return i == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::make_pair(i, m_ovector[x + 1]));
            }

            /* Extract the matched substring. */
            [[nodiscard]] std::string_view group() const noexcept {
                return {m_self->subject.data() + start(), stop() - start()};
            }

            /* Extract a numbered capture group or nullopt if the numbered group did
            not participate in the match.  Throws an `IndexError` if the index is out
            of bounds for the regular expression. */
            [[nodiscard]] auto group(size_t index) const
                -> std::optional<std::string_view>
            {
                if (index >= m_count) {
                    throw IndexError(std::to_string(index));
                }
                size_t x = index * 2;
                size_t start = m_ovector[x];
                return start == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::string_view{
                        m_self->subject.data() + start, m_ovector[x + 1] - start
                    });
            }

            /* Extract a named capture group or nullopt if the named group is not
            present. */
            [[nodiscard]] auto group(const char* name) const noexcept
                -> std::optional<std::string_view>
            {
                size_t x = groupnum(name) * 2;
                size_t start = m_ovector[x];
                return start == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::string_view{
                        m_self->subject.data() + start, m_ovector[x + 1] - start
                    });
            }

            /* Extract a named capture group or nullopt if the named group is not
            present. */
            [[nodiscard]] auto group(std::string_view name) const noexcept
                -> std::optional<std::string_view>
            {
                size_t x = groupnum(name.data()) * 2;
                size_t start = m_ovector[x];
                return start == PCRE2_UNSET ?
                    std::nullopt :
                    std::make_optional(std::string_view{
                        m_self->subject.data() + start, m_ovector[x + 1] - start
                    });
            }

            /* Extract several capture groups at once, returning a fixed-size array
            aligned to the argument list, which may be unpacked using structured
            bindings. */
            template <typename... Args>
                requires (sizeof...(Args) > 1 && ((
                    std::convertible_to<Args, size_t> ||
                    std::convertible_to<Args, const char*> ||
                    std::convertible_to<Args, std::string_view>
                ) && ...))
            [[nodiscard]] auto group(Args&&... args) const noexcept {
                return [this]<size_t... Is>(
                    std::index_sequence<Is...>,
                    auto&&... args
                ) noexcept {
                    return std::array<std::optional<std::string_view>, sizeof...(Args)>{
                        group(std::forward<Args>(args))...
                    };
                }(std::index_sequence_for<Args...>{}, std::forward<Args>(args)...);
            }

            /* Syntactic sugar for match.group(). */
            template <typename... Args>
                requires ((
                    std::convertible_to<Args, size_t> ||
                    std::convertible_to<Args, const char*> ||
                    std::convertible_to<Args, std::string_view>
                ) && ...)
            [[nodiscard]] auto operator[](Args&&... args) const noexcept {
                return [this]<size_t... Is>(
                    std::index_sequence<Is...>,
                    auto&&... args
                ) noexcept {
                    return std::array<std::optional<std::string_view>, sizeof...(Args)>{
                        group(std::forward<Args>(args))...
                    };
                }(std::index_sequence_for<Args...>{}, std::forward<Args>(args)...);
            }

            /// TODO: if there was an easy way to determine the total number of
            /// capture groups at compile time, then I could return a raw array here.

            /* Extract all capture groups into a `std::vector`.  Any groups that did
            not participate in the match will be returned as empty optionals. */
            [[nodiscard]] auto groups() const noexcept
                -> std::vector<std::optional<std::string_view>>
            {
                std::vector<std::optional<std::string_view>> result;
                result.reserve(m_count);
                for (size_t i = 0, n = m_count * 2; i < n; i += 2) {
                    size_t start = m_ovector[i];
                    result.emplace_back(start == PCRE2_UNSET ?
                        std::nullopt :
                        std::make_optional(std::string_view{
                            m_self->subject.data() + start, m_ovector[i + 1] - start
                        })
                    );
                }
                return result;
            }

            /* Extract all capture groups into a `std::vector`.  Any groups that did
            not participate in the match will be replaced with the default value. */
            [[nodiscard]] auto groups(std::string_view default_value) const noexcept
                -> std::vector<std::string_view>
            {
                std::vector<std::string_view> result;
                result.reserve(m_count);
                for (size_t i = 0, n = m_count * 2; i < n; i += 2) {
                    size_t start = m_ovector[i];
                    result.emplace_back(start == PCRE2_UNSET ?
                        default_value :
                        std::string_view{
                            m_self->subject.data() + start, m_ovector[i + 1] - start
                        }
                    );
                }
                return result;
            }

            /// TODO: if there was an easy way to extract the name of each capture
            /// group in the template pattern at compile time, then I could return a
            /// minimal perfect hash table here, which would be ideal for performance.

            /* Extract all named capture groups into a `std::unordered_map` */
            [[nodiscard]] auto groupdict() const noexcept
                -> std::unordered_map<std::string_view, std::optional<std::string_view>>
            {
                PCRE2_SPTR table;
                uint32_t name_count;
                uint32_t name_entry_size;
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMETABLE, &table);
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMECOUNT, &name_count);
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

                std::unordered_map<std::string_view, std::optional<std::string_view>> result;
                result.reserve(name_count);

                for (uint32_t i = 0; i < name_count; ++i) {
                    size_t offset = 0;
                    size_t group_number = table[offset++];
                    if constexpr (PCRE2_CODE_UNIT_WIDTH == 8) {
                        group_number <<= 8;
                        group_number += table[offset++];
                    }
                    std::string_view group_name {
                        reinterpret_cast<const char*>(table + offset)
                    };
                    size_t x = group_number * 2;
                    size_t start = m_ovector[x];
                    result.emplace(
                        group_name,
                        start == PCRE2_UNSET ?
                            std::nullopt :
                            std::make_optional(std::string_view{
                                m_self->subject.data() + start,
                                m_ovector[x + 1] - start
                            }
                        )
                    );
                }

                return result;
            }

            /* Extract all named capture groups into a `std::unordered_map` */
            [[nodiscard]] auto groupdict(std::string_view default_value) const noexcept
                -> std::unordered_map<std::string_view, std::string_view>
            {
                PCRE2_SPTR table;
                uint32_t name_count;
                uint32_t name_entry_size;
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMETABLE, &table);
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMECOUNT, &name_count);
                pcre2_pattern_info(m_code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

                std::unordered_map<std::string_view, std::string_view> result;
                result.reserve(name_count);

                for (uint32_t i = 0; i < name_count; ++i) {
                    size_t offset = 0;
                    size_t group_number = table[offset++];
                    if constexpr (PCRE2_CODE_UNIT_WIDTH == 8) {
                        group_number <<= 8;
                        group_number += table[offset++];
                    }
                    std::string_view group_name {
                        reinterpret_cast<const char*>(table + offset)
                    };
                    size_t x = group_number * 2;
                    size_t start = m_ovector[x];
                    result.emplace(
                        group_name,
                        start == PCRE2_UNSET ?
                            default_value :
                            std::string_view{
                                m_self->subject.data() + start,
                                m_ovector[x + 1] - start
                            }
                    );
                }

                return result;
            }

            /// TODO: maybe this can be a bidirectional iterator?  That eliminates the
            /// need for lastindex/lastgroup, and gives you even finer control.
            /// Actually, it might even be a random access iterator, which would allow
            /// you to efficiently walk over just the subset of matching capture groups.

            /* Iterator over all of the capture groups that participated in the match.
            Yields pairs where the first value is the group index and the second is the
            matching substring. */
            struct iterator {
                using iterator_category     = std::input_iterator_tag;
                using difference_type       = std::ptrdiff_t;
                using value_type            = std::pair<size_t, std::string_view>;
                using pointer               = value_type*;
                using reference             = value_type&;

            private:
                friend match;
                const match* m_match = nullptr;
                size_t m_index = 0;
                size_t m_count = 0;
                value_type m_current;
    
                iterator(const match* match, size_t count) :
                    m_match(match),
                    m_count(count),
                    m_current([this] noexcept -> value_type {
                        while (m_index < m_count) {
                            size_t x = m_index * 2;
                            size_t start = m_match->m_ovector[x];
                            if (start != PCRE2_UNSET) {
                                return {
                                    m_index,
                                    std::string_view{
                                        m_match->signature.data() + start,
                                        m_match->m_ovector[1] - start
                                    }
                                };
                            }
                            ++m_index;
                        }
                        return {};
                    }())
                {}
    
            public:
                iterator() = default;
    
                [[nodiscard]] value_type& operator*() noexcept {
                    return m_current;
                }

                [[nodiscard]] const value_type& operator*() const noexcept {
                    return m_current;
                }

                [[nodiscard]] value_type* operator->() noexcept {
                    return &m_current;
                }
    
                [[nodiscard]] const value_type* operator->() const noexcept {
                    return &m_current;
                }
    
                iterator& operator++() noexcept {
                    while (++m_index < m_count) {
                        size_t x = m_index * 2;
                        size_t start = m_match->m_ovector[x];
                        if (start != PCRE2_UNSET) {
                            m_current = {
                                m_index,
                                std::string_view{
                                    m_match->signature.data() + start,
                                    m_match->m_ovector[x + 1] - start
                                }
                            };
                            break;
                        }
                    }
                    return *this;
                }
    
                [[nodiscard]] iterator operator++(int) noexcept {
                    iterator copy = *this;
                    ++(*this);
                    return copy;
                }
    
                [[nodiscard]] friend bool operator==(
                    const iterator& self,
                    sentinel
                ) noexcept {
                    return self.m_index >= self.m_count;
                }
    
                [[nodiscard]] friend bool operator==(
                    sentinel,
                    const iterator& self
                ) noexcept {
                    return self.m_index >= self.m_count;
                }
    
                [[nodiscard]] friend bool operator!=(
                    const iterator& self,
                    sentinel
                ) noexcept {
                    return self.m_index < self.m_count;
                }
    
                [[nodiscard]] friend bool operator!=(
                    sentinel,
                    const iterator& self
                ) noexcept {
                    return self.m_index < self.m_count;
                }
            };
    
            [[nodiscard]] iterator begin() const noexcept { return {m_self, m_count}; }
            [[nodiscard]] iterator cbegin() const noexcept { return begin(); }
            [[nodiscard]] sentinel end() const noexcept { return {}; }
            [[nodiscard]] sentinel cend() const noexcept { return {}; }
    
            /* Dump a string representation of a match object to an output stream. */
            friend std::ostream& operator<<(
                std::ostream& stream,
                const match& self
            ) noexcept {
                constexpr static_str no_match = "<No Match>";
                constexpr static_str empty_match = "<Match span=(), groups=()>";

                if (!self) {
                    return stream.write(no_match.data(), no_match.size());
                }

                auto span = self.span();
                if (!span) {
                    return stream.write(empty_match.data(), empty_match.size());
                }

                auto [start, stop] = *span;
                stream << "<Match span=(" << start << ", " << stop << "), groups=(";
                auto it = self.begin();
                auto end = self.end();
                if (it != end) {
                    stream << it->second;
                    while (++it != end) {
                        stream << ", " << it->second;
                    }
                }
                stream << ")>";
                return stream;
            }
        } m_match;

        friend match;

        regex_match() = default;
        regex_match(
            std::shared_ptr<pcre2_code> code,
            size_t start,
            size_t stop
        ) :
            m_code(code),
            m_data(
                pcre2_match_data_create_from_pattern(
                    m_code.get(),
                    nullptr  // use default memory manager
                ),
                Deleter{}
            ),
            m_start(start),
            m_stop(stop),
            m_match([](
                const CRTP* self,
                pcre2_code* code,
                pcre2_match_data* data
            ) -> match {
                if (!self->m_data) {
                    throw MemoryError();
                }
                int rc = pcre2_match(
                    self->m_code.get(),
                    reinterpret_cast<PCRE2_SPTR>(self->subject.data()),
                    self->m_stop,
                    self->m_start,
                    0,  // use default options
                    self->m_data.get(),  // preallocated block for the result
                    nullptr  // use default match context
                );
                if (rc < 0) {
                    if (rc == PCRE2_ERROR_NOMATCH) {
                        return {std::string_view{
                            self->subject.data() + self->m_start,
                            self->m_stop - self->m_start
                        }};
                    }
                    throw impl::pcre_error(rc);
                }
                PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data);
                return {
                    std::string_view{self->subject.data() + self->m_start, ovector[0]},
                    self,
                    code,
                    data
                };
            }(static_cast<const CRTP*>(this), m_code.get(), m_data.get()))
        {}

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = match;
        using reference = match&;
        using pointer = match*;

        [[nodiscard]] match& operator*() noexcept { return m_match; }
        [[nodiscard]] const match& operator*() const noexcept { return m_match; }
        [[nodiscard]] match* operator->() noexcept { return &m_match; }
        [[nodiscard]] const match* operator->() const noexcept { return &m_match; }

        CRTP& operator++() noexcept {
            CRTP* self = static_cast<CRTP*>(this);
            if (m_match) {
                PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(m_data.get());
                size_t start = ovector[1];
                int rc = pcre2_match(
                    m_code.get(),
                    reinterpret_cast<PCRE2_SPTR>(self->subject.data()),
                    m_stop,
                    start,
                    0,  // use default options
                    m_data.get(),  // preallocated block for the result
                    nullptr  // use default match context
                );
                if (rc < 0) {
                    if (rc == PCRE2_ERROR_NOMATCH) {
                        m_match = {std::string_view{
                            self->subject.data() + start,
                            m_stop - start
                        }};
                    } else {
                        throw impl::pcre_error(rc);
                    }
                }
                m_match.m_prev = std::string_view{
                    self->subject.data() + start,
                    ovector[0] - start
                };
            } else {
                m_data.reset();
            }
            return *self;
        }

        void operator++(int) noexcept {
            ++*this;
        }

        [[nodiscard]] friend bool operator==(const regex_match& self, sentinel) noexcept {
            return self.m_data == nullptr;
        }

        [[nodiscard]] friend bool operator==(sentinel, const regex_match& self) noexcept {
            return self.m_data == nullptr;
        }

        [[nodiscard]] friend bool operator!=(const regex_match& self, sentinel) noexcept {
            return self.m_data != nullptr;
        }

        [[nodiscard]] friend bool operator!=(sentinel, const regex_match& self) noexcept {
            return self.m_data != nullptr;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(m_match);
        }

        /* A shim class that is returned by `begin()` in order to allow use in
        range-based for loops.  The iterator trivially forwards to the regex range
        itself. */
        struct iterator {
        private:
            friend regex_match;
            CRTP* self = nullptr;

            iterator(CRTP* self) : self(self) {}

        public:
            using iterator_category = regex_match::iterator_category;
            using difference_type = regex_match::difference_type;
            using value_type = regex_match::value_type;
            using reference = regex_match::reference;
            using pointer = regex_match::pointer;

            iterator() = default;

            [[nodiscard]] value_type& operator*() noexcept { return **self; }
            [[nodiscard]] const value_type& operator*() const noexcept { return **self; }
            [[nodiscard]] value_type* operator->() noexcept { return &**self; }
            [[nodiscard]] const value_type* operator->() const noexcept { return &**self; }

            iterator& operator++() noexcept {
                ++*self;
                return *this;
            }

            [[nodiscard]] iterator operator++(int) noexcept {
                iterator copy = *this;
                ++(*this);
                return copy;
            }

            [[nodiscard]] friend bool operator==(const iterator& self, sentinel) noexcept {
                return *self.self == sentinel{};
            }

            [[nodiscard]] friend bool operator==(sentinel, const iterator& self) noexcept {
                return *self.self == sentinel{};
            }

            [[nodiscard]] friend bool operator!=(const iterator& self, sentinel) noexcept {
                return *self.self != sentinel{};
            }

            [[nodiscard]] friend bool operator!=(sentinel, const iterator& self) noexcept {
                return *self.self != sentinel{};
            }
        };

        [[nodiscard]] iterator begin() noexcept { return static_cast<CRTP&>(*this); }
        [[nodiscard]] iterator cbegin() noexcept { return begin(); }
        [[nodiscard]] sentinel end() noexcept { return {}; }
        [[nodiscard]] sentinel cend() noexcept { return {}; }
    };

    /* A regular expression iterator that owns the underlying input string, possibly
    requiring an extra allocation. */
    struct regex_owning_match : regex_match<regex_owning_match> {
        std::string subject;

        regex_owning_match() = default;
        regex_owning_match(
            std::string subject,
            std::shared_ptr<pcre2_code> code,
            size_t start,
            size_t stop
        ) :
            regex_match<regex_owning_match>(code, start, stop),
            subject(std::move(subject))
        {}
    };

    /* A regular expression iterator that references an external input string, whose
    lifetime is guaranteed to exceed that of the iterator itself. */
    struct regex_borrowed_match : regex_match<regex_borrowed_match> {
        std::string_view subject;

        regex_borrowed_match() = default;
        regex_borrowed_match(
            std::string_view subject,
            std::shared_ptr<pcre2_code> code,
            size_t start,
            size_t stop
        ) :
            regex_match<regex_borrowed_match>(code, start, stop),
            subject(std::move(subject))
        {}
    };

}


namespace meta {

    template <typename T>
    concept regex = inherits<T, impl::regex_tag>;

    template <typename T>
    concept regex_match = inherits<T, impl::regex_match_tag>;

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
template <static_str Pattern>
struct regex : impl::regex_tag {
    using borrowed_match = impl::regex_borrowed_match;
    using owning_match = impl::regex_owning_match;

private:
    std::shared_ptr<pcre2_code> m_code;

    static bool normalize_indices(size_t size, ssize_t& start, ssize_t& stop) {
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
        return stop >= start;
    }

    template <typename... Args>
    struct finditer_args {
        template <typename = void>
        static constexpr bool value = false;
        template <>
        static constexpr bool value<std::void_t<
            decltype(std::declval<regex&>().finditer(std::declval<Args>()...))
        >> = true;
        static constexpr bool enable = value<>;
    };

public:
    /* Compile the pattern into a PCRE2 regular expression. */
    template <typename T>
    regex() : m_code([] {
        int err;
        PCRE2_SIZE err_offset;

        // compile the pattern
        std::shared_ptr<pcre2_code> result {
            pcre2_compile(
                reinterpret_cast<PCRE2_SPTR>(Pattern.data()),
                Pattern.size(),
                PCRE2_JIT_COMPLETE,
                &err,
                &err_offset,
                nullptr  // use default compile context
            ),
            pcre2_code_free
        };

        // pretty print errors
        if (!result) {
            PCRE2_UCHAR pcre_err[256];
            pcre2_get_error_message(err, pcre_err, sizeof(pcre_err));
            throw RuntimeError(
                "[invalid regex] " + std::string(reinterpret_cast<char*>(pcre_err)) +
                "\n\n    " + Pattern + "\n    " + std::string(err_offset, ' ') +
                "^"
            );
        }

        // JIT compile the expression if possible
        int rc = pcre2_jit_compile(result.get(), PCRE2_JIT_COMPLETE);
        if (rc < 0 && rc != PCRE2_ERROR_JIT_BADOPTION) {
            throw impl::pcre_error(rc);
        }
        return result;
    }()) {}

    /* Get the pattern used to construct the regular expression as read-only memory. */
    [[nodiscard]] static constexpr const auto& pattern() noexcept {
        return Pattern;
    }

    /* Get a capture group's name from its index number.  Returns an empty string if
    the capture group is anonymous, and throws an `IndexError` if the index is out of
    range. */
    [[nodiscard]] std::string_view groupname(size_t index) const {
        PCRE2_SPTR table;
        uint32_t name_count;
        uint32_t name_entry_size;
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMETABLE, &table);
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMECOUNT, &name_count);
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

        for (uint32_t i = 0; i < name_count; ++i) {
            size_t offset = 0;
            size_t group_number = table[offset++];
            if constexpr (PCRE2_CODE_UNIT_WIDTH == 8) {
                // if 8-bit PCRE2 library is used, then first 2 bytes are devoted to
                // the index, not just 1
                group_number <<= 8;
                group_number |= table[offset++];
            }
            if (group_number == index) {
                size_t idx = index * 2;
                return {reinterpret_cast<const char*>(table + offset)};
            }
            table += name_entry_size;
        }

        throw IndexError(std::to_string(index));
    }

    /* Get the number of capture groups in the regular expression. */
    [[nodiscard]] size_t groupcount() const noexcept {
        if (m_code) {
            uint32_t count;
            pcre2_pattern_info(m_code.get(), PCRE2_INFO_CAPTURECOUNT, &count);
            return count;
        }
        return 0;
    }

    /* Get a dictionary mapping capture group names to their corresponding indices. */
    [[nodiscard]] auto groupindex() const {
        std::unordered_map<std::string_view, size_t> result;
        if (m_code == nullptr) {
            return result;
        }

        // retrieve name table
        PCRE2_SPTR table;
        uint32_t name_count;
        uint32_t name_entry_size;
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMETABLE, &table);
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMECOUNT, &name_count);
        pcre2_pattern_info(m_code.get(), PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);

        // extract all named capture groups
        for (uint32_t i = 0; i < name_count; ++i) {
            uint16_t group_number = (table[0] << 8) | table[1];
            const char* name = reinterpret_cast<const char*>(table + 2);
            auto [it, inserted] = result.try_emplace(name, group_number);
            if (!inserted) {
                throw RuntimeError(
                    "duplicate group name: " + std::string(it->first)
                );
            }
            table += name_entry_size;
        }
        return result;
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <size_t N> requires (N > 0)
    [[nodiscard]] borrowed_match match(const char (&subject)[N]) const {
        return {
            std::string_view(subject, N - 1),
            m_code,
            0,
            N - 1
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <meta::static_str Str>
    [[nodiscard]] borrowed_match match(const Str& subject) const {
        return {
            std::string_view(subject),
            m_code,
            0,
            subject.size()
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <meta::inherits<std::string_view> Str>
    [[nodiscard]] borrowed_match match(Str&& subject) const {
        return {
            std::forward<Str>(subject),
            m_code,
            0,
            subject.size()
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <std::convertible_to<std::string> Str>
        requires (
            !meta::string_literal<Str> &&
            !meta::static_str<Str> &&
            !meta::inherits<std::string_view, Str>
        )
    [[nodiscard]] owning_match match(Str&& subject) const {
        return {
            std::forward<Str>(subject),
            m_code,
            0,
            subject.size()
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <size_t N> requires (N > 0)
    [[nodiscard]] borrowed_match match(
        const char (&subject)[N],
        ssize_t start,
        ssize_t stop = -1
    ) const {
        if (!normalize_indices(N - 1, start, stop)) {
            return {};
        }
        return {
            std::string_view(subject, N - 1),
            m_code,
            static_cast<size_t>(start),
            static_cast<size_t>(stop)
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <meta::static_str Str>
    [[nodiscard]] borrowed_match match(
        const Str& subject,
        ssize_t start,
        ssize_t stop = -1
    ) const {
        if (!normalize_indices(subject.size(), start, stop)) {
            return {};
        }
        return {
            std::string_view(subject),
            m_code,
            static_cast<size_t>(start),
            static_cast<size_t>(stop)
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <meta::inherits<std::string_view> Str>
    [[nodiscard]] borrowed_match match(
        Str&& subject,
        ssize_t start,
        ssize_t stop = -1
    ) const {
        if (!normalize_indices(subject.size(), start, stop)) {
            return {};
        }
        return {
            std::forward<Str>(subject),
            m_code,
            static_cast<size_t>(start),
            static_cast<size_t>(stop)
        };
    }

    /* Evaluate the regular expression against a target string, returning an iterator
    that yields successive, non-overlapping matches from left to right.  The iterator
    dereferences to a match struct that can be used to access and iterate over capture
    groups, as well as split the string into intervening substrings.  The final match
    will always be empty, and can be used to access the remaining substring.  Empty
    matches (and the iterators that reference them) evaluate to false under boolean
    logic.

    If the input is given as a string literal, a `bertrand::static_str` instance, or a
    `std::string_view`, the iterator will store a non-owning view of the input string,
    which must be guaranteed to outlive the iterator itself.  Otherwise, the iterator
    will own a local copy of the input string. */
    template <std::convertible_to<std::string> Str>
        requires (
            !meta::string_literal<Str> &&
            !meta::static_str<Str> &&
            !meta::inherits<std::string_view, Str>
        )
    [[nodiscard]] owning_match match(
        Str&& subject,
        ssize_t start,
        ssize_t stop = -1
    ) const {
        if (!normalize_indices(subject.size(), start, stop)) {
            return {};
        }
        return {
            std::forward<Str>(subject),
            m_code,
            static_cast<size_t>(start),
            static_cast<size_t>(stop)
        };
    }


    /// TODO: add in another `split()` method that returns a transform_view of the
    /// match() range that just calls each match's .split() method.


    /// TODO: replacements become significantly easier with an approach like this




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




    /// TODO: split(), sub(), subn()


    /// TODO: index into the regex with an integer or string to get the substring
    /// corresponding to that group in the pattern.  Can possibly iterate over that
    /// as well.


    /* Print a string representation of a Regex pattern to an output stream. */
    friend std::ostream& operator<<(std::ostream& stream, const regex&) {
        constexpr static_str out = "<Regex: " + Pattern + ">";
        return stream.write(out.data(), out.size());
    }
};


}  // namespace bertrand


#endif  // BERTRAND_REGEX_H
