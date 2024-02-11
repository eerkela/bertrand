"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse
import sysconfig

import numpy
import pybind11

from . import get_include, __version__


def main() -> None:
    """Run Bertrand as a command-line utility."""
    parser = argparse.ArgumentParser(description="Bertrand module utilities.")
    parser.add_argument(
        "--version", "-v",
        action="store_true",
        help="Print the installed version number."
    )
    parser.add_argument(
        "--get-include", "--includes", "-I",
        action="store_true",
        help=(
            "List the include directories needed to compile C++ code that uses "
            "Bertrand.  This includes the Python development headers, as well as "
            "those of numpy, pybind11, and bertrand itself.  Users can include all of "
            "these in a single command by adding `$(python3 -m bertrand -I)` to their "
            "compilation options."
        )
    )

    args = parser.parse_args()
    show_help = True

    if args.get_include:
        print(
            "-I", sysconfig.get_path("include"),
            "-lpython" + sysconfig.get_python_version(),
            "-I", numpy.get_include(),
            "-I", pybind11.get_include(),
            "-I", get_include(),
        )
        show_help = False

    if args.version:
        print(__version__)
        show_help = False

    if show_help:
        parser.print_help()


if __name__ == "__main__":
    main()
