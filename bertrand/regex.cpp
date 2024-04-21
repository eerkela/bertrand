
#include "regex.h"
#include "python.h"

namespace py = bertrand::py;


PYBIND11_MODULE(regex, m) {
    m.doc() = "Regular expression module for Bertrand.";

    py::Class<bertrand::Regex>(m, "Regex")
        .def(py::init<const std::string&>(), py::arg("pattern"))
        .def_property_readonly("pattern", &bertrand::Regex::pattern)
        .def_property_readonly("flags", &bertrand::Regex::flags)
        .def_property_readonly("groupcount", &bertrand::Regex::groupcount)
        .def_property_readonly("groupindex", &bertrand::Regex::groupindex)
        .def("has_flag", &bertrand::Regex::has_flag, py::arg("flag"))
        .def(
            "match",
            py::overload_cast<const std::string&>(
                &bertrand::Regex::match,
                py::const_
            ),
            py::arg("string")
        )
        .def(
            "match",
            py::overload_cast<const std::string&, long long, long long>(
                &bertrand::Regex::match,
                py::const_
            ),
            py::arg("string"),
            py::arg("start"),
            py::arg("stop") = -1
        )
        .def("__repr__", [](const bertrand::Regex& self) {
            std::ostringstream os;
            os << self;
            return os.str();
        })
    ;

    py::Class<bertrand::Regex::Match>(m, "Match")
        .def_property_readonly("count", &bertrand::Regex::Match::count)
        .def(
            "start",
            py::overload_cast<size_t>(
                &bertrand::Regex::Match::start,
                py::const_
            ),
            py::arg("index") = 0
        )
        .def(
            "start",
            py::overload_cast<const char*>(
                &bertrand::Regex::Match::start,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "start",
            py::overload_cast<const std::string&>(
                &bertrand::Regex::Match::start,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "start",
            py::overload_cast<const std::string_view&>(
                &bertrand::Regex::Match::start,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "stop",
            py::overload_cast<size_t>(
                &bertrand::Regex::Match::stop,
                py::const_
            ),
            py::arg("index") = 0
        )
        .def(
            "stop",
            py::overload_cast<const char*>(
                &bertrand::Regex::Match::stop,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "stop",
            py::overload_cast<const std::string&>(
                &bertrand::Regex::Match::stop,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "stop",
            py::overload_cast<const std::string_view&>(
                &bertrand::Regex::Match::stop,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "span",
            py::overload_cast<size_t>(
                &bertrand::Regex::Match::span,
                py::const_
            ),
            py::arg("index") = 0
        )
        .def(
            "span",
            py::overload_cast<const char*>(
                &bertrand::Regex::Match::span,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "span",
            py::overload_cast<const std::string&>(
                &bertrand::Regex::Match::span,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "span",
            py::overload_cast<const std::string_view&>(
                &bertrand::Regex::Match::span,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "group",
            py::overload_cast<size_t>(
                &bertrand::Regex::Match::group,
                py::const_
            ),
            py::arg("index") = 0
        )
        .def(
            "group",
            py::overload_cast<const char*>(
                &bertrand::Regex::Match::group,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "group",
            py::overload_cast<const std::string&>(
                &bertrand::Regex::Match::group,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "group",
            py::overload_cast<const std::string_view&>(
                &bertrand::Regex::Match::group,
                py::const_
            ),
            py::arg("group")
        )
        .def(
            "group",
            py::overload_cast<const py::args&>(
                &bertrand::Regex::Match::group,
                py::const_
            )
        )
        .def("__bool__", &bertrand::Regex::Match::operator bool)
    ;

}
