"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import os
import re
import shutil
import stat
import subprocess
import tarfile
import time
import tomllib
from pathlib import Path
from tqdm import tqdm
from urllib.request import urlretrieve


class Environment:
    """A class that initializes a virtual environment with a full C++ compiler suite
    and a custom Python install to match.
    """

    BAR_COLOR = "#bfbfbf"
    BAR_FORMAT = "{l_bar}{bar}| [{elapsed}, {rate_fmt}{postfix}]"
    SEMVER_REGEX = re.compile(r"(\d+)\.(\d+)\.(\d+)")
    OLD_PREFIX = "_OLD_VIRTUAL_"

    class ProgressBar:
        """A simple progress bar to show the download progress of large files."""

        def __init__(self, name: str) -> None:
            self.name = name
            self.pbar: tqdm | None = None
            self.downloaded = 0

        def __call__(self, block_num: int, block_size: int, total_size: int) -> None:
            if not self.pbar:
                self.pbar = tqdm(
                    desc=self.name,
                    total=total_size,
                    unit = " B",
                    unit_divisor=10**6,
                    colour=Environment.BAR_COLOR,
                    bar_format=Environment.BAR_FORMAT,
                )

            self.downloaded += block_size
            if self.downloaded < total_size:
                self.pbar.update(block_size)
            else:
                self.pbar.close()

    # TODO: ping each of these urls to make sure they are valid before proceeding with
    # installation (which can take a long time)

    @staticmethod
    def get_gcc_url(version: str) -> str:
        """Return the URL for the specified GCC version.

        Parameters
        ----------
        version : str
            The version of GCC to download.

        Raises
        ------
        ValueError
            If the GCC version is not in the form 'X.Y.Z' or is less than 14.

        Returns
        -------
        str
            The URL for the specified GCC version.
        """
        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError("Invalid GCC version.  Must be in the form 'X.Y.Z'.")

        major, _, _ = regex.groups()
        if int(major) < 14:
            raise ValueError("GCC version must be 14 or greater.")

        return f"https://ftpmirror.gnu.org/gnu/gcc/gcc-{version}/gcc-{version}.tar.xz"

    @staticmethod
    def get_clang_url(version: str) -> str:
        """Return the URL for the specified Clang version.

        Parameters
        ----------
        version : str
            The version of Clang to download.

        Raises
        ------
        ValueError
            If the Clang version is not in the form 'X.Y.Z' or is less than 18.

        Returns
        -------
        str
            The URL for the specified Clang version.
        """
        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError("Invalid Clang version.  Must be in the form 'X.Y.Z'.")

        major, _, _ = regex.groups()
        if int(major) < 18:
            raise ValueError("Clang version must be 18 or greater.")

        return f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{version}.tar.gz"

    @staticmethod
    def get_ninja_url(version: str) -> str:
        """Return the URL for the specified Ninja version.

        Parameters
        ----------
        version : str
            The version of Ninja to download.

        Raises
        ------
        ValueError
            If the Ninja version is not in the form 'X.Y.Z' or is less than 1.12.

        Returns
        -------
        str
            The URL for the specified Ninja version.
        """
        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError("Invalid Ninja version.  Must be in the form 'X.Y.Z'.")
        major, minor, _ = regex.groups()
        if int(major) < 1 or (int(major) == 1 and int(minor) < 11):
            raise ValueError("Ninja version must be 1.11 or greater.")

        return f"https://github.com/ninja-build/ninja/archive/refs/tags/v{version}.tar.gz"

    @staticmethod
    def get_cmake_url(version: str) -> str:
        """Return the URL for the specified CMake version.

        Parameters
        ----------
        version : str
            The version of CMake to download.

        Raises
        ------
        ValueError
            If the CMake version is not in the form 'X.Y.Z' or is less than 3.28.

        Returns
        -------
        str
            The URL for the specified CMake version.
        """
        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError("Invalid CMake version.  Must be in the form 'X.Y.Z'.")
        major, minor, _ = regex.groups()
        if int(major) < 3 or (int(major) == 3 and int(minor) < 28):
            raise ValueError("CMake version must be 3.28 or greater.")

        return (
            f"https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}.tar.gz"
        )

    @staticmethod
    def get_python_url(version: str) -> str:
        """Return the URL for the specified Python version.

        Parameters
        ----------
        version : str
            The version of Python to download.

        Raises
        ------
        ValueError
            If the Python version is not in the form 'X.Y.Z' or is less than 3.12.  # TODO: determine min version

        Returns
        -------
        str
            The URL for the specified Python version.
        """
        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError("Invalid Python version.  Must be in the form 'X.Y.Z'.")
        major, minor, _ = regex.groups()
        if int(major) < 3 or (int(major) == 3 and int(minor) < 12):
            raise ValueError("Python version must be 3.12 or greater.")

        return f"https://www.python.org/ftp/python/{version}/Python-{version}.tar.xz"

    def __init__(
        self,
        cwd: Path,
        *,
        compiler: str = "gcc",
        compiler_version: str = "14.1.0",
        ninja_version: str = "1.12.1",
        cmake_version: str = "3.29.4",
        python_version: str = "3.12.3",
        workers: int = 0
    ) -> None:
        self.vars = {
           "CC": os.environ.get("CC", None),
           "CXX": os.environ.get("CXX", None),
           "PATH": os.environ.get("PATH", None),
           "LIBRARY_PATH": os.environ.get("LIBRARY_PATH", None),
           "LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", None),
        }

        self.compiler = compiler
        self.compiler_version = compiler_version
        if self.compiler == "gcc":
            self.compiler_url = self.get_gcc_url(self.compiler_version)
        elif self.compiler == "clang":
            self.compiler_url = self.get_clang_url(self.compiler_version)
        else:
            raise ValueError("Compiler must be 'gcc' or 'clang'.")
        self.ninja_version = ninja_version
        self.ninja_url = self.get_ninja_url(self.ninja_version)
        self.cmake_version = cmake_version
        self.cmake_url = self.get_cmake_url(self.cmake_version)
        self.python_version = python_version
        self.python_url = self.get_python_url(self.python_version)

        self.cwd = cwd
        self.venv = cwd / "bertrand"
        self.bin = self.venv / "bin"
        self.include = self.venv / "include"
        self.lib = self.venv / "lib"
        self.workers = workers or os.cpu_count() or 1
        if self.workers < 1:
            raise ValueError("workers must be a positive integer.")

    def clean(self) -> None:
        """Cleans up temporary build artifacts, including environment variables and
        source directories.
        """
        if self.vars["CC"] is not None:
            os.environ["CC"] = self.vars["CC"]
        else:
            os.environ.pop("CC", None)

        if self.vars["CXX"] is not None:
            os.environ["CXX"] = self.vars["CXX"]
        else:
            os.environ.pop("CXX", None)

        if self.vars["PATH"] is not None:
            os.environ["PATH"] = self.vars["PATH"]
        else:
            os.environ.pop("PATH", None)

        if self.vars["LIBRARY_PATH"] is not None:
            os.environ["LIBRARY_PATH"] = self.vars["LIBRARY_PATH"]
        else:
            os.environ.pop("LIBRARY_PATH", None)

        if self.vars["LD_LIBRARY_PATH"] is not None:
            os.environ["LD_LIBRARY_PATH"] = self.vars["LD_LIBRARY_PATH"]
        else:
            os.environ.pop("LD_LIBRARY_PATH", None)

    def install_gcc(self) -> None:
        """Install the GCC compiler."""
        archive = self.venv / f"gcc-{self.compiler_version}.tar.xz"
        source = self.venv / f"gcc-{self.compiler_version}"
        build = self.venv / f"gcc-{self.compiler_version}-build"
        lib64 = self.venv / "lib64"
        urlretrieve(self.compiler_url, archive, self.ProgressBar("Downloading GCC"))
        with tarfile.open(archive) as tar:
            for member in tqdm(
                tar.getmembers(),
                desc=" Extracting GCC",
                total=len(tar.getmembers()),
                colour=Environment.BAR_COLOR,
                bar_format=Environment.BAR_FORMAT,
            ):
                tar.extract(member, self.venv, filter="data")
        archive.unlink()
        subprocess.check_call(["./contrib/download_prerequisites"], cwd=source)
        build.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(
            [
                str(source / "configure"),
                f"--prefix={self.venv}",
                "--enable-languages=c,c++",
                "--enable-shared",
                "--disable-werror",
                "--disable-multilib",
                "--disable-bootstrap"
            ],
            cwd=build
        )
        subprocess.check_call(["make", f"-j{self.workers}"], cwd=build)
        subprocess.check_call(["make", "install"], cwd=build)
        shutil.rmtree(source)
        shutil.rmtree(build)
        os.environ["CC"] = str(self.bin / "gcc")
        os.environ["CXX"] = str(self.bin / "g++")
        if self.vars["PATH"]:
            os.environ["PATH"] = os.pathsep.join([str(self.bin), self.vars["PATH"]])
        else:
            os.environ["PATH"] = str(self.bin)
        if self.vars["LIBRARY_PATH"]:
            os.environ["LIBRARY_PATH"] = os.pathsep.join(
                [str(self.lib), str(lib64), self.vars["LIBRARY_PATH"]]
            )
        else:
            os.environ["LIBRARY_PATH"] = os.pathsep.join([str(self.lib), str(lib64)])
        if self.vars["LD_LIBRARY_PATH"]:
            os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(
                [str(self.lib), str(lib64), self.vars["LD_LIBRARY_PATH"]]
            )
        else:
            os.environ["LD_LIBRARY_PATH"] = os.pathsep.join([str(self.lib), str(lib64)])

    def install_ninja(self) -> None:
        """Install the ninja build system."""
        archive = self.venv / f"ninja-{self.ninja_version}.tar.gz"
        install = self.venv / f"ninja-{self.ninja_version}"
        urlretrieve(self.ninja_url, archive, self.ProgressBar("Downloading Ninja"))
        with tarfile.open(archive) as tar:
            for member in tqdm(
                tar.getmembers(),
                desc=" Extracting Ninja",
                total=len(tar.getmembers()),
                colour=Environment.BAR_COLOR,
                bar_format=Environment.BAR_FORMAT,
            ):
                tar.extract(member, self.venv, filter="data")
        archive.unlink()
        subprocess.check_call([str(install / "configure.py"), "--bootstrap"], cwd=install)
        shutil.move(install / "ninja", self.bin / "ninja")
        shutil.rmtree(install)

    def install_cmake(self) -> None:
        """Install the cmake build system using Ninja as the backend generator."""
        archive = self.venv / f"cmake-{self.cmake_version}.tar.gz"
        source = self.venv / f"cmake-{self.cmake_version}"
        build = self.venv / f"cmake-{self.cmake_version}-build"
        urlretrieve(self.cmake_url, archive, self.ProgressBar("Downloading CMake"))
        with tarfile.open(archive) as tar:
            for member in tqdm(
                tar.getmembers(),
                desc=" Extracting CMake",
                total=len(tar.getmembers()),
                colour=Environment.BAR_COLOR,
                bar_format=Environment.BAR_FORMAT,
            ):
                tar.extract(member, self.venv, filter="data")
        archive.unlink()
        build.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(
            [
                str(source / "bootstrap"),
                f"--prefix={self.venv}",
                "--generator=Ninja",
                f"--parallel={self.workers}",
            ],
            cwd=build
        )
        subprocess.check_call([str(self.bin / "ninja"), f"-j{self.workers}"], cwd=build)
        subprocess.check_call([str(self.bin / "ninja"), "install"], cwd=build)
        shutil.rmtree(source)
        shutil.rmtree(build)

    def install_python(self) -> None:
        """Install a custom Python distribution with the selected compiler."""
        archive = self.venv / f"Python-{self.python_version}.tar.xz"
        source = self.venv / f"Python-{self.python_version}"
        build = self.venv / f"Python-{self.python_version}-build"
        urlretrieve(self.python_url, archive, self.ProgressBar("Downloading Python"))
        with tarfile.open(archive) as tar:
            for member in tqdm(
                tar.getmembers(),
                desc=" Extracting Python",
                total=len(tar.getmembers()),
                colour=Environment.BAR_COLOR,
                bar_format=Environment.BAR_FORMAT,
            ):
                tar.extract(member, self.venv, filter="data")
        archive.unlink()
        build.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(
            [
                str(source / "configure"),
                f"--prefix={self.venv}",
                "--with-ensurepip=upgrade",
                "--enable-shared",
                "--enable-optimizations",
                "--with-lto",
                # f"CFLAGS=\"-DBERTRAND_TRACEBACK_EXCLUDE_PYTHON={source}:{build}:{install}\"",
            ],
            cwd=build
        )
        subprocess.check_call(["make", f"-j{self.workers}"], cwd=build)
        subprocess.check_call(["make", "install"], cwd=build)
        os.symlink(self.bin / "python3", self.bin / "python")
        os.symlink(self.bin / "pip3", self.bin / "pip")
        shutil.rmtree(source)
        shutil.rmtree(build)

    @staticmethod
    def activate(env_file: Path) -> None:
        """Load the environment variables from a TOML file (typically bertrand/env.toml).

        This method parses the file and prints the variables to stdout as bash
        commands, which are caught in the activation script and exported to the
        resulting environment.

        Parameters
        ----------
        env_file : Path
            The path to the TOML file containing the environment variables.

        Raises
        ------
        TOMLDecodeError
            If the file is not a valid TOML file.
        ValueError
            If any of the variables are not strings or lists of strings.

        Notes
        -----
        The file should be formatted as follows:

        ```toml
        [vars]
        CC = "path/to/c/compiler"
        CXX = "path/to/c++/compiler"
        (...)

        [paths]
        PATH = ["/path/to/bin1/", "/path/to/bin2/", (...), "${PATH}"]
        LIBRARY_PATH = ["/path/to/lib1/", "/path/to/lib2/", (...), "${LIBRARY_PATH}"]
        LD_LIBRARY_PATH = ["/path/to/lib1/", "/path/to/lib2/", (...), "${LD_LIBRARY_PATH}"]
        (...)
        ```

        Where each value is a string or a list of strings describing a valid bash
        command.  When the environment is activated, the variables in [vars] will
        overwrite any existing variables with the same name.  The values in [paths]
        will be prepended to the current paths (if any) and joined using the system's
        path separator, which can be found by running `import os; os.pathsep` in
        Python.
        """
        new = []
        with env_file.open("rb") as file:
            data = tomllib.load(file)

        for key, value in data.get("vars", {}).items():
            if not isinstance(value, str):
                raise ValueError(f"Value for {key} must be a string.")

            if os.environ.get(key, None):
                print(f'export {Environment.OLD_PREFIX}{key}="{os.environ[key]}"')

            new.append(f'export {key}={value}')

        for key, value in data.get("paths", {}).items():
            if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
                raise ValueError(f"Value for {key} must be a list of strings.")

            if os.environ.get(key, None):
                print(f'export {Environment.OLD_PREFIX}{key}="{os.environ[key]}"')
                fragment = os.pathsep.join([*value, os.environ[key]])
            else:
                fragment = os.pathsep.join(value)

            new.append(f'export {key}={fragment}')

        print("\n".join(new))

    @staticmethod
    def deactivate(env_file: Path) -> None:
        """Restore the environment variables to their original state.

        This method reads the environment variables from a TOML file (typically
        bertrand/env.toml) and prints bash commands to stdout that will unset the
        variables and restore the values from before the environment was activated.

        Parameters
        ----------
        env_file : Path
            The path to the TOML file containing the environment variables.

        Raises
        ------
        TOMLDecodeError
            If the file is not a valid TOML file.

        Notes
        -----
        When the activate() method is called, the environment variables are saved
        with a prefix of "_OLD_VIRTUAL_" to prevent conflicts.  This method undoes that
        by transferring the value from the prefixed variable back to the original, and
        then clearing the prefixed variable.

        If the variable did not exist before the environment was activated, it will
        clear it without replacement.
        """
        with env_file.open("rb") as file:
            data = tomllib.load(file)

        for key, _ in data.get("vars", {}).keys():
            if os.environ.get(f"{Environment.OLD_PREFIX}{key}", None):
                print(f'export {key}="{os.environ[Environment.OLD_PREFIX + key]}"')
                print(f'unset {Environment.OLD_PREFIX}{key}')
            else:
                print(f'unset {key}')

        for key, _ in data.get("paths", {}).keys():
            if os.environ.get(f"{Environment.OLD_PREFIX}{key}", None):
                print(f'export {key}="${{{Environment.OLD_PREFIX}{key}:-}}"')
                print(f'unset {Environment.OLD_PREFIX}{key}')
            else:
                print(f"unset {key}")

    def create_activation_script(self) -> None:
        """Create an activation script that enters the virtual environment."""
        script = self.venv / "activate"
        with script.open("w") as file:
            file.write(f"""
# This file must be used with "source bertrand/activate" *from bash*
# You cannot run it directly

deactivate() {{
    # reset old environment variables
    if [ -n "${{_OLD_VIRTUAL_CC:-}}" ]; then
        CC="${{_OLD_VIRTUAL_CC:-}}"
        export CC
        unset _OLD_VIRTUAL_CC
    fi
    if [ -n "${{_OLD_VIRTUAL_CXX:-}}" ]; then
        CXX="${{_OLD_VIRTUAL_CXX:-}}"
        export CXX
        unset _OLD_VIRTUAL_CXX
    fi
    if [ -n "${{_OLD_VIRTUAL_PATH:-}}" ]; then
        PATH="${{_OLD_VIRTUAL_PATH:-}}"
        export PATH
        unset _OLD_VIRTUAL_PATH
    fi
    if [ -n "${{_OLD_VIRTUAL_LIBRARY_PATH:-}}" ]; then
        LIBRARY_PATH="${{_OLD_VIRTUAL_LIBRARY_PATH:-}}"
        export LIBRARY_PATH
        unset _OLD_VIRTUAL_LIBRARY_PATH
    fi
    if [ -n "${{_OLD_VIRTUAL_LD_LIBRARY_PATH:-}}" ]; then
        LD_LIBRARY_PATH="${{_OLD_VIRTUAL_LD_LIBRARY_PATH:-}}"
        export LD_LIBRARY_PATH
        unset _OLD_VIRTUAL_LD_LIBRARY_PATH
    fi
    if [ -n "${{_OLD_VIRTUAL_PYTHONHOME:-}}" ]; then
        PYTHONHOME="${{_OLD_VIRTUAL_PYTHONHOME:-}}"
        export PYTHONHOME
        unset _OLD_VIRTUAL_PYTHONHOME
    fi

    if [ -n "${{_OLD_VIRTUAL_PS1:-}}" ]; then
        PS1="${{_OLD_VIRTUAL_PS1:-}}"
        export PS1
        unset _OLD_VIRTUAL_PS1
    fi

    unset BERTRAND_VIRTUAL_ENV
    unset VIRTUAL_ENV
    unset VIRTUAL_ENV_PROMPT
    if [ ! "${{1:-}}" = "nondestructive" ]; then
        # Self destruct!
        unset -f deactivate
    fi

    # Call hash to forget past commands.  Without forgetting past commands,
    # the $PATH changes we made may not be respected.
    hash -r 2> /dev/null
}}

# unsert irrelevant variables
deactivate nondestructive

# on Windows, a path can contain colons and backslashes and has to be converted:
if [ "${{OSTYPE:-}}" = "cygwin" ] || [ "${{OSTYPE:-}}" = "msys" ]; then
    # transform D:\\path\\to\\venv to /d/path/to/venv on MSYS
    # and to /cygdrive/d/path/to/venv on Cygwin
    export VIRTUAL_ENV=$(cygpath "{self.venv}")
else
    # use the path as-is
    export VIRTUAL_ENV="{self.venv}"
fi

BERTRAND_VIRTUAL_ENV="ON"
export BERTRAND_VIRTUAL_ENV

_OLD_VIRTUAL_CC="${{CC:-}}"
CC="$VIRTUAL_ENV/bin/{self.compiler}"
export CC

_OLD_VIRTUAL_CXX="${{CXX:-}}"
CXX="$VIRTUAL_ENV/bin/g++"  # TODO: pass in correct C++ compiler
export CXX

_OLD_VIRTUAL_PATH="$PATH"
PATH="$VIRTUAL_ENV/bin:$PATH"
export PATH

_OLD_VIRTUAL_LIBRARY_PATH="${{LIBRARY_PATH:-}}"
LIBRARY_PATH="$VIRTUAL_ENV/lib:$VIRTUAL_ENV/lib64:$LIBRARY_PATH"
export LIBRARY_PATH

_OLD_VIRTUAL_LD_LIBRARY_PATH="${{LD_LIBRARY_PATH:-}}"
LD_LIBRARY_PATH="$VIRTUAL_ENV/lib:$VIRTUAL_ENV/lib64:$LD_LIBRARY_PATH"
export LD_LIBRARY_PATH

# unset PYTHONHOME if set
# this will fail if PYTHONHOME is set to the empty string (which is bad anyway)
if [ -n "${{PYTHONHOME:-}}" ]; then
    _OLD_VIRTUAL_PYTHONHOME="${{PYTHONHOME:-}}"
    unset PYTHONHOME
fi

if [ -z "${{VIRTUAL_ENV_DISABLE_PROMPT:-}}" ] ; then
    _OLD_VIRTUAL_PS1="${{PS1:-}}"
    PS1="({self.venv.name}) ${{PS1:-}}"
    export PS1
    VIRTUAL_ENV_PROMPT="({self.venv.name}) "
    export VIRTUAL_ENV_PROMPT
fi

# Call hash to forget past commands.  Without forgetting past commands,
# the $PATH changes we made may not be respected.
hash -r 2> /dev/null
            """)

        # set script permissions
        script.chmod(
            stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |  # Owner: read, write, execute
            stat.S_IRGRP | stat.S_IXGRP |                 # Group: read, execute
            stat.S_IROTH | stat.S_IXOTH                   # Others: read, execute
        )

    def create(self, force: bool = False) -> None:
        """Run the full initialization process.

        Parameters
        ----------
        force : bool, optional
            If ``True``, the virtual environment will be rebuilt from scratch.
            Defaults to ``False``.

        Raises
        ------
        subprocess.CalledProcessError
            If any of the subprocess calls fail.
        """
        start = time.time()

        flag = self.venv / "built"
        if force or not flag.exists():
            if self.venv.exists():
                shutil.rmtree(self.venv)
            self.venv.mkdir(parents=True)
            try:
                self.install_gcc()
                self.install_ninja()
                self.install_cmake()
                self.install_python()
                self.create_activation_script()
            except subprocess.CalledProcessError as error:
                print(error.stderr)
                raise
            finally:
                self.clean()
            flag.touch()

        print(f"Elapsed time: {time.time() - start:.2f} seconds")


if __name__ == "__main__":
    # Environment(Path.cwd()).create()
    Environment.activate(Path.cwd() / "bertrand" / "env.toml")
