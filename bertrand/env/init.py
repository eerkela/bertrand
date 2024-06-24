"""Create Python/C/C++ virtual environments using the Bertrand CLI."""
import functools
import os
import re
import shutil
import stat
import subprocess
import sysconfig
import tarfile
import time
from pathlib import Path
from typing import Any

import packaging.version
import requests
import tomlkit
from bs4 import BeautifulSoup
from tqdm import tqdm

from .version import __version__
from .environment import Environment, env


Version = packaging.version.Version


BAR_COLOR = "#bfbfbf"
BAR_FORMAT = "{l_bar}{bar}| [{elapsed}, {rate_fmt}{postfix}]"
TIMEOUT = 30
VERSION_PATTERN = re.compile(
    rf"^\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
    re.IGNORECASE | re.VERBOSE
)


def download(url: str, path: Path, title: str) -> None:
    """Download a file from a URL and save it to a local path.

    Parameters
    ----------
    url : str
        The URL to download the file from.
    path : Path
        The local path to save the downloaded file to.
    title : str
        The title to display on the left side of the progress bar.

    Raises
    ------
    ValueError
        If the HTTP request returns an error code.
    """
    response = requests.get(
        url,
        timeout=TIMEOUT,
        stream=True,
        allow_redirects=True
    )
    if response.status_code >= 400:
        raise ValueError(f"Could not download file from {url}")

    path.parent.mkdir(parents=True, exist_ok=True)
    response.raw.read = functools.partial(response.raw.read, decode_content=True)
    with tqdm.wrapattr(
        response.raw,
        "read",
        total=int(response.headers.get("content-length", 0)),
        desc=title,
        colour=BAR_COLOR,
        bar_format=BAR_FORMAT,
    ) as r:
        with path.open("wb") as f:
            shutil.copyfileobj(r, f)


def extract(archive: Path, dest: Path, title: str) -> None:
    """Extract the contents of an archive to a destination directory.

    Parameters
    ----------
    archive : Path
        The path to the archive file to extract.
    dest : Path
        The path to the directory to extract the archive to.
    title : str
        The title to display on the left side of the progress bar.
    """
    with tarfile.open(archive) as tar:
        for member in tqdm(
            tar.getmembers(),
            desc=title,
            total=len(tar.getmembers()),
            colour=BAR_COLOR,
            bar_format=BAR_FORMAT,
        ):
            tar.extract(member, dest, filter="data")


def ping(url: str) -> bool:
    """Ping a URL to check if it's reachable.

    Parameters
    ----------
    url : str
        The URL to ping.

    Returns
    -------
    bool
        True if the URL is reachable, False otherwise.
    """
    try:
        response = requests.head(url, timeout=TIMEOUT, allow_redirects=True)
        return response.status_code < 400
    except requests.exceptions.RequestException:
        return False


def scrape_versions(
    url: str,
    pattern: re.Pattern[str],
    limit: int | None = None
) -> list[Version]:
    """Scrape version numbers from a webpage.

    Parameters
    ----------
    url : str
        The URL to scrape version numbers from.
    pattern : re.Pattern
        A compiled regular expression pattern to match version numbers.
    limit : int, optional
        The maximum number of version numbers to return.  Supports negative indexing.

    Returns
    -------
    list[Version]
        A sorted list of version numbers scraped from the webpage, with the most recent
        version first.
    """
    response = requests.get(url, timeout=TIMEOUT)
    soup = BeautifulSoup(response.text, "html.parser")
    versions: list[Version] = []
    for link in soup.find_all("a"):
        regex = pattern.match(link.text)
        if regex:
            v = Version(regex.group("version"))
            if not v.is_prerelease:
                versions.append(v)

    versions.sort(reverse=True)
    if limit is None:
        return versions
    return versions[:limit]


def pypi_versions(package_name: str, limit: int | None = None) -> list[Version]:
    """Get the version numbers for a package on PyPI.

    Parameters
    ----------
    package_name : str
        The name of the package to get the version numbers for.
    limit : int, optional
        The maximum number of version numbers to return.  Supports negative indexing.

    Returns
    -------
    list[Version]
        A sorted list of version numbers for the package on PyPI, with the most recent
        version first.
    """
    response = requests.get(f"https://pypi.org/pypi/{package_name}/json", timeout=TIMEOUT)
    if response.status_code >= 400:
        return []

    versions: list[Version] = []
    for r in response.json()["releases"]:
        v = Version(r)
        if not v.is_prerelease:
            versions.append(v)

    versions.sort(reverse=True)
    if limit is None:
        return versions
    return versions[:limit]


