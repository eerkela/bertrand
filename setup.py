"""Setup script for Bertrand."""
import subprocess
import sysconfig
from pathlib import Path

from bertrand import BuildSources, Source, Package, setup, env


# TODO: for headless installation, use pipx to install bertrand:

# See: https://devguide.python.org/getting-started/setup-building/#install-dependencies

# sudo apt update
# sudo apt install build-essential pipx cmake autoconf gdb lcov pkg-config \
#     libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
#     libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev lzma lzma-dev \
#     tk-dev uuid-dev zlib1g-dev
# sudo pipx ensurepath --global
# pipx install bertrand
# bertrand init venv


PYTHON_H = Path(sysconfig.get_path("include")) / "Python.h"
PYTHON_PCM = PYTHON_H.parent / "Python.h.pcm"


class BuildSourcesHeadless(BuildSources):
    """A modification of the standard BuiltExt command that skips building C++
    extensions if not in a virtual environment, rather than failing.
    """

    def finalize_options(self) -> None:
        """Skip if not in a virtual environment."""
        if env:
            super().finalize_options()

    def build_extensions(self) -> None:
        """Build if in a virtual environment, otherwise skip."""
        if env:
            try:
                self.stage0()

                # precompile Python.h so that it can be efficiently imported
                # subprocess.check_call(
                #     [
                #         str(env / "bin" / "clang++"),
                #         "-std=c++23",
                #         *self._bertrand_compile_args,
                #         "-fmodule-header",
                #         str(PYTHON_H),
                #         "-o",
                #         str(PYTHON_PCM),
                #     ]
                # )

                # NOTE: we need to bootstrap the AST parser so that it can be used when
                # compiling Bertrand itself.
                build = Path.cwd() / "bertrand" / "env" / "codegen" / "build"
                build.mkdir(exist_ok=True)

                # the AST parser depends on a json parsing library, so we need to
                # build that first
                subprocess.check_call(
                    [
                        str(env / "bin" / "conan"),
                        "install",
                        str(build.parent / "conanfile.txt"),
                        "--build=missing",
                        "--output-folder",
                        ".",
                    ],
                    cwd=build,
                )

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

                self.stage1()
                self.stage2()
                self.stage3()
                self.stage4()

                # then build extensions using it
                # super().build_extensions()
            finally:
                PYTHON_PCM.unlink(missing_ok=True)


setup(
    cpp_deps=[
        Package("nlohmann_json", "3.11.3", "nlohmann_json", "nlohmann_json::nlohmann_json"),
        Package("pcre2", "10.43", "PCRE2", "pcre2::pcre2"),
        Package("cpptrace", "0.6.1", "cpptrace", "cpptrace::cpptrace"),
    ],
    sources=[
        Source("bertrand/example_module.cpp"),
        Source("bertrand/executable.cpp"),
        *(Source(p) for p in Path("bertrand/python").rglob("*.cpp")),
    ],
    cmdclass={"build_ext": BuildSourcesHeadless},
)
