#ifndef BERTRAND_REGEX_H
#define BERTRAND_REGEX_H

#include <optional>
#include <string>

#define PCRE2_CODE_UNIT_WIDTH 8  // for UTF8/ASCII strings
#include "pcre2.h"


namespace bertrand{



#include <iostream>


class Regex {
    pcre2_code* code = nullptr;

public:

    /* Compile the pattern into a PCRE2 regular expression. */
    Regex(const char* pattern) {
        int err;
        PCRE2_SIZE err_offset;

        code = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(pattern),
            PCRE2_ZERO_TERMINATED,
            0,  // use default options
            &err,  // to extract errors
            &err_offset,
            nullptr  // use default compile context
        );

        if (code == nullptr) {
            PCRE2_UCHAR buffer[256];
            pcre2_get_error_message(err, buffer, sizeof(buffer));
            throw std::runtime_error(
                "[invalid regex] compilation failed at offset " +
                std::to_string(err_offset) + ": " + reinterpret_cast<char*>(buffer)
            );
        }
    }

    Regex(const std::string& pattern) : Regex(pattern.c_str()) {}

    /* Move constructor. */
    Regex(Regex&& other) noexcept : code(other.code) {
        other.code = nullptr;
    }

    /* Move assignment operator. */
    Regex& operator=(Regex&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (code != nullptr) {
            pcre2_code_free(code);
        }
        code = other.code;
        other.code = nullptr;
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

    class Match {
        friend Regex;
        pcre2_code* code;
        std::string subject;
        pcre2_match_data* match;

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
        ) : code(code), subject(subject),
            match(pcre2_match_data_create_from_pattern(code, nullptr))
        {
            int rc = pcre2_match(
                code,
                reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                subject.length(),
                start_index,
                0,  // use default options
                match,  // preallocated block for storing the result
                nullptr  // use default match context
            );

            if (rc < 0) {
                pcre2_match_data* temp = match;
                match = nullptr;
                pcre2_match_data_free(temp);
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
            if (match == nullptr) {
                return 0;
            }
            return pcre2_get_ovector_count(match);
        }

        /* Get the start index of a numbered capture group. */
        size_t start(size_t index = 0) const {
            if (match == nullptr || index >= pcre2_get_ovector_count(match)) {
                std::ostringstream msg;
                msg << "no capture group at index " << index;
                throw std::out_of_range(msg.str());
            }
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
            return ovector[2 * index];
        }

        /* Get the start index of a numbered capture group. */
        size_t stop(size_t index = 0) const {
            if (match == nullptr || index >= pcre2_get_ovector_count(match)) {
                std::ostringstream msg;
                msg << "no capture group at index " << index;
                throw std::out_of_range(msg.str());
            }
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
            return ovector[2 * index + 1];
        }

        /* Extract a numbered capture group. */
        std::optional<std::string> group(size_t index = 0) const {
            if (match == nullptr || index >= pcre2_get_ovector_count(match)) {
                return std::nullopt;
            }
            PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
            size_t start = ovector[2 * index];
            size_t end = ovector[2 * index + 1];
            return std::string(subject.c_str() + start, end - start);
        }

        /* Get the start index of a named capture group. */
        std::optional<size_t> start(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return start(number);
        }

        /* Get the start index of a named capture group. */
        std::optional<size_t> stop(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return stop(number);
        }

        /* Extract a named capture group. */
        std::optional<std::string> group(const char* name) const {
            int number = group_number(name);
            if (number == PCRE2_ERROR_NOSUBSTRING) {
                return std::nullopt;
            }
            return group(number);
        }


        // TODO: extract a map of all named groups to their substrings

        // TODO: make this iterable?

    };

    /* Match the regular expression against a target string, returning a Match object
    encapsulating the results. */
    Match match(const std::string& subject) const noexcept {
        return {code, subject, 0};
    }

    ///////////////////////////////
    ////    ITERATIVE MATCH    ////
    ///////////////////////////////

    class Iterator {
        friend Regex;

    public:

    };


    // TODO: return an iterator over matches in a string

    Iterator match_iter(const std::string& subject) const noexcept {
        return {};
    }

    std::vector<std::string> match_all(const std::string& subject) const noexcept {
        // pcre2_match_data* data = pcre2_match_data_create_from_pattern
        // reuse the same data pointer for the whole operation.
        return {};
    }


};



}  // namespace bertrand


#endif  // BERTRAND_REGEX_H