def normalize_version(version: Version) -> Version:
    """Expand a version specifier to a full version number.

    Parameters
    ----------
    version : Version
        The version specifier to normalize.

    Returns
    -------
    Version
        The same version with any missing MAJOR.MINOR.MICRO parts filled with zeros.
    """
    return Version(f"{version.major}.{version.minor}.{version.micro}")


class Target:
    """Abstract base class for build targets when installing a virtual environment."""

    def __init__(self, version: Version) -> None:
        self.version = version
        self.build = True

    def __call__(self, workers: int) -> None:
        raise NotImplementedError()

    def push(self) -> None:
        """Update the env.toml file for the build target and export any changes to
        the build environment.  Does nothing unless overridden in a subclass.
        """


class Gold(Target):
    """Strategy for installing the LD and gold linkers into a virtual environment."""

    GMP_RELEASES = "https://ftp.gnu.org/gnu/gmp/"
    GMP_PATTERN = re.compile(
        rf"^\s*gmp-(?P<version>{packaging.version.VERSION_PATTERN})\.tar\.xz\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MPFR_RELEASES = "https://ftp.gnu.org/gnu/mpfr/"
    MPFR_PATTERN = re.compile(
        rf"^\s*mpfr-(?P<version>{packaging.version.VERSION_PATTERN})\.tar\.xz\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    TEXINFO_RELEASES = "https://ftp.gnu.org/gnu/texinfo/"
    TEXINFO_PATTERN = re.compile(
        rf"^\s*texinfo-(?P<version>{packaging.version.VERSION_PATTERN})\.tar\.xz\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    BINUTILS_RELEASES = "https://ftp.gnu.org/gnu/binutils/"
    BINUTILS_PATTERN = re.compile(
        rf"^\s*binutils-(?P<version>{packaging.version.VERSION_PATTERN})\.tar\.xz\s*$",
        re.IGNORECASE | re.VERBOSE
    )

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.BINUTILS_RELEASES, self.BINUTILS_PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest ld/gold version.  Please specify a "
                    "version number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid ld/gold version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        super().__init__(v)
        self.url = f"https://ftp.gnu.org/gnu/binutils/binutils-{self.version}.tar.xz"
        if not ping(self.url):
            raise ValueError(f"ld/gold version {self.version} not found.")
        print(f"binutils URL: {self.url}")

    def install_gmp(self, workers: int) -> None:
        """Install GNU MP (a.k.a. GMP) into a virtual environment.

        Parameters
        ----------
        workers : int
            The number of workers to use when building GMP.

        Raises
        ------
        ValueError
            If the latest GMP version cannot be detected.
        """
        versions = scrape_versions(self.GMP_RELEASES, self.GMP_PATTERN, 1)
        if not versions:
            raise ValueError("Could not detect latest GMP version.")
        version = normalize_version(versions[0])

        archive = env / f"gmp-{version}.tar.xz"
        source = env / f"gmp-{version}"
        build = env / f"gmp-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/gmp/gmp-{version}.tar.xz",
                archive,
                "Downloading GMP"
            )
            extract(archive, env.dir, " Extracting GMP")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [str(source / 'configure'), f"--prefix={env.dir}"],
                cwd=build
            )
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def install_mpfr(self, workers: int) -> None:
        """Install the MPFR library into a virtual environment.

        This depends on GMP.

        Parameters
        ----------
        workers : int
            The number of workers to use when building MPFR.

        Raises
        ------
        ValueError
            If the latest MPFR version cannot be detected.
        """
        self.install_gmp(workers)
        versions = scrape_versions(self.MPFR_RELEASES, self.MPFR_PATTERN, 1)
        if not versions:
            raise ValueError("Could not detect latest MPFR version.")
        version = normalize_version(versions[0])

        archive = env / f"mpfr-{version}.tar.xz"
        source = env / f"mpfr-{version}"
        build = env / f"mpfr-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/mpfr/mpfr-{version}.tar.xz",
                archive,
                "Downloading MPFR"
            )
            extract(archive, env.dir, " Extracting MPFR")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / 'configure'),
                    f"--prefix={env.dir}",
                    f"--with-gmp={env.dir}",
                ],
                cwd=build
            )
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def install_texinfo(self, workers: int) -> None:
        """Install the GNU Texinfo system into a virtual environment.

        Parameters
        ----------
        workers : int
            The number of workers to use when building Texinfo.

        Raises
        ------
        ValueError
            If the latest Texinfo version cannot be detected.

        Notes
        -----
        Compiling binutils will fail unless this package is installed first.  The error
        is rather ambiguous, but it's due to the lack of a `makeinfo` executable in the
        PATH.  Installing Texinfo will provide this executable.
        """
        versions = scrape_versions(self.TEXINFO_RELEASES, self.TEXINFO_PATTERN, 1)
        if not versions:
            raise ValueError("Could not detect latest texinfo version.")
        version = versions[0]

        archive = env / f"texinfo-{version}.tar.xz"
        source = env / f"texinfo-{version}"
        build = env / f"texinfo-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/texinfo/texinfo-{version}.tar.xz",
                archive,
                "Downloading Texinfo"
            )
            extract(archive, env.dir, " Extracting Texinfo")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [str(source / 'configure'), f"--prefix={env.dir}"],
                cwd=build
            )
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def __call__(self, workers: int) -> None:
        self.install_mpfr(workers)
        self.install_texinfo(workers)
        archive = env / f"binutils-{self.version}.tar.xz"
        source = env / f"binutils-{self.version}"
        build = env / f"binutils-{self.version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/binutils/binutils-{self.version}.tar.xz",
                archive,
                "Downloading Binutils"
            )
            extract(archive, env.dir, " Extracting Binutils")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / 'configure'),
                    f"--prefix={env.dir}",
                    "--enable-gold",
                    "--enable-plugins",
                    "--disable-werror",
                    f"--with-gmp={env.dir}",
                    f"--with-mpfr={env.dir}",
                ],
                cwd=build,
            )
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
            shutil.move(env / "bin" / "ld", env / "bin" / "ld.ld")
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for the ld/gold linker."""
        env.info["gold"] = str(self.version)


class Ninja(Target):
    """Strategy for installing the Ninja build system into a virtual environment."""

    RELEASES = "https://github.com/ninja-build/ninja/releases"
    PATTERN = re.compile(
        rf"^\s*v(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("1.11.0")

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest Ninja version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid Ninja version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        if v < self.MIN_VERSION:
            raise ValueError(f"Ninja version must be {self.MIN_VERSION} or greater.")

        super().__init__(normalize_version(v))
        self.url = f"https://github.com/ninja-build/ninja/archive/refs/tags/v{self.version}.tar.gz"
        if not ping(self.url):
            raise ValueError(f"Ninja version {self.version} not found.")
        print(f"Ninja URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"ninja-{self.version}.tar.gz"
        install = env / f"ninja-{self.version}"
        try:
            download(self.url, archive, "Downloading Ninja")
            extract(archive, env.dir, " Extracting Ninja")
            archive.unlink()
            subprocess.check_call(
                [str(install / "configure.py"), "--bootstrap"],
                cwd=install
            )
            shutil.move(install / "ninja", env / "bin" / "ninja")
        finally:
            archive.unlink(missing_ok=True)
            if install.exists():
                shutil.rmtree(install)

    def push(self) -> None:
        """Update the environment with the config for the Ninja build system."""
        env.info["ninja"] = str(self.version)


class Clang(Target):
    """A strategy for installing the Clang compiler into a virtual environment."""

    RELEASES = "https://github.com/llvm/llvm-project/releases"
    PATTERN = re.compile(
        rf"^\s*LLVM\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("18.0.0")

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest CMake version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid Clang version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        if v < self.MIN_VERSION:
            raise ValueError(f"Clang version must be {self.MIN_VERSION} or greater.")

        super().__init__(normalize_version(v))
        self.url = f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{self.version}.tar.gz"  # pylint: disable=line-too-long
        if not ping(self.url):
            raise ValueError(f"Clang version {self.version} not found.")
        print(f"Clang URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"clang-{self.version}.tar.gz"
        source = env / f"llvm-project-llvmorg-{self.version}"
        build = env / f"clang-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Clang")
            extract(archive, env.dir, " Extracting Clang")
            archive.unlink()

            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    "cmake",
                    "-G",
                    "Ninja",
                    "-C",
                    str(source / "clang" / "cmake" / "caches" / "BOLT-PGO.cmake"),
                    f"-DLLVM_BINUTILS_INCDIR={env / 'include'}",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DLLVM_ENABLE_RUNTIMES=compiler-rt;libcxx;libcxxabi;libunwind",
                    "-DPGO_INSTRUMENT_LTO=Thin",

                    # stage 2
                    f"-DBOOTSTRAP_LLVM_BINUTILS_INCDIR={env / 'include'}",
                    "-DBOOTSTRAP_LLVM_ENABLE_RUNTIMES=compiler-rt;libcxx;libcxxabi;libunwind",
                    "-DBOOTSTRAP_LLVM_ENABLE_LLD=ON",
                    "-DBOOTSTRAP_LLVM_ENABLE_LIBCXX=ON",

                    # stage 3
                    f"-DBOOTSTRAP_BOOTSTRAP_LLVM_BINUTILS_INCDIR={env / 'include'}",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_PROJECTS=all",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_RUNTIMES=all",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_LIBCXX=ON",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_LLD=ON",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_PIC=ON",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_FFI=ON",
                    "-DBOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_RTTI=ON",
                    str(source / 'llvm'),
                ],
                cwd=build
            )
            subprocess.check_call(
                [str(env / "bin" / "ninja"), "stage2-clang-bolt", f"-j{workers}"],
                cwd=build
            )
            subprocess.check_call(
                [str(env / "bin" / "ninja"), "install"],
                cwd=build
            )
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for the Clang compiler."""
        env.info["clang"] = str(self.version)
        env.vars["CC"] = str(env / "bin" / "clang")
        env.vars["CXX"] = str(env / "bin" / "clang++")
        env.vars["AR"] = str(env / "bin" / "llvm-ar")
        env.vars["NM"] = str(env / "bin" / "llvm-nm")
        env.vars["RANLIB"] = str(env / "bin" / "llvm-ranlib")


