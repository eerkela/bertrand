"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse


from . import __version__
from .setuptools import quick_include


# TODO: Add linker options to `python -m bertrand -I` for pcre2, cpptrace, and googletest


def main() -> None:
    """Run Bertrand as a command-line utility."""
    parser = argparse.ArgumentParser(description="Bertrand module utilities.")
    parser.add_argument(
        "-v", "--version",
        action="store_true",
        help="Print the installed version number."
    )
    parser.add_argument(
        "-I", "--include",
        action="store_true",
        help=(
            "List all the include and link symbols needed to compile a pure-C++ "
            "project that relies on Bertrand as a dependency.  This includes the "
            "Python development headers, numpy, pybind11, pcre2, googletest, and "
            "cpptrace as well as those of bertrand itself.  Users can quickly include "
            "all of these in a single command by adding `$(python3 -m bertrand -I)` "
            "to their compilation flags."
        )
    )

    args = parser.parse_args()
    show_help = True

    if args.include:
        print(" ".join(quick_include()))
        show_help = False

    if args.version:
        print(__version__)
        show_help = False

    if show_help:
        parser.print_help()


if __name__ == "__main__":
    main()
