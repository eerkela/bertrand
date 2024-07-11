"""Setup script for Bertrand."""
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


setup(
    cpp_deps=[
        "pcre2/10.43@PCRE2/pcre2::pcre2",  # TODO: standardize on | separators
        "cpptrace/0.6.1@cpptrace/cpptrace::cpptrace",
    ],
    sources=[
        Source("bertrand/python.cpp"),
        *[Source(p) for p in Path("bertrand/python").rglob("*.cpp")],
    ],
    # sources=[Source("A.cpp"), Source("B.cpp")],
    cmdclass={"build_ext": BuildSourcesHeadless},
)