class CMake(Target):
    """Strategy for installing the CMake build system into a virtual environment."""

    RELEASES = "https://github.com/Kitware/CMake/releases"
    PATTERN = re.compile(
        rf"^\s*v(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("3.28.0")

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest CMake version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid CMake version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        if v < self.MIN_VERSION:
            raise ValueError(f"CMake version must be {self.MIN_VERSION} or greater.")

        super().__init__(normalize_version(v))
        self.url = f"https://github.com/Kitware/CMake/releases/download/v{self.version}/cmake-{self.version}.tar.gz"  # pylint: disable=line-too-long
        if not ping(self.url):
            raise ValueError(f"CMake version {self.version} not found.")
        print(f"CMake URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"cmake-{self.version}.tar.gz"
        source = env / f"cmake-{self.version}"
        build = env / f"cmake-{self.version}-build"
        try:
            download(self.url, archive, "Downloading CMake")
            extract(archive, env.dir, " Extracting CMake")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / "bootstrap"),
                    f"--prefix={env.dir}",
                    "--generator=Ninja",
                    f"--parallel={workers}",
                ],
                cwd=build
            )
            subprocess.check_call(
                [str(env / "bin" / "ninja"), f"-j{workers}"],
                cwd=build
            )
            subprocess.check_call(
                [str(env / "bin" / "ninja"), "install"],
                cwd=build
            )
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for the CMake build system."""
        env.info["cmake"] = str(self.version)


class Mold(Target):
    """Strategy for installing the mold linker into a virtual environment."""

    RELEASES = "https://github.com/rui314/mold/releases"
    PATTERN = re.compile(
        rf"^\s*mold\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest mold version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid mold version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        super().__init__(normalize_version(v))
        self.url = f"https://github.com/rui314/mold/archive/refs/tags/v{self.version}.tar.gz"
        if not ping(self.url):
            raise ValueError(f"mold version {self.version} not found.")
        print(f"mold URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"mold-{self.version}.tar.gz"
        source = env / f"mold-{self.version}"
        build = env / f"mold-{self.version}-build"
        try:
            download(self.url, archive, "Downloading mold")
            extract(archive, env.dir, " Extracting mold")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            cmake = str(env / "bin" / "cmake")
            subprocess.check_call(
                [
                    cmake,
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    f"-B{str(build)}",
                ],
                cwd=source
            )
            subprocess.check_call([str(env / "bin" / "ninja"), f"-j{workers}"], cwd=build)
            subprocess.check_call([str(env / "bin" / "ninja"), "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for the mold linker."""
        env.info["mold"] = str(self.version)
        env.vars["LD"] = str(env / "bin" / "ld.mold")
        env.flags["LDFLAGS"].append("-fuse-ld=mold")


