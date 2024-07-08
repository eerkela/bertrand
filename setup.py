"""Setup script for Bertrand."""
import os
from pathlib import Path
import subprocess

from bertrand import BuildSources, Source, setup, env


# TODO: for headless installation, use pipx to install bertrand:

# See: https://devguide.python.org/getting-started/setup-building/#install-dependencies

# sudo apt update
# sudo apt install build-essential pipx cmake autoconf gdb lcov pkg-config \
#     libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
#     libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev lzma lzma-dev \
#     tk-dev uuid-dev zlib1g-dev
# sudo pipx ensurepath --global
# pipx install bertrand
# bertrand init



class BuildSourcesHeadless(BuildSources):
    """A modification of the standard BuiltExt command that skips building C++
    extensions if not in a virtual environment, rather than failing.
    """

    def build_extensions(self) -> None:
        """Build if in a virtual environment, otherwise skip."""
        if env:
            # build clang AST parser first
            build = Path.cwd() / "bertrand" / "env" / "codegen" / "build"
            build.mkdir(exist_ok=True)
            subprocess.check_call(
                [
                    str(env / "bin" / "cmake"),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "..",
                ],
                cwd=build,
            )
            subprocess.check_call(["ninja"], cwd=build)
            subprocess.check_call(["ninja", "install"], cwd=build)

            # then build extensions using it
            super().build_extensions()




# setup(
#     conan=[
#         "pcre2/10.43@PCRE2/pcre2::pcre2",
#         "cpptrace/0.6.1@cpptrace/cpptrace::cpptrace",
#     ],
#     ext_modules=[
#         Extension(
#             "example",
#             ["bertrand/example.cpp", "bertrand/example_module.cpp"],
#             extra_compile_args=["-fdeclspec"]
#         ),
#         # Extension(
#         #     "bertrand.env.ast",
#         #     ["bertrand/env/ast.cpp"],
#         #     extra_compile_args=["-std=c++17"],
#         #     extra_link_args=["-lclang-cpp"]
#         # ),
#     ],
#     cmdclass={"build_ext": BuildExtHeadless},
# )


cwd = Path.cwd()
setup(
    cpp_deps=[
        "pcre2/10.43@PCRE2/pcre2::pcre2",
        "cpptrace/0.6.1@cpptrace/cpptrace::cpptrace",
    ],
    sources=[
        # Source(cwd / "bertrand" / "example.cpp"),
        Source(cwd / "bertrand" / "example_module.cpp"),
        Source(cwd / "bertrand" / "example_module2.cpp"),
    ],
    cmdclass={"build_ext": BuildSourcesHeadless},
)
