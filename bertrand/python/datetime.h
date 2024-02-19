#ifndef BERTRAND_PYTHON_DATETIME_H
#define BERTRAND_PYTHON_DATETIME_H

#include <chrono>

#include <Python.h>
#include <datetime.h>  // Python datetime library
#include <pybind11/chrono.h>

#include "common.h"
#include "int.h"
#include "float.h"
#include "regex.h"
#include "str.h"


namespace bertrand {
namespace py {


namespace impl {

    /* Import Python's datetime API at the C level. */
    static const bool DATETIME_IMPORTED = []() -> bool {
        if (Py_IsInitialized()) {
            PyDateTime_IMPORT;
        }
        return PyDateTimeAPI != nullptr;
    }();

    /* Extract the datetime.date type into a pybind11 handle. */
    static const Handle PyDate_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return Handle(reinterpret_cast<PyObject*>(PyDateTimeAPI->DateType));
        }
        return nullptr;
    }();

    /* Extract the datetime.time type into a pybind11 handle. */
    static const Handle PyTime_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return Handle(reinterpret_cast<PyObject*>(PyDateTimeAPI->TimeType));
        }
        return nullptr;
    }();

    /* Extract the datetime.datetime type into a pybind11 handle. */
    static const Handle PyDateTime_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return Handle(reinterpret_cast<PyObject*>(PyDateTimeAPI->DateTimeType));
        }
        return nullptr;
    }();

    /* Extract the datetime.timedelta type into a pybind11 handle. */
    static const Handle PyDelta_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return Handle(reinterpret_cast<PyObject*>(PyDateTimeAPI->DeltaType));
        }
        return nullptr;
    }();

    /* Extract the datetime.tzinfo type into a pybind11 handle. */
    static const Handle PyTZInfo_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return Handle(reinterpret_cast<PyObject*>(PyDateTimeAPI->TZInfoType));
        }
        return nullptr;
    }();

    /* Import Python's zoneinfo package. */
    static const Handle zoneinfo = []() -> Handle {
        if (Py_IsInitialized()) {
            return py::import("zoneinfo");
        }
        return nullptr;
    }();

    /* Extract the ZoneInfo type for use in type checks. */
    static const Handle PyZoneInfo_Type = []() -> Handle {
        if (zoneinfo.ptr() != nullptr) {
            return zoneinfo.attr("ZoneInfo");
        }
        return nullptr;
    }();

    /* Import 3rd party tzlocal package to get system's local timezone.  NOTE: this is
    installed alongside bertrand as a dependency. */
    static const Handle tzlocal = []() -> Handle {
        if (Py_IsInitialized()) {
            return py::import("tzlocal");
        }
        return nullptr;
    }();

    /* Import 3rd party dateutil.parser module to parse strings.  NOTE: this is installed
    alongside bertrand as a dependency. */
    static const Handle dateutil_parser = []() -> Handle {
        if (Py_IsInitialized()) {
            return py::import("dateutil.parser");
        }
        return nullptr;
    }();

    /* Enumerated tags holding numeric conversions for datetime and timedelta types. */
    class TimeUnits {
        friend class py::Timedelta;
        struct UnitTag {};

        template <typename T, typename U>
        using enable_cpp = std::enable_if_t<std::is_arithmetic_v<T>, U>;

        template <typename T, typename U>
        using enable_py = std::enable_if_t<
            std::is_base_of_v<pybind11::int_, T> ||
            std::is_base_of_v<pybind11::float_, T>,
            U
        >;

        template <typename From>
        class Unit : public UnitTag {
        protected:

            inline static PyObject* timedelta_from_DSU(int d, int s, int us) {
                PyObject* result = PyDelta_FromDSU(d, s, us);
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        public:

            /* Convert from this unit to the target unit, given as a tag struct.  Units
            can override this method to account for irregular unit lengths. */
            template <typename T, typename To>
            static constexpr T convert(T value, const To&) {
                if constexpr (std::is_same_v<From, To>) {
                    return value;
                } else {
                    return value * (static_cast<double>(From::scale) / To::scale);
                }
            }

        };

        struct Nanoseconds : public Unit<Nanoseconds> {
            static constexpr long long scale = 1;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / Days::scale;
                int s = value / Seconds::scale;
                int us = value / Microseconds::scale;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Int factor(1000);
                PyObject* result = PyObject_CallFunctionObjArgs(
                    impl::PyDelta_Type.ptr(),
                    zero.ptr(),
                    zero.ptr(),
                    (value / factor).ptr(),
                    nullptr
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Microseconds : public Unit<Microseconds> {
            static constexpr long long scale = 1000;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / (Days::scale / scale);
                int s = value / (Seconds::scale / scale);
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                PyObject* result = PyObject_CallFunctionObjArgs(
                    impl::PyDelta_Type.ptr(),
                    zero.ptr(),
                    zero.ptr(),
                    value.ptr(),
                    nullptr
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Milliseconds : public Unit<Milliseconds> {
            static constexpr long long scale = 1000 * Microseconds::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / (Days::scale / scale);
                int s = value / (Seconds::scale / scale);
                value -= s * (Seconds::scale / scale);
                value *= scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Int factor(1000);
                PyObject* result = PyObject_CallFunctionObjArgs(
                    impl::PyDelta_Type.ptr(),
                    zero.ptr(),
                    (value / factor).ptr(),
                    nullptr
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Seconds : public Unit<Seconds> {
            static constexpr long long scale = 1000 * Milliseconds::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / (Days::scale / scale);
                int s = value;
                value -= s;
                value *= scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                PyObject* result = PyObject_CallFunctionObjArgs(
                    impl::PyDelta_Type.ptr(),
                    zero.ptr(),
                    value.ptr(),
                    nullptr
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Minutes : public Unit<Minutes> {
            static constexpr long long scale = 60 * Seconds::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / (Days::scale / scale);
                value -= d * (Days::scale / scale);
                value *= scale / Seconds::scale;
                int s = value;
                value -= s;
                value *= Seconds::scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Int factor(60);
                PyObject* result = PyObject_CallFunctionObjArgs(
                    impl::PyDelta_Type.ptr(),
                    zero.ptr(),
                    (value * factor).ptr(),
                    nullptr
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Hours : public Unit<Hours> {
            static constexpr long long scale = 60 * Minutes::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value / (Days::scale / scale);
                value -= d * (Days::scale / scale);
                value *= scale / Seconds::scale;
                int s = value;
                value -= s;
                value *= Seconds::scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Int factor(24);
                PyObject* result = PyObject_CallOneArg(
                    impl::PyDelta_Type.ptr(),
                    (value / factor).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Days : public Unit<Days> {
            static constexpr long long scale = 24 * Hours::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                int d = value;
                value -= d;
                value *= scale / Seconds::scale;
                int s = value;
                value -= s;
                value *= Seconds::scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                PyObject* result = PyObject_CallOneArg(
                    impl::PyDelta_Type.ptr(),
                    value.ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Weeks : public Unit<Weeks> {
            static constexpr long long scale = 7 * Days::scale;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                value *= scale / Days::scale;
                int d = value;
                value -= d;
                value *= Days::scale / Seconds::scale;
                int s = value;
                value -= s;
                value *= Seconds::scale / Microseconds::scale;
                int us = value;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Int factor(7);
                PyObject* result = PyObject_CallOneArg(
                    impl::PyDelta_Type.ptr(),
                    (value * factor).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Months : public Unit<Months> {
            // defining a month as 365.2425 / 12 = 30.436875 days.
            static constexpr long long scale = 30 * Days::scale + 37746000000000;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                double temp = value * 30.436875; 
                int d = temp;
                temp -= d;
                temp *= Days::scale / Seconds::scale;
                int s = temp;
                temp -= s;
                temp *= Seconds::scale / Microseconds::scale;
                int us = temp;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Float factor(30.436875);
                PyObject* result = PyObject_CallOneArg(
                    impl::PyDelta_Type.ptr(),
                    (value * factor).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

        struct Years : public Unit<Years> {
            // defining a year as 365.2425 days (proleptic Gregorian calendar)
            static constexpr long long scale = 365 * Days::scale + 20952000000000;

            template <typename T>
            static enable_cpp<T, PyObject*> to_timedelta(T value) {
                double temp = value * 365.2425; 
                int d = temp;
                temp -= d;
                temp *= Days::scale / Seconds::scale;
                int s = temp;
                temp -= s;
                temp *= Seconds::scale / Microseconds::scale;
                int us = temp;
                return Unit::timedelta_from_DSU(d, s, us);
            }

            template <typename T>
            static enable_py<T, PyObject*> to_timedelta(const T& value) {
                static const Int zero(0);
                static const Float factor(365.2425);
                PyObject* result = PyObject_CallOneArg(
                    impl::PyDelta_Type.ptr(),
                    (value * factor).ptr()
                );
                if (result == nullptr) {
                    throw error_already_set();
                }
                return result;
            }

        };

    public:
        static constexpr Years Y{};
        static constexpr Months M{};
        static constexpr Weeks W{};
        static constexpr Days D{};
        static constexpr Hours h{};
        static constexpr Minutes m{};
        static constexpr Seconds s{};
        static constexpr Milliseconds ms{};
        static constexpr Microseconds us{};
        static constexpr Nanoseconds ns{};

        template <typename T>
        static constexpr bool is_unit = std::is_base_of_v<UnitTag, T>;
    };

    /* A collection of regular expressions for parsing timedelta strings given in
    either abbreviated natural text ("1h22m", "1 hour, 22 minutes", "1.2 seconds" etc.)
    or ISO clock format ("01:22:00", "1:22", "00:01:22:00.000"), with precision up to
    years and months and down to nanoseconds. */
    class TimeParser {

        // capture groups for natural text
        static constexpr std::string_view td_Y{
            R"((?P<Y>[\d.]+)(y(ear|r)?s?))"
        };
        static constexpr std::string_view td_M{
            R"((?P<M>[\d.]+)((month|mnth|mth|mo)s?))"
        };
        static constexpr std::string_view td_W{
            R"((?P<W>[\d.]+)(w(eek|k)?s?))"
        };
        static constexpr std::string_view td_D{
            R"((?P<D>[\d.]+)(d(ay|y)?s?))"
        };
        static constexpr std::string_view td_h{
            R"((?P<h>[\d.]+)(h(our|r)?s?))"
        };
        static constexpr std::string_view td_m{
            R"((?P<m>[\d.]+)(minutes?|mins?|m(?!s)))"
        };
        static constexpr std::string_view td_s{
            R"((?P<s>[\d.]+)(s(econd|ec)?s?))"
        };
        static constexpr std::string_view td_ms{
            R"((?P<ms>[\d.]+)((millisecond|millisec|msec|mil)s?|ms))"
        };
        static constexpr std::string_view td_us{
            R"((?P<us>[\d.]+)((microsecond|microsec|micro)s?|u(sec)?s?))"
        };
        static constexpr std::string_view td_ns{
            R"((?P<ns>[\d.]+)(n(anosecond|anosec|ano|second|sec)s?))"
        };

        // capture groups for clock format
        static constexpr std::string_view td_day_clock{
            R"((?P<D>\d+):(?P<h>\d{1,2}):(?P<m>\d{1,2}):(?P<s>\d{1,2}(\.\d+)?))"
        };
        static constexpr std::string_view td_hour_clock{
            R"((?P<h>\d+):(?P<m>\d{1,2}):(?P<s>\d{1,2}(\.\d+)?))"
        };
        static constexpr std::string_view td_minute_clock{
            R"((?P<m>\d{1,2}):(?P<s>\d{1,2}(\.\d+)?))"
        };
        static constexpr std::string_view td_second_clock{
            R"(:(?P<s>\d{1,2}(\.\d+)?))"
        };

        // match an optional, comma-separated pattern
        static std::string opt(const std::string_view& pat) {
            return "(" + std::string(pat) + "[,/]?)?";
        }

        // compilation flags for regular expressions
        static constexpr uint32_t flags = Regex::IGNORE_CASE | Regex::JIT;

    public:
        enum class TimedeltaPattern {
            ABBREV,
            HCLOCK,
            DCLOCK,
            MCLOCK,
            SCLOCK
        };

        /* Match a timedelta string against all regular expressions and return a pair
        containing the first result and a tag indicating the kind of pattern that was
        matched.  All patterns populate the same capture groups, whose labels match the
        units listed in impl::TimeUnits. */
        static auto match_timedelta(const std::string& string)
            -> std::pair<Regex::Match, TimedeltaPattern>
        {
            static const Regex abbrev(
                "^(?P<sign>[+-])?" + opt(td_Y) + opt(td_M) + opt(td_W) +
                opt(td_D) + opt(td_h) + opt(td_m) + opt(td_s) +
                opt(td_ms) + opt(td_us) + opt(td_ns) + "$",
                flags
            );
            static const Regex hclock(
                "^(?P<sign>[+-])?" + opt(td_W) + opt(td_D) +
                std::string(td_hour_clock) + "$",
                flags
            );
            static const Regex dclock(
                "^(?P<sign>[+-])?" + std::string(td_day_clock) + "$",
                flags
            );
            static const Regex mclock(
                "^(?P<sign>[+-])?" + std::string(td_minute_clock) + "$",
                flags
            );
            static const Regex sclock(
                "^(?P<sign>[+-])?" + std::string(td_second_clock) + "$",
                flags
            );

            // remove whitespace from the string before matching
            static const Regex whitespace(R"(\s+)", flags);
            std::string stripped(whitespace.sub("", string));

            Regex::Match result = abbrev.match(stripped);
            if (result) {
                return {std::move(result), TimedeltaPattern::ABBREV};
            }

            result = hclock.match(stripped);
            if (result) {
                return {std::move(result), TimedeltaPattern::HCLOCK};
            }

            result = dclock.match(stripped);
            if (result) {
                return {std::move(result), TimedeltaPattern::DCLOCK};
            }

            result = mclock.match(stripped);
            if (result) {
                return {std::move(result), TimedeltaPattern::MCLOCK};
            }

            return {sclock.match(stripped), TimedeltaPattern::SCLOCK};
        };

        /* Convert a timedelta string into a datetime.timedelta object. */
        static PyObject* to_timedelta(const std::string& string, bool as_hours = false) {
            using OptStr = std::optional<std::string>;

            auto [match, pattern] = match_timedelta(string);
            if (!match) {
                throw ValueError("could not parse timedelta string: '" + string + "'");
            }

            int sign = 1;
            if (OptStr sign_str = match["sign"]) {
                if (sign_str.value() == "-") {
                    sign = -1;
                }
            }

            // set DSU values based on the pattern
            double A = 0, B = 0, C = 0;
            switch (pattern) {
                case (TimedeltaPattern::ABBREV):
                    if (OptStr O = match["Y"]) {
                        A += TimeUnits::Y.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["M"]) {
                        A += TimeUnits::M.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["W"]) {
                        A += TimeUnits::W.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["D"]) {
                        A += TimeUnits::D.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["h"]) {
                        B += TimeUnits::h.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["m"]) {
                        B += TimeUnits::m.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["s"]) {
                        B += TimeUnits::s.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["ms"]) {
                        C += TimeUnits::ms.convert(std::stod(*O), TimeUnits::us) * sign;
                    }
                    if (OptStr O = match["us"]) {
                        C += TimeUnits::us.convert(std::stod(*O), TimeUnits::us) * sign;
                    }
                    if (OptStr O = match["ns"]) {
                        C += TimeUnits::ns.convert(std::stod(*O), TimeUnits::us) * sign;
                    }
                    break;
                case (TimedeltaPattern::HCLOCK):
                    if (OptStr O = match["W"]) {
                        A += TimeUnits::W.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["D"]) {
                        A += TimeUnits::D.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["h"]) {
                        B += TimeUnits::h.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["m"]) {
                        B += TimeUnits::m.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["s"]) {
                        B += TimeUnits::s.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    break;
                case (TimedeltaPattern::DCLOCK):
                    if (OptStr O = match["D"]) {
                        A += TimeUnits::D.convert(std::stod(*O), TimeUnits::D) * sign;
                    }
                    if (OptStr O = match["h"]) {
                        B += TimeUnits::h.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["m"]) {
                        B += TimeUnits::m.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    if (OptStr O = match["s"]) {
                        B += TimeUnits::s.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    break;
                case (TimedeltaPattern::MCLOCK):
                    // strings of the form "1:22" are ambiguous.  Do they represent
                    // hours and minutes or minutes and seconds?  By default, we assume
                    // the latter, but if `as_hours=true`, we reverse that assumption
                    if (as_hours) {
                        if (OptStr O = match["m"]) {
                            B += TimeUnits::h.convert(std::stod(*O), TimeUnits::s) * sign;
                        }
                        if (OptStr O = match["s"]) {
                            B += TimeUnits::m.convert(std::stod(*O), TimeUnits::s) * sign;
                        }
                    } else {
                        if (OptStr O = match["m"]) {
                            B += TimeUnits::m.convert(std::stod(*O), TimeUnits::s) * sign;
                        }
                        if (OptStr O = match["s"]) {
                            B += TimeUnits::s.convert(std::stod(*O), TimeUnits::s) * sign;
                        }
                    }
                    break;
                case (TimedeltaPattern::SCLOCK):
                    if (OptStr O = match["s"]) {
                        B += TimeUnits::s.convert(std::stod(*O), TimeUnits::s) * sign;
                    }
                    break;
                default:
                    throw ValueError("unrecognized timedelta pattern");
            }

            // demote fractional units to the next lower unit
            int d = A;
            B += TimeUnits::D.convert(A - d, TimeUnits::s);
            int s = B;
            C += TimeUnits::s.convert(B - s, TimeUnits::us);
            int us = C;

            // create a new timedelta object
            PyObject* result = PyDelta_FromDSU(d, s, us);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }

    };

}  // namespace impl


/* New subclass of pybind11::object that represents a datetime.timedelta object at the
Python level. */
class Timedelta :
    public pybind11::object,
    public impl::FullCompare<Timedelta>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<Timedelta>;

    static PyObject* convert_to_timedelta(PyObject* obj) {
        throw TypeError("cannot convert object to datetime.timedelta");
    }

public:
    CONSTRUCTORS(Timedelta, PyDelta_Check, convert_to_timedelta);
    using Units = impl::TimeUnits;

    /* Default constructor.  Initializes to an empty delta. */
    inline Timedelta() : Timedelta(0, 0, 0) {}

    /* Construct a Python timedelta from a number of days, seconds, and microseconds. */
    inline explicit Timedelta(int days, int seconds, int microseconds) : Base([&] {
        PyObject* result = PyDelta_FromDSU(days, seconds, microseconds);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Python timedelta from a number of arbitrary units. */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    inline explicit Timedelta(T offset, const Unit& = Units::s) : Base([&offset] {
        static_assert(
            Units::is_unit<Unit>,
            "unit must be one of py::Timedelta::Units::Y, ::M, ::W, ::D, ::h, ::m, "
            "::s, ::ms, ::us, or ::ns"
        );
        return Unit::to_timedelta(offset);
    }(), stolen_t{}) {}

    /* Construct a timedelta from a number of units, where the number is given as a
    python integer or float. */
    template <typename T, typename Unit, Units::enable_py<T, int> = 0>
    inline explicit Timedelta(const T& offset, const Unit& = Units::s) : Base([&offset] {
        static_assert(
            Units::is_unit<Unit>,
            "unit must be one of py::Timedelta::Units::Y, ::M, ::W, ::D, ::h, ::m, "
            "::s, ::ms, ::us, or ::ns"
        );
        return Unit::to_timedelta(offset);
    }(), stolen_t{}) {}

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    inline explicit Timedelta(
        const char* string,
        bool as_hours = false
    ) : Base([&string, &as_hours] {
        return impl::TimeParser::to_timedelta(string, as_hours);
    }(), stolen_t{}) {}

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    inline explicit Timedelta(
        const std::string& string,
        bool as_hours = false
    ) : Base([&string, &as_hours] {
        return impl::TimeParser::to_timedelta(string, as_hours);
    }(), stolen_t{}) {}

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    inline explicit Timedelta(
        const std::string_view& string,
        bool as_hours = false
    ) : Base([&string, &as_hours] {
        return impl::TimeParser::to_timedelta(std::string(string), as_hours);
    }(), stolen_t{}) {}

    //////////////////////
    ////    STATIC    ////
    //////////////////////

    /* Get the minimum possible timedelta. */
    inline static const Timedelta& min() {
        static Timedelta min(impl::PyDelta_Type.attr("min"), stolen_t{});
        return min;
    }

    /* Get the maximum possible timedelta. */
    inline static const Timedelta& max() {
        static Timedelta max(impl::PyDelta_Type.attr("max"), stolen_t{});
        return max;
    }

    /* Get the smallest representable timedelta. */
    inline static const Timedelta& resolution() {
        static Timedelta resolution(impl::PyDelta_Type.attr("resolution"), stolen_t{});
        return resolution;
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the number of days in the timedelta. */
    inline int days() const {
        return PyDateTime_DELTA_GET_DAYS(this->ptr());
    }

    /* Get the number of seconds in the timedelta. */
    inline int seconds() const {
        return PyDateTime_DELTA_GET_SECONDS(this->ptr());
    }

    /* Get the number of microseconds in the timedelta. */
    inline int microseconds() const {
        return PyDateTime_DELTA_GET_MICROSECONDS(this->ptr());
    }

    /* Get the total number of seconds in the timedelta as a double. */
    inline double total_seconds() const {
        double result = days() * (24 * 60 * 60);
        result += seconds();
        result += microseconds() * 1e-6;
        return result;
    }

    /* Get the total number of microseconds in the timedelta as an integer.  Note: this
    can overflow if the timedelta is very large (>290,000 years). */
    inline long long total_microseconds() const {
        return (
            (Units::Days::scale / 1000) * days() +
            (Units::Seconds::scale / 1000) * seconds() +
            microseconds()
        );
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;

    /* Equivalent to Python `timedelta + timedelta`. */
    inline Timedelta operator+(const Timedelta& other) const {
        PyObject* result = PyNumber_Add(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `timedelta - timedelta`. */
    inline Timedelta operator-(const Timedelta& other) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `timedelta * number`. */
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    inline Timedelta operator*(T number) const {
        PyObject* result = PyNumber_Multiply(
            this->ptr(),
            detail::object_or_cast(number).ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `number * timedelta`. */
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    inline friend Timedelta operator*(T number, const Timedelta& delta) {
        PyObject* result = PyNumber_Multiply(
            detail::object_or_cast(number).ptr(),
            delta.ptr()
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `timedelta / timedelta`. */
    inline Float operator/(const Timedelta& other) const {
        PyObject* result = PyNumber_TrueDivide(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Float>(result);
    }

    /* Equivalent to Python `timedelta / float` or `timedelta // int`. */
    template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    inline Timedelta operator/(T number) const {
        if constexpr (std::is_integral_v<T>) {
            PyObject* result = PyNumber_FloorDivide(
                this->ptr(),
                detail::object_or_cast(number).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Timedelta>(result);
        } else {
            PyObject* result = PyNumber_TrueDivide(
                this->ptr(),
                detail::object_or_cast(number).ptr()
            );
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Timedelta>(result);
        }
    }

    /* Equivalent to Python `timedelta % timedelta`. */
    inline Timedelta operator%(const Timedelta& other) const {
        PyObject* result = PyNumber_Remainder(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `+timedelta`. */
    inline Timedelta operator+() const {
        PyObject* result = PyNumber_Positive(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

    /* Equivalent to Python `-timedelta`. */
    inline Timedelta operator-() const {
        PyObject* result = PyNumber_Negative(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

};


/* New subclass of pybind11::object that represents a datetime.tzinfo object at the
Python level. */
class Timezone :
    public pybind11::object,
    public impl::EqualCompare<Timezone>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Timezone>;

    static PyObject* convert_to_zoneinfo(PyObject* obj) {
        if (PyDelta_Check(obj)) {
            PyObject* result = PyTimeZone_FromOffset(obj);
            if (result == nullptr) {
                throw error_already_set();
            }
            return result;
        }

        if (impl::PyZoneInfo_Type.ptr() == nullptr) {
            throw TypeError("zoneinfo module not available");
        }
        PyObject* result = PyObject_CallOneArg(impl::PyZoneInfo_Type.ptr(), obj);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    CONSTRUCTORS(Timezone, PyTZInfo_Check, convert_to_zoneinfo);

    /* Default constructor.  Initializes to the system's local timezone. */
    inline Timezone() : Base([] {
        if (impl::tzlocal.ptr() == nullptr) {
            throw TypeError("tzlocal module not available");
        }
        return impl::tzlocal.attr("get_localzone")().release();
    }(), stolen_t{}) {}

    /* Construct a timezone from an IANA-recognized timezone specifier. */
    inline Timezone(Str name) : Base([&name] {
        if (impl::zoneinfo.ptr() == nullptr) {
            throw TypeError("zoneinfo module not available");
        }
        return impl::zoneinfo.attr("ZoneInfo")(name).release();
    }(), stolen_t{}) {}

    /* Explicit overload for string literals that forwards to ZoneInfo constructor. */
    inline Timezone(const char* name) : Timezone(Str(name)) {}

    /* Construct a timezone from a manual timedelta UTC offset. */
    inline explicit Timezone(const Timedelta& offset) : Base([&offset] {
        PyObject* result = PyTimeZone_FromOffset(offset.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a timezone from a manual timedelta UTC offset and name. */
    inline explicit Timezone(const Timedelta& offset, Str name) : Base([&offset, &name] {
        PyObject* result = PyTimeZone_FromOffsetAndName(offset.ptr(), name.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the UTC timezone singleton. */
    inline static const Timezone& UTC() {
        static Timezone utc(PyDateTime_TimeZone_UTC, borrowed_t{});
        return utc;
    }

    /* Get a set of all recognized timezone identifiers. */
    inline static Set AVAILABLE() {
        if (impl::zoneinfo.ptr() == nullptr) {
            throw TypeError("zoneinfo module not available");
        }
        return impl::zoneinfo.attr("available_timezones")();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator==;
    using Compare::operator!=;
};


/* New subclass of pybind11::object that represents a datetime.date object at the
Python level. */
class Date :
    public pybind11::object,
    public impl::FullCompare<Date>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<Date>;

    static PyObject* convert_to_date(PyObject* obj) {
        throw TypeError("cannot convert object to datetime.date");
    }

public:
    CONSTRUCTORS(Date, PyDate_Check, convert_to_date);



};


/* New subclass of pybind11::object that represents a datetime.time object at the
Python level. */
class Time :
    public pybind11::object,
    public impl::FullCompare<Time>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<Time>;

    static PyObject* convert_to_time(PyObject* obj) {
        throw TypeError("cannot convert object to datetime.time");
    }

public:
    CONSTRUCTORS(Time, PyTime_Check, convert_to_time);



};


/*
['astimezone', 'combine', 'ctime', 'date', 'day', 'dst',
'fold', 'fromisocalendar', 'fromisoformat', 'fromordinal', 'fromtimestamp', 'hour',
'isocalendar', 'isoformat', 'isoweekday', 'max', 'microsecond', 'min', 'minute',
'month', 'now', 'replace', 'resolution', 'second', 'strftime', 'strptime', 'time',
'timestamp', 'timetuple', 'timetz', 'today', 'toordinal', 'tzinfo', 'tzname',
'utcfromtimestamp', 'utcnow', 'utcoffset', 'utctimetuple', 'weekday', 'year']
*/


/* New subclass of pybind11::object that represents a datetime.datetime object at the
Python level. */
class Datetime :
    public pybind11::object,
    public impl::FullCompare<Datetime>
{
    using Base = pybind11::object;
    using Compare = impl::FullCompare<Datetime>;

    static PyObject* astimezone(PyObject* obj, PyObject* tz) {
        PyObject* func = PyObject_GetAttrString(obj, "astimezone");
        if (func == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyObject_CallOneArg(func, tz);
        Py_DECREF(func);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

    static PyObject* convert_to_datetime(PyObject* obj) {
        throw TypeError("cannot convert object to datetime.datetime");
    }

public:
    using Units = impl::TimeUnits;
    CONSTRUCTORS(Datetime, PyDateTime_Check, convert_to_datetime);

    /* Default constructor.  Initializes to the current system time with local
    timezone. */
    inline Datetime() : Base([] {
        PyObject* func = PyObject_GetAttrString(impl::PyDateTime_Type.ptr(), "now");
        if (func == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyObject_CallOneArg(func, Timezone::UTC().ptr());
        Py_DECREF(func);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Python datetime object from a dateutil-parseable string and optional
    timezone.  If the string contains its own timezone information (a GMT offset, for
    example), then it will be converted from its current timezone to the specified one.
    Otherwise, it will be localized directly. */
    Datetime(
        Str string,
        std::optional<Timezone> tz = std::nullopt,
        bool fuzzy = false,
        bool dayfirst = false,
        bool yearfirst = false
    ) : Base([&] {
        Object result = impl::dateutil_parser.attr("parse")(
            string,
            py::arg("fuzzy") = fuzzy,
            py::arg("dayfirst") = dayfirst,
            py::arg("yearfirst") = yearfirst
        );
        if (tz.has_value()) {
            if (result.attr("tzinfo").is_none()) {
                Object replace = result.attr("replace");
                return replace(py::arg("tzinfo") = tz.value()).release().ptr();
            }
            return astimezone(result.ptr(), tz.value().ptr());
        }
        return result.release().ptr();
    }(), stolen_t{}) {}

    /* Overload for string literals that forwards to the dateutil constructor. */
    template <typename... Args>
    Datetime(const char* string, Args&&... args) : Datetime(
        Str(string), std::forward<Args>(args)...
    ) {}

    /* Construct a Python datetime object using an offset from a given epoch.  If the
    epoch is not specified, it defaults to the current system time, with the proper
    timezone.  If the epoch has timezone information, the resulting datetime will be
    localized to that timezone, otherwise it will be assumed to be UTC. */
    template <
        typename T,
        typename Unit,
        std::enable_if_t<std::is_arithmetic_v<T>, int> = 0
    >
    explicit Datetime(
        T offset,
        const Unit& unit = Units::s
    ) : Base([&offset, &unit] {
        static_assert(
            Units::is_unit<Unit>,
            "unit must be one of py::Datetime::Units::W, ::D, ::h, ::m, ::s, ::ms, "
            "::us, ::ns"
        );

        PyObject* func = PyObject_GetAttrString(
            impl::PyDateTime_Type.ptr(), "fromtimestamp"
        );
        if (func == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyObject_CallFunctionObjArgs(
            func,
            py::cast(unit.convert(offset, Units::s)).ptr(),
            Timezone::UTC().ptr(),
            nullptr
        );
        Py_DECREF(func);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    // TODO: add constructors for python integers and floats using Units::enable_cpp/enable_py


    /* Construct a Python datetime object using an offset from a given epoch.  If the
    epoch is not specified, it defaults to the current system time, with the proper
    timezone.  If the epoch has timezone information, the resulting datetime will be
    localized to that timezone, otherwise it will be assumed to be UTC. */
    template <
        typename T,
        typename Unit,
        std::enable_if_t<std::is_arithmetic_v<T>, int> = 0
    >
    explicit Datetime(
        T offset,
        const Unit& unit,
        Datetime since
    ) : Base([&offset, &unit, &since] {
        static_assert(
            Units::is_unit<Unit>,
            "unit must be one of py::Datetime::Units::W, ::D, ::h, ::m, ::s, ::ms, "
            "::us, ::ns"
        );

        std::optional<Timezone> tz = since.timezone();
        if (!tz.has_value()) {
            since.localize("UTC");
            tz = since.timezone();
        }

        PyObject* func = PyObject_GetAttrString(
            impl::PyDateTime_Type.ptr(), "fromtimestamp"
        );
        if (func == nullptr) {
            throw error_already_set();
        }
        PyObject* result = PyObject_CallFunctionObjArgs(
            func,
            py::cast(since.timestamp() + unit.convert(offset, Units::s)).ptr(),
            tz.value().ptr(),
            nullptr
        );
        Py_DECREF(func);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Python datetime object from a numeric date and time. */
    explicit Datetime(
        int year,
        int month,
        int day,
        int hour = 0,
        int minute = 0,
        int second = 0,
        int microsecond = 0,
        std::optional<Timezone> tz = std::nullopt
    ) : Base([&] {
        PyObject* result = PyDateTime_FromDateAndTime(
            year, month, day, hour, minute, second, microsecond
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            Object replace = Handle(result).attr("replace");
            return replace(py::arg("tzinfo") = tz.value()).release().ptr();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct a Python datetime object from a numeric date and time, where the
    time is given in fractional seconds. */
    explicit Datetime(
        int year,
        int month,
        int day,
        int hour,
        int minute,
        double second,
        std::optional<Timezone> tz = std::nullopt
    ) : Base([&] {
        int int_seconds = static_cast<int>(second);
        PyObject* result = PyDateTime_FromDateAndTime(
            year,
            month,
            day,
            hour,
            minute,
            int_seconds,
            static_cast<int>((second - int_seconds) * 1e6)
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            Object replace = Handle(result).attr("replace");
            return replace(py::arg("tzinfo") = tz.value()).release().ptr();
        }
        return result;
    }(), stolen_t{}) {}

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Get the minimum possible datetime. */
    inline static const Datetime& min() {
        static Datetime min(impl::PyDateTime_Type.attr("min"), stolen_t{});
        return min;
    }

    /* Get the maximum possible datetime. */
    inline static const Datetime& max() {
        static Datetime max(impl::PyDateTime_Type.attr("max"), stolen_t{});
        return max;
    }

    /* Get the smallest possible datetime. */
    inline static const Datetime& resolution() {
        static Datetime resolution(impl::PyDateTime_Type.attr("resolution"), stolen_t{});
        return resolution;
    }

    /* Get a reference to the datetime singleton describing the UTC epoch. */
    inline static const Datetime& UTC() {
        static Datetime epoch(1970, 1, 1, 0, 0, 0, 0, Timezone::UTC());
        return epoch;
    }

    /* Get the year. */
    inline int year() const noexcept {
        return PyDateTime_GET_YEAR(this->ptr());
    }

    /* Get the month. */
    inline int month() const noexcept {
        return PyDateTime_GET_MONTH(this->ptr());
    }

    /* Get the day. */
    inline int day() const noexcept {
        return PyDateTime_GET_DAY(this->ptr());
    }

    /* Get the hour. */
    inline int hour() const noexcept {
        return PyDateTime_DATE_GET_HOUR(this->ptr());
    }

    /* Get the minute. */
    inline int minute() const noexcept {
        return PyDateTime_DATE_GET_MINUTE(this->ptr());
    }

    /* Get the second. */
    inline int second() const noexcept {
        return PyDateTime_DATE_GET_SECOND(this->ptr());
    }

    /* Get the microsecond. */
    inline int microsecond() const noexcept {
        return PyDateTime_DATE_GET_MICROSECOND(this->ptr());
    }

    /* Get the number of seconds from the UTC epoch as a double. */
    inline double timestamp() const noexcept {
        return this->attr("timestamp")().cast<double>();
    }

    /* Get the number of microseconds from the UTC epoch as an integer. */
    inline long long abs_timestamp() const noexcept {
        long long seconds = this->attr("timestamp")().cast<double>();
        return seconds * 1e6 + this->microsecond();
    }

    /* Get the timezone associated with this datetime or nullopt if the datetime is
    naive. */
    inline std::optional<Timezone> timezone() const {
        PyObject* tz = PyObject_GetAttrString(this->ptr(), "tzinfo");
        if (tz == nullptr) {
            throw error_already_set();
        }
        if (tz == Py_None) {
            Py_DECREF(tz);
            return std::nullopt;
        }
        return reinterpret_steal<Timezone>(tz);
    }

    /* Localize the datetime to a particular timezone.  If the datetime was originally
    naive, then this will interpret it in the target timezone.  If it has previous
    timezone information, then it will be converted to the new timezone.  Passing
    nullopt to this method strips the datetime of its timezone, making it naive. */
    inline void localize(std::optional<Timezone> tz) {
        // strip tzinfo
        if (!tz.has_value()) {
            if (!this->attr("tzinfo").is_none()) {
                *this = this->attr("replace")(py::arg("tzinfo") = None);
            }
            return;
        }

        // directly localize
        if (this->attr("tzinfo").is_none()) {
            *this = this->attr("replace")(py::arg("tzinfo") = tz.value());
            return;
        }

        // convert timezone
        PyObject* result = astimezone(this->ptr(), tz.value().ptr());
        *this = reinterpret_steal<Datetime>(result);
    }

    // toordinal, utctimetuple, timetuple, tzname, dst, utcoffset, replace, fold, date,
    // time, timetz

    /* Return the day of the week as an integer, where Monday is 0 and Sunday is 6. */
    inline int weekday() const noexcept {
        return this->attr("weekday")().cast<int>();
    }

    /* Return the day of the week as an integer, where Monday is 1 and Sunday is 7. */
    inline int isoweekday() const noexcept {
        return this->attr("isoweekday")().cast<int>();
    }

    /* Return the ISO year and week number as a tuple. */
    inline std::tuple<int, int, int> isocalendar() const {
        return this->attr("isocalendar")().cast<std::tuple<int, int, int>>();
    }

    // /* Return the ISO 8601 formatted string representation of the datetime. */
    // inline Str isoformat() const {  // TODO: sep + timespec
    //     return this->attr("isoformat")().cast<Str>();
    // }

    /* Return a ctime string representation. */
    inline Str ctime() const {
        return this->attr("ctime")();
    }

    /* Return a formatted string representation of the datetime. */
    inline Str strftime(Str format) const {
        return this->attr("strftime")(format);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>=;
    using Compare::operator>;

    inline Datetime operator+(const Timedelta& other) const {
        PyObject* result = PyObject_CallMethod(this->ptr(), "__add__", "O", other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Datetime>(result);
    }

    inline Datetime operator-(const Timedelta& other) const {
        PyObject* result = PyObject_CallMethod(this->ptr(), "__sub__", "O", other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Datetime>(result);
    }

    inline Timedelta operator-(const Datetime& other) const {
        PyObject* result = PyObject_CallMethod(this->ptr(), "__sub__", "O", other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

};


}  // namespace py
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Timedelta)
BERTRAND_STD_HASH(bertrand::py::Timezone)
BERTRAND_STD_HASH(bertrand::py::Date)
BERTRAND_STD_HASH(bertrand::py::Time)
BERTRAND_STD_HASH(bertrand::py::Datetime)


#endif  // BERTRAND_PYTHON_DATETIME_H