class Python(Target):
    """Strategy for installing Python into a virtual environment."""

    RELEASES = "https://www.python.org/downloads/"
    PATTERN = re.compile(
        rf"^\s*Python\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("3.9.0")

    def __init__(self, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest Python version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid Python version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        if v < self.MIN_VERSION:
            raise ValueError(f"Python version must be {self.MIN_VERSION} or greater.")

        super().__init__(normalize_version(v))
        self.url = f"https://www.python.org/ftp/python/{self.version}/Python-{self.version}.tar.xz"
        if not ping(self.url):
            raise ValueError(f"Python version {self.version} not found.")
        print(f"Python URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"Python-{self.version}.tar.xz"
        source = env / f"Python-{self.version}"
        build = env / f"Python-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Python")
            extract(archive, env.dir, " Extracting Python")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            configure_args = [
                str(source / "configure"),
                "--prefix",
                str(env.dir),
                "--with-ensurepip=upgrade",
                "--enable-shared",
                "--enable-optimizations",
                "--with-lto",
            ]
            if Version(sysconfig.get_python_version()) >= Version("3.12"):
                configure_args.append("--enable-bolt")
            subprocess.check_call(configure_args, cwd=build)
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
            os.symlink(env / "bin" / "python3", env / "bin" / "python")
            os.symlink(env / "bin" / "pip3", env / "bin" / "pip")
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for Python."""
        env.info["python"] = str(self.version)
        env.vars["PYTHONHOME"] = str(env.dir)
        env.paths["CPATH"].append(
            env / "include" / f"python{self.version.major}.{self.version.minor}"
        )
        env.flags["LDFLAGS"].append(f"-lpython{self.version.major}.{self.version.minor}")


# TODO: ideally, the Conan target would be subsumed by Bertrand, and would be
# installed as a dependency.


class Conan(Target):
    """Strategy for installing the Conan package manager into a virtual environment."""

    MIN_VERSION = Version("2.0.0")

    def __init__(self, version: str) -> None:
        pip_versions = pypi_versions("conan")
        if not pip_versions:
            raise RuntimeError("Could not retrieve Conan version numbers from pypi.")

        if version == "latest":
            super().__init__(pip_versions[0])
        else:
            v = Version(version)
            if v not in pip_versions:
                raise ValueError(
                    f"Invalid Conan version '{version}'.  Must be set to 'latest' or a "
                    f"version specifier of the form 'X.Y.Z'."
                )
            if v < self.MIN_VERSION:
                raise ValueError(
                    f"Conan version must be {self.MIN_VERSION} or greater."
                )
            super().__init__(v)

    def __call__(self, workers: int) -> None:
        os.environ["CONAN_HOME"] = str(env / ".conan")

        # NOTE: pip install conan sometimes fails unless pandas is installed first
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", "pandas"],
        )
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", f"conan=={self.version}"],
        )
        subprocess.check_call(
            [str(env / "bin" / "conan"), "profile", "detect"]
        )

    def push(self) -> None:
        """Update the environment with the config for Conan."""
        env.info["conan"] = str(self.version)
        env.vars["CONAN_HOME"] = str(env / ".conan")


class Bertrand(Target):
    """Strategy for installing the Bertrand package manager into a virtual environment."""

    def __init__(self, version: str) -> None:
        available = pypi_versions("bertrand")
        if not available:
            raise RuntimeError("Could not retrieve Bertrand version numbers from pypi.")
        if version == "latest":
            super().__init__(available[0])
        else:
            v = Version(version)
            if v not in available:
                raise ValueError(
                    f"Invalid Bertrand version '{version}'.  Must be set to 'latest' "
                    f"or a version specifier of the form 'X.Y.Z'."
                )
            super().__init__(v)

    def __call__(self, workers: int) -> None:
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", "numpy"],
        )
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", "pybind11"],
        )
        # TODO: eventually, this all boils down to:
        # subprocess.check_call(
        #     [str(env / "bin" / "pip"), "install", f"bertrand=={__version__}"],
        # )

    def push(self) -> None:
        """Update the environment with the config for Bertrand."""
        env.info["bertrand"] = __version__
        np_include = subprocess.run(
            [
                str(env / "bin" / "python"),
                "-c",
                "import numpy; print(numpy.get_include())"
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        pybind11_include = subprocess.run(
            [
                str(env / "bin" / "python"),
                "-c",
                "import pybind11; print(pybind11.get_include())"
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        # TODO: add bertrand include path (although there may not be any headers to include)
        # bertrand_include = subprocess.run(
        #     [
        #         str(env / "bin" / "python"),
        #         "-c",
        #         "import bertrand; print(bertrand.get_include())"
        #     ],
        #     capture_output=True,
        #     text=True,
        #     check=True,
        # )

        env.paths["CPATH"].extend([
            Path(np_include.stdout.strip()),
            Path(pybind11_include.stdout.strip()),
            # Path(bertrand_include.stdout.strip()),
        ])


class Recipe:
    """Determines the correct order in which to install each target in order to
    bootstrap the virtual environment.
    """

    def __init__(
        self,
        clang_version: str,
        ninja_version: str,
        cmake_version: str,
        mold_version: str,
        python_version: str,
        conan_version: str,
    ) -> None:
        self.gold = Gold("latest")
        self.clang = Clang(clang_version)
        self.ninja = Ninja(ninja_version)
        self.cmake = CMake(cmake_version)
        self.mold = Mold(mold_version)
        self.python = Python(python_version)
        self.conan = Conan(conan_version)
        self.bertrand = Bertrand(__version__)

    def install(self, workers: int) -> None:
        """Install the virtual environment in the given directory.

        Parameters
        ----------
        workers : int
            The number of workers to use when building the tools.
        """
        # NOTE: the LLVMGold.so plugin is required to build Python with LTO using
        # Clang, even if we end up using mold in the final environment.  This appears
        # to be a bug in the Python build system itself.
        order = [
            self.gold,
            self.ninja,
            self.clang,
            self.cmake,
            self.mold,
            self.python,
            self.conan,
            self.bertrand,
        ]

        # install recipe
        for target in order:
            if target.build:
                target(workers)
                target.push()

        # write activation script
        file = env / "activate"
        with file.open("w") as f:
            f.write(f"""
# This file must be used with "source venv/activate" *from bash*
# You cannot run it directly

# Check if we are currently inside a (non-bertrand) Python virtual environment
# and preemptively deactivate it
if [ -n "${{VIRTUAL_ENV}}" ] && [ -z "${{BERTRAND_HOME}}" ]; then
    echo "Deactivating current virtual environment at ${{VIRTUAL_ENV}}"
    deactivate
fi

deactivate() {{
    # deactivation script restores all prefixed variables, including paths
    if [ -n "${{BERTRAND_HOME}}" ] ; then
        eval "$(bertrand deactivate)"
    fi

    # Call hash to forget past commands.  Without this, the $PATH changes we
    # made may not be respected.
    hash -r 2> /dev/null

    if [ -n "${{{Environment.OLD_PREFIX}PS1:-}}" ] ; then
        export PS1="${{{Environment.OLD_PREFIX}PS1:-}}"
        unset {Environment.OLD_PREFIX}PS1
    fi

    unset VIRTUAL_ENV
    unset VIRTUAL_ENV_PROMPT
    unset BERTRAND_HOME
    if [ ! "${{1:-}}" == "nondestructive" ] ; then
        # Self destruct!
        unset -f deactivate
    fi
}}

# unset irrelevant variables
deactivate nondestructive

# on Windows, a path can contain colons and backslashes and has to be converted:
if [ "${{OSTYPE:-}}" == "cygwin" ] || [ "${{OSTYPE:-}}" == "msys" ]; then
    # transform D:\\path\\to\\venv into /d/path/to/venv on MSYS
    # and to /cygdrive/d/path/to/venv on Cygwin
    export VIRTUAL_ENV=$(cygpath "{env.dir}")
else
    export VIRTUAL_ENV="{env.dir}"
fi

# save the previous variables and set those from env.toml
export BERTRAND_HOME="${{VIRTUAL_ENV}}"
eval "$(bertrand activate "${{VIRTUAL_ENV}}")"

if [ -z "${{VIRTUAL_ENV_DISABLE_PROMPT:-}}" ] ; then
    {Environment.OLD_PREFIX}PS1="${{PS1:-}}"
    export PS1="({env.dir.name}) ${{PS1:-}}"
    export VIRTUAL_ENV_PROMPT="({env.dir.name}) "
fi

# Call hash to forget past commands.  Without this, the $PATH changes we
# made may not be respected.
hash -r 2> /dev/null
            """)

        # set script permissions
        file.chmod(
            stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |  # Owner: read, write, execute
            stat.S_IRGRP | stat.S_IXGRP |                 # Group: read, execute
            stat.S_IROTH | stat.S_IXOTH                   # Others: read, execute
        )


def init(
    cwd: Path,
    name: str = "venv",
    *,
    clang_version: str = "latest",
    ninja_version: str = "latest",
    cmake_version: str = "latest",
    mold_version: str = "latest",
    python_version: str = "latest",
    conan_version: str = "latest",
    workers: int = 0,
    force: bool = False
) -> None:
    """Initialize a virtual environment in the current directory.

    Parameters
    ----------
    cwd : Path
        The path to the current working directory.
    name : str
        The name of the virtual environment.
    clang_version : str
        The version of the clang compiler.
    ninja_version : str
        The version of the ninja build system.
    cmake_version : str
        The version of the cmake build tool.
    mold_version : str
        The version of the mold linker.
    python_version : str
        The version of Python.
    conan_version : str
        The Conan version to install.
    workers : int
        The number of workers to use when building the tools.
    force : bool
        If true, remove the existing virtual environment and create a new one.

    Raises
    ------
    ValueError
        If any of the inputs are invalid.
    subprocess.CalledProcessError
        If an error occurs during the installation process.
    RuntimeError
        If the `init` command is invoked incorrectly, for instance from inside another
        virtual environment or if the settings do not exactly match an existing
        environment.

    Notes
    -----
    This is the target function for the `bertrand init` CLI command.  It is safe to
    call from Python, but can take a long time to run, and is mostly intended for
    command-line use.
    """
    start = time.time()
    if "BERTRAND_HOME" in os.environ:
        raise RuntimeError(
            "`$ bertrand init` should not be run from inside a virtual environment. "
            "Exit the virtual environment before running this command."
        )

    # validate and normalize inputs
    recipe = Recipe(
        clang_version,
        ninja_version,
        cmake_version,
        mold_version,
        python_version,
        conan_version,
    )
    workers = workers or os.cpu_count() or 1
    if workers < 1:
        raise ValueError("workers must be a positive integer.")

    # set up the environment
    old_environment = os.environ.copy()
    os.environ["BERTRAND_HOME"] = str(cwd / name)

    try:

        # remove old environment if force installing
        if force and env.dir.exists():
            shutil.rmtree(env.dir)
        env.dir.mkdir(parents=True, exist_ok=True)

        # create swap memory for large builds
        swapfile = env / "swapfile"
        size: int | None = None
        print(
            "\n======================================================================\n"
            "Compiling a virtual environment from source is resource-intensive and\n"
            "can take a long time to complete.  It is therefore recommended to create\n"
            "a temporary swap file to ensure that the build process does not run out\n"
            "of memory at any point.\n"
            "\n"
            "How much disk space (in GB) should be allocated for this file\n"
            "(0 avoids creating a swap file)? "
        )
        while size is None:
            try:
                size = int(input())
                if size < 0:
                    print("Please enter a non-negative integer: ")
                    size = None
            except:
                print("Please enter a valid integer: ")
                size = None
        if size:
            subprocess.check_call(["sudo", "fallocate", "-l", f"{size}G", str(swapfile)])
            subprocess.check_call(["sudo", "chmod", "600", str(swapfile)])
            subprocess.check_call(["sudo", "mkswap", str(swapfile)])
            subprocess.check_call(["sudo", "swapon", str(swapfile)])
        print(
            "\n======================================================================\n"
        )

        # initialize env.toml
        if not env.toml.exists():
            with env.toml.open("w") as f:
                doc = tomlkit.document()
                paths = tomlkit.table()
                paths.add("PATH", [str(env.dir / "bin")])
                paths.add("CPATH", [str(env.dir / "include")])
                paths.add("LIBRARY_PATH", [str(env.dir / "lib")])
                paths.add("LD_LIBRARY_PATH", [str(env.dir / "lib")])
                flags = tomlkit.table()
                flags.add("CFLAGS", [])
                flags.add("CXXFLAGS", [])
                flags.add("LDFLAGS", [])
                doc.add("info", tomlkit.table())
                doc.add("vars", tomlkit.table())
                doc.add("paths", paths)
                doc.add("flags", flags)
                doc.add("packages", tomlkit.aot())
                tomlkit.dump(doc, f)

        # export env.toml into the current environment
        env.vars = env.vars.copy()  # type: ignore
        env.paths = env.paths.copy()  # type: ignore
        env.flags = env.flags.copy()  # type: ignore

        # skip previously built targets
        info = env.info
        if "gold" in info:
            recipe.gold.build = False
        if "clang" in info:
            if recipe.clang.version != Version(info["clang"]):
                raise RuntimeError(
                    f"clang version '{recipe.clang.version}' does not match existing "
                    f"environment '{info['clang']}'.  Use `--force` if you want to "
                    f"rebuild the environment from scratch."
                )
            recipe.clang.build = False
        if "ninja" in info:
            if recipe.ninja.version != Version(info["ninja"]):
                raise RuntimeError(
                    f"generator version '{recipe.ninja.version}' does not match "
                    f"existing environment '{info['ninja']}'.  Use `--force` if you "
                    f"want to rebuild the environment from scratch."
                )
            recipe.ninja.build = False
        if "cmake" in info:
            if recipe.cmake.version != Version(info["cmake"]):
                raise RuntimeError(
                    f"build system version '{recipe.cmake.version}' does not match "
                    f"existing environment '{info['cmake']}'.  Use `--force` if you "
                    f"want to rebuild the environment from scratch."
                )
            recipe.cmake.build = False
        if "mold" in info:
            if recipe.mold.version != Version(info["mold"]):
                raise RuntimeError(
                    f"linker version '{recipe.mold.version}' does not match existing "
                    f"environment '{info['mold']}'.  Use `--force` if you want to "
                    f"rebuild the environment from scratch. "
                )
            recipe.mold.build = False
        if "python" in info:
            if recipe.python.version != Version(info["python"]):
                raise RuntimeError(
                    f"Python version '{recipe.python.version}' does not match "
                    f"existing environment '{info['python']}'.  Use `--force` if "
                    f"you want to rebuild the environment from scratch."
                )
            recipe.python.build = False
        if "conan" in info:
            if recipe.conan.version != Version(info["conan"]):
                raise RuntimeError(
                    f"Conan version '{recipe.conan.version}' does not match existing "
                    f"environment '{info['conan']}'.  Use `--force` if you want to "
                    f"rebuild the environment from scratch."
                )
            recipe.conan.build = False

        # install the recipe
        recipe.install(workers)

    finally:

        # remove swap memory
        if swapfile.exists():
            print("\nRemoving temporary swap file...")
            try:
                subprocess.check_call(["sudo", "swapoff", str(swapfile)])
            except subprocess.CalledProcessError:
                pass
            swapfile.unlink(missing_ok=True)

        # restore previous environment
        for k in os.environ:
            if k in old_environment:
                os.environ[k] = old_environment[k]
            else:
                del os.environ[k]

    # print elapsed time
    elapsed = time.time() - start
    hours = elapsed // 3600
    minutes = (elapsed % 3600) // 60
    seconds = elapsed % 60
    if hours:
        print(
            f"Elapsed time: {hours:.0f} hours, {minutes:.0f} minutes, "
            f"{seconds:.2f} seconds"
        )
    elif minutes:
        print(f"Elapsed time: {minutes:.0f} minutes, {seconds:.2f} seconds")
    else:
        print(f"Elapsed time: {seconds:.2f} seconds")
