#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

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

    static const Module dateutil_parser = py::import("dateutil.parser");
    static const Module tzlocal = py::import("tzlocal");
    static const Module zoneinfo = py::import("zoneinfo");

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
            return reinterpret_cast<PyObject*>(PyDateTimeAPI->DateType);
        }
        return nullptr;
    }();

    /* Extract the datetime.time type into a pybind11 handle. */
    static const Handle PyTime_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return reinterpret_cast<PyObject*>(PyDateTimeAPI->TimeType);
        }
        return nullptr;
    }();

    /* Extract the datetime.datetime type into a pybind11 handle. */
    static const Handle PyDateTime_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return reinterpret_cast<PyObject*>(PyDateTimeAPI->DateTimeType);
        }
        return nullptr;
    }();

    /* Extract the datetime.timedelta type into a pybind11 handle. */
    static const Handle PyDelta_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return reinterpret_cast<PyObject*>(PyDateTimeAPI->DeltaType);
        }
        return nullptr;
    }();

    /* Extract the datetime.tzinfo type into a pybind11 handle. */
    static const Handle PyTZInfo_Type = []() -> Handle {
        if (DATETIME_IMPORTED) {
            return reinterpret_cast<PyObject*>(PyDateTimeAPI->TZInfoType);
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

    /* Enumerated tags holding numeric conversions for datetime and timedelta types. */
    class TimeUnits {
        friend class py::Timedelta;

        template <typename T, typename U>
        using enable_cpp = std::enable_if_t<std::is_arithmetic_v<T>, U>;

        template <typename T, typename U>
        using enable_py = std::enable_if_t<
            std::is_base_of_v<pybind11::int_, T> ||
            std::is_base_of_v<pybind11::float_, T>,
            U
        >;

        template <typename From>
        class Unit {
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

        struct DUnitTag {};
        struct TUnitTag {};
        struct DTUnitTag {};

        struct Nanoseconds : public Unit<Nanoseconds>, TUnitTag, DTUnitTag {
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

        struct Microseconds : public Unit<Microseconds>, TUnitTag, DTUnitTag {
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

        struct Milliseconds : public Unit<Milliseconds>, TUnitTag, DTUnitTag {
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

        struct Seconds : public Unit<Seconds>, TUnitTag, DTUnitTag {
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

        struct Minutes : public Unit<Minutes>, TUnitTag, DTUnitTag {
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

        struct Hours : public Unit<Hours>, TUnitTag, DTUnitTag {
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

        struct Days : public Unit<Days>, DUnitTag, DTUnitTag {
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

        struct Weeks : public Unit<Weeks>, DUnitTag, DTUnitTag {
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

        struct Months : public Unit<Months>, DUnitTag, DTUnitTag {
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

        struct Years : public Unit<Years>, DUnitTag, DTUnitTag {
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
        static constexpr bool is_date_unit = std::is_base_of_v<DUnitTag, T>;
        template <typename T>
        static constexpr bool is_time_unit = std::is_base_of_v<TUnitTag, T>;
        template <typename T>
        static constexpr bool is_datetime_unit = std::is_base_of_v<DTUnitTag, T>;
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
class Timedelta : public Object, public impl::Ops<Timedelta> {
    using Ops = impl::Ops<Timedelta>;

    static PyObject* convert_to_timedelta(PyObject* obj) {
        throw Object::noconvert<Timedelta>(obj);
    }

public:
    static py::Type Type;
    using Units = impl::TimeUnits;

    template <typename T>
    static constexpr bool like = impl::is_timedelta_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Timedelta, PyDelta_Check, convert_to_timedelta)

    /* Default constructor.  Initializes to an empty delta. */
    Timedelta() : Timedelta(0, 0, 0) {}

    /* Construct a Python timedelta from a number of days, seconds, and microseconds. */
    explicit Timedelta(int days, int seconds, int microseconds) :
        Object(PyDelta_FromDSU(days, seconds, microseconds), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a Python timedelta from a number of arbitrary units. */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    explicit Timedelta(const T& offset, const Unit& = Units::s) :
        Object(Unit::to_timedelta(offset), stolen_t{})
    {
        static_assert(
            Units::is_datetime_unit<Unit>,
            "unit must be one of py::Timedelta::Units::Y, ::M, ::W, ::D, ::h, ::m, "
            "::s, ::ms, ::us, or ::ns"
        );
    }

    /* Construct a timedelta from a number of units, where the number is given as a
    python integer or float. */
    template <typename T, typename Unit, Units::enable_py<T, int> = 0>
    explicit Timedelta(const T& offset, const Unit& = Units::s) :
        Object(Unit::to_timedelta(offset), stolen_t{})
    {
        static_assert(
            Units::is_datetime_unit<Unit>,
            "unit must be one of py::Timedelta::Units::Y, ::M, ::W, ::D, ::h, ::m, "
            "::s, ::ms, ::us, or ::ns"
        );
    }

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    explicit Timedelta(const char* string, bool as_hours = false) :
        Object(impl::TimeParser::to_timedelta(string, as_hours), stolen_t{})
    {}

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    explicit Timedelta(const std::string& string, bool as_hours = false) :
        Object(impl::TimeParser::to_timedelta(string, as_hours), stolen_t{})
    {}

    /* Construct a timedelta from a string representation.  NOTE: This matches both
    abbreviated ("1h22m", "1 hour, 22 minutes", etc.) and clock format ("01:22:00",
    "1:22", "00:01:22:00") strings, with precision up to years and months and down to
    microseconds. */
    explicit Timedelta(const std::string_view& string, bool as_hours = false) :
        Object(impl::TimeParser::to_timedelta(std::string(string), as_hours), stolen_t{})
    {}

    //////////////////////
    ////    STATIC    ////
    //////////////////////

    /* Get the minimum possible timedelta. */
    inline static const Timedelta& min() {
        static Timedelta min(impl::PyDelta_Type.attr("min"));  // TODO: invalid?
        return min;
    }

    /* Get the maximum possible timedelta. */
    inline static const Timedelta& max() {
        static Timedelta max(impl::PyDelta_Type.attr("max"));
        return max;
    }

    /* Get the smallest representable timedelta. */
    inline static const Timedelta& resolution() {
        static Timedelta resolution(impl::PyDelta_Type.attr("resolution"));
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

    pybind11::iterator begin() const = delete;
    pybind11::iterator end() const = delete;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

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
class Timezone : public Object, public impl::Ops<Timezone> {

    static PyObject* convert_to_zoneinfo(PyObject* obj) {
        if (PyDelta_Check(obj)) {
            return PyTimeZone_FromOffset(obj);
        } else {
            return PyObject_CallOneArg(impl::PyZoneInfo_Type.ptr(), obj);
        }
    }

public:
    static py::Type Type;

    template <typename T>
    static constexpr bool like = impl::is_timezone_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Timezone, PyTZInfo_Check, convert_to_zoneinfo)

    /* Default constructor.  Initializes to the system's local timezone. */
    Timezone() {
        m_ptr = impl::tzlocal.attr("get_localzone")().release().ptr();
    }

    /* Construct a timezone from an IANA-recognized timezone specifier. */
    explicit Timezone(const Str& name) {
        m_ptr = impl::zoneinfo.attr("ZoneInfo")(name).release().ptr();
    }

    /* Explicit overload for string literals that forwards to ZoneInfo constructor. */
    explicit Timezone(const char* name) : Timezone(Str(name)) {}

    /* Construct a timezone from a manual timedelta UTC offset. */
    explicit Timezone(const Timedelta& offset) :
        Object(PyTimeZone_FromOffset(offset.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a timezone from a manual timedelta UTC offset and name. */
    explicit Timezone(const Timedelta& offset, const Str& name) :
        Object(PyTimeZone_FromOffsetAndName(offset.ptr(), name.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    ////////////////////////////////
    ////    PYTHON INTERFACE    ////
    ////////////////////////////////

    /* Get the UTC timezone singleton. */
    inline static const Timezone& UTC() {
        static const Timezone utc(PyDateTime_TimeZone_UTC, borrowed_t{});
        return utc;
    }

    /* Get a set of all recognized timezone identifiers. */
    inline static Set AVAILABLE() {
        return impl::zoneinfo.attr("available_timezones")();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    pybind11::iterator begin() const = delete;
    pybind11::iterator end() const = delete;

    using impl::Ops<Timezone>::operator==;
    using impl::Ops<Timezone>::operator!=;
};


/* New subclass of pybind11::object that represents a datetime.date object at the
Python level. */
class Date : public Object, public impl::Ops<Date> {
    using Ops = impl::Ops<Date>;

    static PyObject* convert_to_date(PyObject* obj) {
        throw Object::noconvert<Date>(obj);
    }

public:
    static py::Type Type;
    using Units = impl::TimeUnits;

    template <typename T>
    static constexpr bool like = impl::is_date_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Date, PyDate_Check, convert_to_date)

    /* Default constructor.  Initializes to the current system date. */
    Date() : Object(impl::PyDate_Type.attr("today")().release(), stolen_t{}) {}

    /* Construct a date object using Python constructor syntax. */
    explicit Date(int year, int month, int day) :
        Object(PyDate_FromDate(year, month, day), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a date object using a given number of units. */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    explicit Date(const T& offset, const Unit& unit = Units::D) {
        static_assert(
            Units::is_date_unit<Unit>,
            "unit must be one of py::Timedelta::Units::Y, ::M, ::W, or ::D"
        );

        long long days = unit.convert(offset, Units::D);
        int y = Units::D.convert(days, Units::Y);
        days -= Units::Y.convert(y, Units::D);
        int m = Units::D.convert(days, Units::M);
        days -= Units::M.convert(m, Units::D);

        m_ptr = PyDate_FromDate(y, m, days);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Construct a date object from an iso format date string. */
    explicit Date(const Str& string) :
        Object(impl::PyDate_Type.attr("fromisoformat")(string).release().ptr(), stolen_t{})
    {}

    /* Explicit overload for string literals, which allows implicit conversion of
    function arguments, etc. */
    template <typename... Args>
    explicit Date(const char* string, Args&&... args) :
        Date(Str(string), std::forward<Args>(args)...)
    {}

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Equivalent to Python `datetime.date.fromordinal()`. */
    inline static Date fromordinal(size_t ordinal) {
        return impl::PyDate_Type.attr("fromordinal")(ordinal);
    }

    /* Equivalent to Python `datetime.date.fromisocalendar()`. */
    inline static Date fromisocalendar(int year, int week, int day) {
        return impl::PyDate_Type.attr("fromisocalendar")(year, week, day);
    }

    /* Equivalent to Python `datetime.date.min`. */
    inline static const Date& min() {
        static Date min(impl::PyDate_Type.attr("min"));  // TODO: invalid?
        return min;
    }

    /* Equivalent to Python `datetime.date.max`. */
    inline static const Date& max() {
        static Date max(impl::PyDate_Type.attr("max"));
        return max;
    }

    /* Equivalent to Python `datetime.date.resolution`. */
    inline static const Date& resolution() {
        static Date resolution(impl::PyDate_Type.attr("resolution"));
        return resolution;
    }

    /* Equivalent to Python `datetime.date.year`. */
    inline int year() const {
        return PyDateTime_GET_YEAR(this->ptr());
    }

    /* Equivalent to Python `datetime.date.month`. */
    inline int month() const {
        return PyDateTime_GET_MONTH(this->ptr());
    }

    /* Equivalent to Python `datetime.date.day`. */
    inline int day() const {
        return PyDateTime_GET_DAY(this->ptr());
    }

    /* Equivalent to Python `datetime.date.replace(args...)`. */
    template <typename... Args>
    inline Date replace(Args&&... args) const {
        return this->attr("replace")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `datetime.date.timetuple()`. */
    inline Tuple timetuple() const {
        return this->attr("timetuple")();
    }

    /* Equivalent to Python `datetime.date.toordinal()`. */
    inline size_t toordinal() const {
        return static_cast<size_t>(this->attr("toordinal")());
    }

    /* Equivalent to Python `datetime.date.weekday()`. */
    inline int weekday() const {
        return static_cast<int>(this->attr("weekday")());
    }

    /* Equivalent to Python `datetime.date.isoweekday()`. */
    inline int isoweekday() const {
        return static_cast<int>(this->attr("isoweekday")());
    }

    /* Equivalent to Python `datetime.date.isocalendar()`. */
    inline Tuple isocalendar() const {
        return this->attr("isocalendar")();
    }

    /* Equivalent to Python `datetime.date.isoformat()`. */
    inline Str isoformat() const {
        return this->attr("isoformat")();
    }

    /* Equivalent to Python `datetime.date.ctime()`. */
    inline Str ctime() const {
        return this->attr("ctime")();
    }

    /* Equivalent to Python `datetime.date.strftime()`. */
    inline Str strftime(Str format) const {
        return this->attr("strftime")(format);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    pybind11::iterator begin() const = delete;
    pybind11::iterator end() const = delete;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

    /* Equivalent to Python `date + timedelta`. */
    inline Date operator+(const Timedelta& delta) const {
        PyObject* result = PyNumber_Add(this->ptr(), delta.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Date>(result);
    }

    /* Equivalent to Python `date - timedelta`. */
    inline Date operator-(const Timedelta& delta) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), delta.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Date>(result);
    }

    /* Equivalent to Python `date - date`. */
    inline Timedelta operator-(const Date& other) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

};


/* New subclass of pybind11::object that represents a datetime.time object at the
Python level. */
class Time : public Object, public impl::Ops<Time> {
    using Ops = impl::Ops<Time>;

    static Object _localize(Handle obj, Handle tz) {
        if (obj.attr("tzinfo").is_none()) {
            return obj.attr("replace")(py::arg("tzinfo") = tz);
        }
        return obj.attr("astimezone")(tz);
    }

    static PyObject* convert_to_time(PyObject* obj) {
        throw Object::noconvert<Time>(obj);
    }

public:
    static py::Type Type;
    using Units = impl::TimeUnits;

    template <typename T>
    static constexpr bool like = impl::is_time_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Time, PyTime_Check, convert_to_time)

    /* Default constructor.  Initializes to the current system time, in its local
    timezone. */
    Time() {
        Object result = impl::PyDateTime_Type.attr("now")().attr("time")();
        m_ptr = _localize(result.ptr(), Timezone().ptr()).release().ptr();
    }

    /* Construct a time object using Python constructor syntax. */
    explicit Time(
        int h,
        int m,
        int s,
        int us = 0,
        std::optional<Timezone> tz = std::nullopt
    ) {
        m_ptr = PyTime_FromTime(h, m, s, us);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            PyObject* temp = m_ptr;
            m_ptr = _localize(m_ptr, tz.value().ptr()).release().ptr();
            Py_DECREF(temp);
        }
    }

    /* Construct a time object using a given number of units. */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    explicit Time(
        const T& offset,
        const Unit& unit = Units::s,
        std::optional<Timezone> tz = std::nullopt
    ) {
        static_assert(
            Units::is_time_unit<Unit>,
            "unit must be one of py::Timedelta::Units::h, ::m, ::s, ::ms, ::us, or ::ns"
        );
        long long us = unit.convert(offset, Units::us);
        int h = Units::us.convert(us, Units::h);
        us -= Units::h.convert(h, Units::us);
        int m = Units::us.convert(us, Units::m);
        us -= Units::m.convert(m, Units::us);
        int s = Units::us.convert(us, Units::s);
        us -= Units::s.convert(s, Units::us);

        m_ptr = PyTime_FromTime(h, m, s, us);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            PyObject* temp = m_ptr;
            m_ptr = _localize(m_ptr, tz.value().ptr()).release().ptr();
            Py_DECREF(temp);
        }
    }

    /* Construct a time object from an iso format time string. */
    explicit Time(const Str& string, std::optional<Timezone> tz = std::nullopt) {
        Object result = impl::PyTime_Type.attr("fromisoformat")(string);
        if (tz.has_value()) {
            m_ptr = _localize(result.ptr(), tz.value().ptr()).release().ptr();
        } else {
            m_ptr = result.release().ptr();
        }
    }

    /* Explicit overload for string literals, which allows implicit conversion of
    function arguments, etc. */
    template <typename... Args>
    explicit Time(const char* string, Args&&... args) : Time(
        Str(string), std::forward<Args>(args)...
    ) {}

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Equivalent to Python `datetime.time.min`. */
    inline static const Time& min() {
        static Time min(impl::PyTime_Type.attr("min"));
        return min;
    }

    /* Equivalent to Python `datetime.time.max`. */
    inline static const Time& max() {
        static Time max(impl::PyTime_Type.attr("max"));
        return max;
    }

    /* Equivalent to Python `datetime.time.resolution`. */
    inline static const Time& resolution() {
        static Time resolution(impl::PyTime_Type.attr("resolution"));
        return resolution;
    }

    /* Equivalent to Python `datetime.time.hour`. */
    inline int hour() const {
        return PyDateTime_TIME_GET_HOUR(this->ptr());
    }

    /* Equivalent to Python `datetime.time.minute`. */
    inline int minute() const {
        return PyDateTime_TIME_GET_MINUTE(this->ptr());
    }

    /* Equivalent to Python `datetime.time.second`. */
    inline int second() const {
        return PyDateTime_TIME_GET_SECOND(this->ptr());
    }

    /* Equivalent to Python `datetime.time.microsecond`. */
    inline int microsecond() const {
        return PyDateTime_TIME_GET_MICROSECOND(this->ptr());
    }

    /* Equivalent to Python `datetime.time.fold`. */
    inline int fold() const {
        return PyDateTime_TIME_GET_FOLD(this->ptr());
    }

    /* Get the timezone associated with this time or nullopt if the time is naive. */
    inline std::optional<Timezone> timezone() const {
        Object tz = this->attr("tzinfo");
        if (tz.is_none()) {
            return std::nullopt;
        }
        return tz.cast<Timezone>();
    }

    /* Localize the time to a particular timezone.  If the time was originally naive,
    then this will interpret it in the target timezone.  If it has previous timezone
    information, then it will be converted to the new timezone.  Passing nullopt to
    this method strips the datetime of its timezone, making it naive. */
    inline void localize(std::optional<Timezone> tz) {
        if (tz.has_value()) {
            *this = _localize(this->ptr(), tz.value().ptr());
        } else if (!this->attr("tzinfo").is_none()) {
            *this = this->attr("replace")(py::arg("tzinfo") = None);
        }
    }

    /* Equivalent to Python `datetime.time.replace(args...)`. */
    template <typename... Args>
    inline Time replace(Args&&... args) const {
        return this->attr("replace")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `datetime.time.isoformat([timespec])`. */
    inline Str isoformat(Str timespec = "auto") const {
        return this->attr("isoformat")(timespec);
    }

    /* Equivalent to Python `datetime.time.strftime(format)`. */
    inline Str strftime(Str format) const {
        return this->attr("strftime")(format);
    }

    /* Equivalent to Python `datetime.time.utcoffset()`. */
    inline Timedelta utcoffset() const {
        return this->attr("utcoffset")();
    }

    /* Equivalent to Python `datetime.time.dst()`. */
    inline Timedelta dst() const {
        return this->attr("dst")();
    }

    /* Equivalent to Python `datetime.time.tzname()`. */
    inline Str tzname() const {
        return this->attr("tzname")();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    pybind11::iterator begin() const = delete;
    pybind11::iterator end() const = delete;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

    /* Equivalent to Python `time + timedelta`. */
    inline Time operator+(const Timedelta& delta) const {
        PyObject* result = PyNumber_Add(this->ptr(), delta.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Time>(result);
    }

    /* Equivalent to Python `time - timedelta`. */
    inline Time operator-(const Timedelta& delta) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), delta.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Time>(result);
    }

    /* Equivalent to Python `time - time`. */
    inline Timedelta operator-(const Time& other) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Timedelta>(result);
    }

};


/* New subclass of pybind11::object that represents a datetime.datetime object at the
Python level. */
class Datetime : public Object, public impl::Ops<Datetime> {
    using Ops = impl::Ops<Datetime>;

    static Object _localize(Handle obj, Handle tz) {
        if (obj.attr("tzinfo").is_none()) {
            return obj.attr("replace")(py::arg("tzinfo") = tz);
        }
        return obj.attr("astimezone")(tz);
    }

    static PyObject* convert_to_datetime(PyObject* obj) {
        throw Object::noconvert<Datetime>(obj);
    }

public:
    static py::Type Type;
    using Units = impl::TimeUnits;

    template <typename T>
    static constexpr bool like = impl::is_datetime_like<T>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_PYTHON_CONSTRUCTORS(Object, Datetime, PyDateTime_Check, convert_to_datetime)

    /* Default constructor.  Initializes to the current system time with local
    timezone. */
    Datetime() {
        Object result = impl::PyDateTime_Type.attr("now")();
        m_ptr = _localize(result.ptr(), Timezone().ptr()).release().ptr();
    }

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
    ) {
        m_ptr = PyDateTime_FromDateAndTime(
            year, month, day, hour, minute, second, microsecond
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            PyObject* temp = m_ptr;
            Object replace = Handle(m_ptr).attr("replace");
            m_ptr = replace(py::arg("tzinfo") = tz.value()).release().ptr();
            Py_DECREF(temp);
        }
    }

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
    ) {
        int int_seconds = static_cast<int>(second);
        m_ptr = PyDateTime_FromDateAndTime(
            year,
            month,
            day,
            hour,
            minute,
            int_seconds,
            static_cast<int>((second - int_seconds) * 1e6)
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
        if (tz.has_value()) {
            PyObject* temp = m_ptr;
            Object replace = Handle(m_ptr).attr("replace");
            m_ptr = replace(py::arg("tzinfo") = tz.value()).release().ptr();
            Py_DECREF(temp);
        }
    }

    /* Construct a Python datetime by combining a separate date and time. */
    explicit Datetime(const Date& date, const Time& time) :
        Object(impl::PyDateTime_Type.attr("combine")(
            date.ptr(),
            time.ptr()
        ).release(), stolen_t{}) {}

    /* Construct a Python datetime object from a separate date and time, with an
    optional timezone. */
    explicit Datetime(
        const Date& date,
        const Time& time,
        Timezone tz
    ) : Object(impl::PyDateTime_Type.attr("combine")(
            date.ptr(),
            time.ptr(),
            tz.ptr()
        ).release(), stolen_t{}) {}

    /* Construct a Python datetime object from a dateutil-parseable string and optional
    timezone.  If the string contains its own timezone information (a GMT offset, for
    example), then it will be converted from its current timezone to the specified one.
    Otherwise, it will be localized directly. */
    explicit Datetime(
        const Str& string,
        std::optional<Timezone> tz = std::nullopt,
        bool fuzzy = false,
        bool dayfirst = false,
        bool yearfirst = false
    ) {
        m_ptr = impl::dateutil_parser.attr("parse")(
            string,
            py::arg("fuzzy") = fuzzy,
            py::arg("dayfirst") = dayfirst,
            py::arg("yearfirst") = yearfirst
        ).release().ptr();
        if (tz.has_value()) {
            PyObject* temp = m_ptr;
            m_ptr = _localize(m_ptr, tz.value().ptr()).release().ptr();
            Py_DECREF(temp);
        }
    }

    /* Overload for string literals that forwards to the dateutil constructor.  This is
    necessary to allow implicit conversions in function calls, etc. */
    template <typename... Args>
    explicit Datetime(const char* string, Args&&... args) : Datetime(
        Str(string), std::forward<Args>(args)...
    ) {}

    /* Construct a Python datetime object using an offset from the UTC epoch
    (1970-01-01 00:00:00+0000). */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    explicit Datetime(
        T offset,
        const Unit& unit = Units::s
    ) : Object((UTC() + Timedelta(offset, unit)).release(), stolen_t{}) {
        static_assert(
            Units::is_datetime_unit<Unit>,
            "unit must be one of py::Datetime::Units::W, ::D, ::h, ::m, ::s, ::ms, "
            "::us, ::ns"
        );
    }

    /* Construct a Python datetime object using an offset from a given epoch.  If the
    epoch is not specified, it defaults to the current system time, with the proper
    timezone.  If the epoch has timezone information, the resulting datetime will be
    localized to that timezone, otherwise it will be assumed to be UTC. */
    template <typename T, typename Unit, Units::enable_cpp<T, int> = 0>
    explicit Datetime(
        T offset,
        const Unit& unit,
        Datetime since
    ) {
        static_assert(
            Units::is_datetime_unit<Unit>,
            "unit must be one of py::Datetime::Units::W, ::D, ::h, ::m, ::s, ::ms, "
            "::us, ::ns"
        );

        if (!since.timezone().has_value()) {
            since.localize(Timezone::UTC());
        }
        m_ptr = (since + Timedelta(offset, unit)).release().ptr();
    }

    /////////////////////////
    ////    INTERFACE    ////
    /////////////////////////

    /* Equivalent to Python `datetime.datetime.min`. */
    inline static const Datetime& min() {
        static Datetime min(impl::PyDateTime_Type.attr("min"));
        return min;
    }

    /* Equivalent to Python `datetime.datetime.max`. */
    inline static const Datetime& max() {
        static Datetime max(impl::PyDateTime_Type.attr("max"));
        return max;
    }

    /* Equivalent to Python `datetime.datetime.resolution`. */
    inline static const Datetime& resolution() {
        static Datetime resolution(impl::PyDateTime_Type.attr("resolution"));
        return resolution;
    }

    /* Get a reference to the datetime singleton describing the UTC epoch. */
    inline static const Datetime& UTC() {
        static Datetime epoch(1970, 1, 1, 0, 0, 0, 0, Timezone::UTC());
        return epoch;
    }

    /* Equivalent to Python `datetime.datetime.fromordinal(value)`. */
    template <typename T>
    inline static Datetime fromordinal(const T& value) {
        return impl::PyDateTime_Type.attr("fromordinal")(value);
    }

    /* Equivalent to Python `datetime.datetime.fromisoformat(string)`. */
    template <typename T>
    inline static Datetime fromisoformat(const T& string) {
        return impl::PyDateTime_Type.attr("fromisoformat")(string);
    }

    /* Equivalent to Python `datetime.datetime.fromisocalendar(year, week, day)`. */
    template <typename T, typename U, typename V>
    inline static Datetime fromisocalendar(const T& year, const U& week, const V& day) {
        return impl::PyDateTime_Type.attr("fromisocalendar")(year, week, day);
    }

    /* Equivalent to Python `datetime.datetime.strptime(string, format)`. */
    template <typename T, typename U>
    inline static Datetime strptime(const T& string, const U& format) {
        return impl::PyDateTime_Type.attr("strptime")(string, format);
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
        return static_cast<double>(this->attr("timestamp")());
    }

    /* Get the number of microseconds from the UTC epoch as an integer. */
    inline long long abs_timestamp() const noexcept {
        long long seconds = static_cast<long long>(this->attr("timestamp")());
        return seconds * 1e6 + this->microsecond();
    }

    /* Equivalent to Python `datetime.datetime.fold`. */
    inline int fold() const noexcept {
        return PyDateTime_DATE_GET_FOLD(this->ptr());
    }

    /* Get the timezone associated with this datetime or nullopt if the datetime is
    naive. */
    inline std::optional<Timezone> timezone() const {
        Object tz = this->attr("tzinfo");
        if (tz.is_none()) {
            return std::nullopt;
        }
        return tz.cast<Timezone>();
    }

    /* Localize the datetime to a particular timezone.  If the datetime was originally
    naive, then this will interpret it in the target timezone.  If it has previous
    timezone information, then it will be converted to the new timezone.  Passing
    nullopt to this method strips the datetime of its timezone, making it naive. */
    inline void localize(std::optional<Timezone> tz) {
        if (tz.has_value()) {
            *this = _localize(this->ptr(), tz.value().ptr());
        } else if (!this->attr("tzinfo").is_none()) {
            *this = this->attr("replace")(py::arg("tzinfo") = None);
        }
    }

    /* Equivalent to Python `datetime.datetime.date()`. */
    inline Date date() const {
        return this->attr("date")();
    }

    /* Equivalent to Python `datetime.datetime.time()`. */
    inline Time time() const {
        return this->attr("time")();
    }

    /* Equivalent to Python `datetime.datetime.timetz()`. */
    inline Time timetz() const {
        return this->attr("timetz")();
    }

    /* Equivalent to Python `datetime.datetime.replace()`. */
    template <typename... Args>
    inline Datetime replace(Args&&... args) {
        return this->attr("replace")(std::forward<Args>(args)...);
    }

    /* Equivalent to Python `datetime.datetime.utcoffset()`. */
    inline Timedelta utcoffset() const {
        return this->attr("utcoffset")();
    }

    /* Equivalent to Python `datetime.datetime.dst()`. */
    inline Timedelta dst() const {
        return this->attr("dst")();
    }

    /* Equivalent to Python `datetime.datetime.tzname()`. */
    inline Str tzname() const {
        return this->attr("tzname")();
    }

    /* Equivalent to Python `datetime.datetime.timetuple()`. */
    inline Tuple timetuple() const {
        return this->attr("timetuple")();
    }

    /* Equivalent to Python `datetime.datetime.utctimetuple()`. */
    inline Tuple utctimetuple() const {
        return this->attr("utctimetuple")();
    }

    /* Equivalent to Python `datetime.datetime.toordinal()`. */
    inline size_t toordinal() const {
        return static_cast<size_t>(this->attr("toordinal")());
    }

    /* Return the day of the week as an integer, where Monday is 0 and Sunday is 6. */
    inline int weekday() const noexcept {
        return static_cast<int>(this->attr("weekday")());
    }

    /* Return the day of the week as an integer, where Monday is 1 and Sunday is 7. */
    inline int isoweekday() const noexcept {
        return static_cast<int>(this->attr("isoweekday")());
    }

    /* Return the ISO year and week number as a tuple. */
    inline std::tuple<int, int, int> isocalendar() const {
        return static_cast<std::tuple<int, int, int>>(this->attr("isocalendar")());
    }

    /* Return the ISO 8601 formatted string representation of the datetime. */
    inline Str isoformat(Str sep = "T", Str timespec = "auto") const {
        return this->attr("isoformat")(sep, timespec);
    }

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

    pybind11::iterator begin() const = delete;
    pybind11::iterator end() const = delete;

    using Ops::operator<;
    using Ops::operator<=;
    using Ops::operator==;
    using Ops::operator!=;
    using Ops::operator>=;
    using Ops::operator>;

    inline Datetime operator+(const Timedelta& other) const {
        PyObject* result = PyNumber_Add(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Datetime>(result);
    }

    inline Datetime operator-(const Timedelta& other) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), other.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Datetime>(result);
    }

    inline Timedelta operator-(const Datetime& other) const {
        PyObject* result = PyNumber_Subtract(this->ptr(), other.ptr());
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
