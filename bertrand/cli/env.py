"""Create and manage Python/C/C++ virtual environments using the Bertrand CLI."""
import os
import re
import shutil
import stat
import subprocess
import sys
import sysconfig
import tarfile
import time
import tomllib
from pathlib import Path
from urllib.request import urlretrieve

import numpy
import pybind11
import requests
from bs4 import BeautifulSoup
from tqdm import tqdm


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

    @staticmethod
    def get_gcc_version(version: str) -> str:
        """Parse the version specifier for the GCC compiler.

        Parameters
        ----------
        version : str
            The version specifier to parse.

        Returns
        -------
        str
            The version of GCC to download.

        Raises
        ------
        ValueError
            If the version number was invalid or could not be detected.
        """
        if version == "latest":
            url = "https://gcc.gnu.org/releases.html"
            response = requests.get(url, timeout=30)
            soup = BeautifulSoup(response.text, "html.parser")
            pattern = re.compile(
                r"GCC (?P<major>\d+)\.(?P<minor>\d+)(\.(?P<patch>\d+))?"
            )
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = pattern.match(link.text)
                if regex:
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch") or 0)
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest GCC version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid GCC version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        major, _, _ = regex.groups()
        if int(major) < 14:
            raise ValueError("GCC version must be 14 or greater.")

        return version

    @staticmethod
    def get_gcc_url(version: str) -> str:
        """Return the URL for the specified GCC version.

        Parameters
        ----------
        version : str
            The version of GCC to download.

        Returns
        -------
        str
            The URL for the specified GCC version.

        Raises
        ------
        ValueError
            If the version number could not be found.
        """
        result = f"https://ftpmirror.gnu.org/gnu/gcc/gcc-{version}/gcc-{version}.tar.xz"
        response = requests.head(result, timeout=30, allow_redirects=True)
        if response.status_code >= 400:
            raise ValueError(f"GCC version {version} not found.")
        print(f"GCC URL: {result}")
        return result

    @staticmethod
    def get_clang_version(version: str) -> str:
        """Parse the version specifier for the Clang compiler.

        Parameters
        ----------
        version : str
            The version specifier to parse.

        Returns
        -------
        str
            The version of Clang to download.

        Raises
        ------
        ValueError
            If the version number was invalid or could not be detected.
        """
        if version == "latest":
            url = "https://github.com/llvm/llvm-project/releases"
            response = requests.get(url, timeout=30)
            soup = BeautifulSoup(response.text, "html.parser")
            pattern = re.compile(
                r"LLVM (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)(-rc(?P<candidate>\d+))?"
            )
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = pattern.match(link.text)
                if regex and not regex.group("candidate"):
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch")),
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest CMake version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid Clang version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        major, _, _ = regex.groups()
        if int(major) < 18:
            raise ValueError("Clang version must be 18 or greater.")

        return version

    @staticmethod
    def get_clang_url(version: str) -> str:
        """Return the URL for the specified Clang version.

        Parameters
        ----------
        version : str
            The version of Clang to download.

        Returns
        -------
        str
            The URL for the specified Clang version.

        Raises
        ------
        ValueError
            If the version number could not be found.
        """
        result = f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{version}.tar.gz"
        response = requests.head(result, timeout=30, allow_redirects=True)
        if response.status_code >= 400:
            raise ValueError(f"Clang version {version} not found.")

        print(f"Clang URL: {result}")
        return result

    @staticmethod
    def get_ninja_version(version: str) -> str:
        """Parse the version specifier for the Ninja generator.

        Parameters
        ----------
        version : str
            The version specifier to parse

        Returns
        -------
        str
            The version of Ninja to download.

        Raises
        ------
        ValueError
            If the version number was invalid or could not be detected.
        """
        if version == "latest":
            url = "https://github.com/ninja-build/ninja/releases"
            response = requests.get(url, timeout=30)
            soup = BeautifulSoup(response.text, "html.parser")
            pattern = re.compile(
                r"v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
            )
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = pattern.match(link.text)
                if regex:
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch"))
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest Ninja version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid Ninja version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        major, minor, _ = regex.groups()
        if int(major) < 1 or (int(major) == 1 and int(minor) < 11):
            raise ValueError("Ninja version must be 1.11 or greater.")

        return version

    @staticmethod
    def get_ninja_url(version: str) -> str:
        """Return the URL for the specified Ninja version.

        Parameters
        ----------
        version : str
            The version of Ninja to download.

        Returns
        -------
        str
            The URL for the specified Ninja version.

        Raises
        ------
        ValueError
            If the version number could not be found.
        """
        result = f"https://github.com/ninja-build/ninja/archive/refs/tags/v{version}.tar.gz"
        response = requests.head(result, timeout=30, allow_redirects=True)
        if response.status_code >= 400:
            raise ValueError(f"Ninja version {version} not found.")

        print(f"Ninja URL: {result}")
        return result

    @staticmethod
    def get_cmake_version(version: str) -> str:
        """Parse the version specifier for the CMake build tool.

        Parameters
        ----------
        version : str
            The version specifier to parse

        Returns
        -------
        str
            The version of CMake to download.

        Raises
        ------
        ValueError
            If the version number was invalid or could not be detected.
        """
        if version == "latest":
            url = "https://github.com/Kitware/CMake/releases"
            response = requests.get(url, timeout=30)
            soup = BeautifulSoup(response.text, "html.parser")
            pattern = re.compile(
                r"v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)(-rc(?P<candidate>\d+))?"
            )
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = pattern.match(link.text)
                if regex and not regex.group("candidate"):
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch")),
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest CMake version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid CMake version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        major, minor, _ = regex.groups()
        if int(major) < 3 or (int(major) == 3 and int(minor) < 28):
            raise ValueError("CMake version must be 3.28 or greater.")

        return version

    @staticmethod
    def get_cmake_url(version: str) -> str:
        """Return the URL for the specified CMake version.

        Parameters
        ----------
        version : str
            The version of CMake to download.

        Returns
        -------
        str
            The URL for the specified CMake version.

        Raises
        ------
        ValueError
            If the version number could not be found.
        """
        result = f"https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}.tar.gz"
        response = requests.head(result, timeout=30, allow_redirects=True)
        if response.status_code >= 400:
            raise ValueError(f"CMake version {version} not found.")

        print(f"CMake URL: {result}")
        return result

    @staticmethod
    def get_python_version(version: str) -> str:
        """Parse the version specifier for Python.

        Parameters
        ----------
        version : str
            The version specifier to parse

        Returns
        -------
        str
            The version of Python to download.

        Raises
        ------
        ValueError
            If the version number was invalid or could not be detected.
        """
        if version == "latest":
            url = "https://www.python.org/downloads/"
            response = requests.get(url, timeout=30)
            soup = BeautifulSoup(response.text, "html.parser")
            pattern = re.compile(
                r"Python (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
            )
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = pattern.match(link.text)
                if regex:
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch"))
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest Python version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = Environment.SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid Python version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        # TODO: determine minimum version number
        major, minor, _ = regex.groups()
        if int(major) < 3 or (int(major) == 3 and int(minor) < 12):
            raise ValueError("Python version must be 3.12 or greater.")

        return version

    @staticmethod
    def get_python_url(version: str) -> str:
        """Return the URL for the specified Python version.

        Parameters
        ----------
        version : str
            The version of Python to download.

        Returns
        -------
        str
            The URL for the specified Python version.

        Raises
        ------
        ValueError
            If the version number could not be found.
        """
        result = f"https://www.python.org/ftp/python/{version}/Python-{version}.tar.xz"
        response = requests.head(result, timeout=30, allow_redirects=True)
        if response.status_code >= 400:
            raise ValueError(f"Python version {version} not found.")

        print(f"Python URL: {result}")
        return result

    def __init__(
        self,
        cwd: Path,
        name: str = "venv",
        *,
        compiler: str = "gcc",
        compiler_version: str = "latest",
        generator: str = "ninja",
        generator_version: str = "latest",
        cmake_version: str = "latest",
        python_version: str = "latest",
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
        if self.compiler == "gcc":
            self.compiler_version = self.get_gcc_version(compiler_version)
            self.compiler_url = self.get_gcc_url(self.compiler_version)
        elif self.compiler == "clang":
            self.compiler_version = self.get_clang_version(compiler_version)
            self.compiler_url = self.get_clang_url(self.compiler_version)
        else:
            raise ValueError("Compiler must be 'gcc' or 'clang'.")

        self.generator = generator
        if self.generator == "ninja":
            self.generator_version = self.get_ninja_version(generator_version)
            self.generator_url = self.get_ninja_url(self.generator_version)
        else:
            raise ValueError("Generator must be 'ninja'.")

        self.cmake_version = self.get_cmake_version(cmake_version)
        self.cmake_url = self.get_cmake_url(self.cmake_version)
        self.python_version = self.get_python_version(python_version)
        self.python_url = self.get_python_url(self.python_version)

        self.cwd = cwd
        self.venv = cwd / name
        self.bin = self.venv / "bin"
        self.include = self.venv / "include"
        self.lib = self.venv / "lib"
        self.workers = workers or os.cpu_count() or 1
        if self.workers < 1:
            raise ValueError("workers must be a positive integer.")

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
        archive = self.venv / f"ninja-{self.generator_version}.tar.gz"
        install = self.venv / f"ninja-{self.generator_version}"
        urlretrieve(self.generator_url, archive, self.ProgressBar("Downloading Ninja"))
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

        If any errors are encountered while parsing the file, the program will exit
        with a return code of 1.
        """
        commands = []

        # save the current environment variables
        for key, value in os.environ.items():
            commands.append(f"export {Environment.OLD_PREFIX}{key}=\"{value}\"")

        try:
            # read new variables from the TOML file
            with env_file.open("rb") as file:
                data = tomllib.load(file)
        except Exception as error:  # pylint: disable=broad-except
            print(error)
            sys.exit(1)

        # [vars] get exported directly
        for key, val_str in data.get("vars", {}).items():
            if not isinstance(val_str, str):
                print(f"ValueError: value for {key} must be a string.")
                sys.exit(1)
            commands.append(f'export {key}=\"{val_str}\"')

        # [paths] get appended to the existing paths if possible
        for key, val_list in data.get("paths", {}).items():
            if not isinstance(val_list, list) or not all(isinstance(v, str) for v in val_list):
                print(f"ValueError: value for {key} must be a list of strings.")
                sys.exit(1)
            if os.environ.get(key, None):
                fragment = os.pathsep.join([*val_list, os.environ[key]])
            else:
                fragment = os.pathsep.join(val_list)
            commands.append(f'export {key}=\"{fragment}\"')

        for command in commands:
            print(command)

    @staticmethod
    def deactivate() -> None:
        """Restore the environment variables to their original state.

        Notes
        -----
        When the activate() method is called, the environment variables are saved
        with a prefix of "_OLD_VIRTUAL_" to prevent conflicts.  This method undoes that
        by transferring the value from the prefixed variable back to the original, and
        then clearing the temporary variable.

        If the variable did not exist before the environment was activated, it will
        clear it without replacement.
        """
        for key, value in os.environ.items():
            if key.startswith(Environment.OLD_PREFIX):
                print(f'export {key.removeprefix(Environment.OLD_PREFIX)}=\"{value}\"')
                print(f'unset {key}')
            elif f"{Environment.OLD_PREFIX}{key}" not in os.environ:
                print(f'unset {key}')

    def create_env_file(self) -> None:
        """Create a configuration file that stores environment variables to be loaded
        into the virtual environment upon activation.

        See Also
        --------
        Environment.activate
            Specifies the format for the configuration file and convention for
            sourcing it into the environment.

        Notes
        -----
        By default, this populates the file with correct paths to the installed
        compilers, headers, and libraries.  Users can modify this file to include
        additional environment variables as needed.
        """
        env_file = self.venv / "env.toml"
        with env_file.open("w") as file:
            file.write(f"""
[vars]
CC = "{self.bin / self.compiler}"
CXX = "{self.bin / 'g++'}"  # TODO: pass in correct C++ compiler

[paths]
PATH = ["{self.bin}"]
LIBRARY_PATH = ["{self.lib}", "{self.lib / 'lib64'}"]
LD_LIBRARY_PATH = ["{self.lib}", "{self.lib / 'lib64'}"]
            """)

    def create_activation_script(self) -> None:
        """Create an activation script that enters the virtual environment."""
        script = self.venv / "activate"
        with script.open("w") as file:
            file.write(f"""
# This file must be used with "source bertrand/activate" *from bash*
# You cannot run it directly

# Exit immediately if a command exits with a non-zero status
set -e

# on Windows, a path can contain colons and backslashes and has to be converted:
if [ "${{OSTYPE:-}}" = "cygwin" ] || [ "${{OSTYPE:-}}" = "msys" ]; then
    # transform D:\\path\\to\\venv to /d/path/to/venv on MSYS
    # and to /cygdrive/d/path/to/venv on Cygwin
    THIS_VIRTUAL_ENV=$(cygpath "{self.venv}")
else
    # use the path as-is
    THIS_VIRTUAL_ENV="{self.venv}"
fi

# virtual environments cannot be nested
if [ -n "$VIRTUAL_ENV" ]; then
    if [ "$VIRTUAL_ENV" == "$THIS_VIRTUAL_ENV" ]; then
        exit 0
    else
        echo "Deactivating existing virtual environment at $VIRTUAL_ENV"
        deactivate
    fi
fi

# save the previous variables and set those from env.toml
eval "$(python -m bertrand activate "$THIS_VIRTUAL_ENV/env.toml")"

export VIRTUAL_ENV="$THIS_VIRTUAL_ENV"
export BERTRAND_HOME="$THIS_VIRTUAL_ENV"
export PYTHONHOME="$THIS_VIRTUAL_ENV"

if [ -z "${{VIRTUAL_ENV_DISABLE_PROMPT:-}}" ] ; then
    export PS1="({self.venv.name}) ${{PS1:-}}"
    export VIRTUAL_ENV_PROMPT="({self.venv.name}) "
fi

deactivate() {{
    # reset old environment variables
    eval "$(python -m bertrand deactivate)"

    # Call hash to forget past commands.  Without forgetting past commands,
    # the $PATH changes we made may not be respected.
    hash -r 2> /dev/null
}}

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
                self.install_gcc()  # TODO: check to see if binaries are already installed and skip if so
                self.install_ninja()
                self.install_cmake()
                self.install_python()
                self.create_env_file()
                self.create_activation_script()
            except subprocess.CalledProcessError as error:
                shutil.rmtree(self.venv)
                print(error.stderr)
                raise
            except:
                shutil.rmtree(self.venv)
                raise
            flag.touch()

        print(f"Elapsed time: {time.time() - start:.2f} seconds")


# TODO: current_env should probably return an actual Environment object, with values
# pulled from the configuration file.


# TODO: current_env should be a class method of Environment -> Environment.current()


def current_env() -> Path | None:
    """Return a path to the current virtual environment, if one exists.

    Returns
    -------
    Path or None
        The path to the current virtual environment, or None if no environment is
        currently active.

    Notes
    -----
    This function checks for the presence of a `BERTRAND_HOME` environment variable
    within the current shell.  If it exists, the function returns its value as a
    Path object.  The truthiness of the return value is sufficient to determine if a
    virtual environment is currently active.
    """
    if "BERTRAND_HOME" in os.environ:
        return Path(os.environ["BERTRAND_HOME"])
    return None


def get_bin() -> list[str]:
    """Return a list of paths to the binary directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the binary directories of the current virtual environment.
    """
    env = current_env()
    if not env:
        return []

    return list(map(str, [
        env / "bin",
    ]))


def get_include() -> list[str]:
    """Return a list of paths to the include directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the include directories of the current virtual environment.
    """
    env = current_env()
    if not env:
        return []

    return [
        str(env / "include"),
        numpy.get_include(),
        pybind11.get_include(),
    ]


def get_lib() -> list[str]:
    """Return a list of paths to the library directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the library directories of the current virtual environment.
    """
    env = current_env()
    if not env:
        return []

    return list(map(str, [
        env / "lib",
        env / "lib64",
    ]))


def get_link() -> list[str]:
    """Return a list of link symbols for the current virtual environment.

    Returns
    -------
    list[str]
        A list of link symbols for the current virtual environment.
    """
    env = current_env()
    if not env:
        return []

    return [
        f"-lpython{sysconfig.get_python_version()}",
    ]
