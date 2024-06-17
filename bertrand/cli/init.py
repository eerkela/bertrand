"""Create Python/C/C++ virtual environments using the Bertrand CLI."""
import functools
import os
import re
import shutil
import stat
import subprocess
import tarfile
import time
from pathlib import Path
from typing import Any

import packaging.version
import requests
import tomlkit
from bs4 import BeautifulSoup
from tqdm import tqdm

from bertrand import __version__
from .env import Environment, env


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

    def __init__(self, name: str, version: Version) -> None:
        self.name = name
        self.version = version
        self.build = True

    def __bool__(self) -> bool:
        return self.build

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

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, v)
        self.url = f"https://ftp.gnu.org/gnu/binutils/binutils-{self.version}.tar.xz"
        if not ping(self.url):
            raise ValueError(f"ld/gold version {self.version} not found.")
        print(f"{self.name} URL: {self.url}")

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
                    "--enable-shared",
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
        env.info["linker"] = self.name
        env.info["linker_version"] = str(self.version)
        env.vars["LD"] = str(env / "bin" / f"ld.{self.name}")
        env.flags["LDFLAGS"].append(f"-fuse-ld={self.name}")


class GCC(Target):
    """A strategy for installing the GCC compiler into a virtual environment."""

    RELEASES = "https://gcc.gnu.org/releases.html"
    PATTERN = re.compile(
        rf"^\s*GCC\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("14.1.0")

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest GCC version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid GCC version.  Must be set to 'latest' or a version specifier "
                "of the form 'X.Y.Z'."
            )

        if v < self.MIN_VERSION:
            raise ValueError(f"GCC version must be {self.MIN_VERSION} or greater.")

        super().__init__(name, normalize_version(v))
        self.url = f"https://ftpmirror.gnu.org/gnu/gcc/gcc-{self.version}/gcc-{self.version}.tar.xz"
        if not ping(self.url):
            raise ValueError(f"GCC version {self.version} not found.")
        print(f"GCC URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"gcc-{self.version}.tar.xz"
        source = env / f"gcc-{self.version}"
        build = env / f"gcc-{self.version}-build"
        try:
            download(self.url, archive, "Downloading GCC")
            extract(archive, env.dir, " Extracting GCC")
            archive.unlink()
            subprocess.check_call(["./contrib/download_prerequisites"], cwd=source)
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / "configure"),
                    f"--prefix={env.dir}",
                    f"--with-ld={env / 'bin' / 'ld.gold'}",
                    "--enable-languages=c,c++",
                    "--enable-shared",
                    "--enable-lto",
                    "--enable-checking=release",
                    "--disable-werror",
                    "--disable-multilib",
                    "--disable-bootstrap",
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

    def push(self) -> None:
        """Update the environment with the config for the GCC compiler."""
        env.info["c_compiler"] = "gcc"
        env.info["cxx_compiler"] = "g++"
        env.info["compiler_version"] = str(self.version)
        env.vars["CC"] = str(env / "bin" / "gcc")
        env.vars["CXX"] = str(env / "bin" / "g++")
        env.vars["AR"] = str(env / "bin" / "gcc-ar")
        env.vars["NM"] = str(env / "bin" / "gcc-nm")
        env.vars["RANLIB"] = str(env / "bin" / "gcc-ranlib")
        env.paths["LIBRARY_PATH"].append(env / "lib64")
        env.paths["LD_LIBRARY_PATH"].append(env / "lib64")


class Clang(Target):
    """A strategy for installing the Clang compiler into a virtual environment."""

    RELEASES = "https://github.com/llvm/llvm-project/releases"
    PATTERN = re.compile(
        rf"^\s*LLVM\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("18.0.0")
    TARGETS = {
        "clang": ["clang", "compiler-rt", "openmp"],
        "clangtools": ["clang-tools-extra"],
        "lld": ["lld"],
    }

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, normalize_version(v))
        self.targets = self.TARGETS[name]
        self.url = f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{self.version}.tar.gz"  # pylint: disable=line-too-long
        if not ping(self.url):
            raise ValueError(f"Clang version {self.version} not found.")
        if "clang" in self.targets:
            print(f"Clang URL: {self.url}")
        if "lld" in self.targets:
            print(f"lld URL: {self.url}")
        if "clangtools" in self.targets:
            print(f"Clangtools URL: {self.url}")

    def __call__(self, workers: int, **kwargs: Any) -> None:
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
                    "Unix Makefiles",
                    f"-DLLVM_ENABLE_PROJECTS={';'.join(self.targets)}",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DBUILD_SHARED_LIBS=ON",
                    f"-DLLVM_BINUTILS_INCDIR={env / 'include'}",
                    str(source / 'llvm'),
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

    def push(self) -> None:
        """Update the environment with the config for the Clang compiler."""
        if "clang" in self.targets:
            env.info["c_compiler"] = "clang"
            env.info["cxx_compiler"] = "clang++"
            env.info["compiler_version"] = str(self.version)
            env.vars["CC"] = str(env / "bin" / "clang")
            env.vars["CXX"] = str(env / "bin" / "clang++")
            env.vars["AR"] = str(env / "bin" / "llvm-ar")
            env.vars["NM"] = str(env / "bin" / "llvm-nm")
            env.vars["RANLIB"] = str(env / "bin" / "llvm-ranlib")
        if "lld" in self.targets:
            env.info["linker"] = "lld"
            env.info["linker_version"] = str(self.version)
            env.vars["LD"] = str(env / "bin" / "ld.lld")
            env.flags["LDFLAGS"].append("-fuse-ld=lld")
        if "clangtools" in self.targets:
            env.info["clangtools"] = str(self.version)


class Ninja(Target):
    """Strategy for installing the Ninja build system into a virtual environment."""

    RELEASES = "https://github.com/ninja-build/ninja/releases"
    PATTERN = re.compile(
        rf"^\s*v(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("1.11.0")

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, normalize_version(v))
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
        env.info["generator"] = self.name
        env.info["generator_version"] = str(self.version)


class CMake(Target):
    """Strategy for installing the CMake build system into a virtual environment."""

    RELEASES = "https://github.com/Kitware/CMake/releases"
    PATTERN = re.compile(
        rf"^\s*v(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("3.28.0")

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, normalize_version(v))
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
        env.info["build_system"] = self.name
        env.info["build_system_version"] = str(self.version)


class Mold(Target):
    """Strategy for installing the mold linker into a virtual environment."""

    RELEASES = "https://github.com/rui314/mold/releases"
    PATTERN = re.compile(
        rf"^\s*mold\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, normalize_version(v))
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
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    f"-B{str(build)}",
                ],
                cwd=source
            )
            subprocess.check_call([cmake, "--build", ".", f"-j{workers}"], cwd=build)
            subprocess.check_call([cmake, "--build", ".", "--target", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push(self) -> None:
        """Update the environment with the config for the mold linker."""
        env.info["linker"] = "mold"
        env.info["linker_version"] = str(self.version)
        env.vars["LD"] = str(env / "bin" / "ld.mold")
        env.flags["LDFLAGS"].append("-fuse-ld=mold")


class Valgrind(Target):
    """Strategy for installing the Valgrind memory checker into a virtual environment."""

    RELEASES = "https://sourceware.org/pub/valgrind/"
    PATTERN = re.compile(
        rf"^\s*valgrind-(?P<version>{packaging.version.VERSION_PATTERN})\.tar\.bz2\s*$",
        re.IGNORECASE | re.VERBOSE
    )

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            versions = scrape_versions(self.RELEASES, self.PATTERN, 1)
            if not versions:
                raise ValueError(
                    "Could not detect latest Valgrind version.  Please specify a version "
                    "number manually."
                )
            v = versions[0]
        elif VERSION_PATTERN.match(version):
            v = Version(version)
        else:
            raise ValueError(
                "Invalid Valgrind version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        super().__init__(name, normalize_version(v))
        self.url = f"https://sourceware.org/pub/valgrind/valgrind-{self.version}.tar.bz2"
        if not ping(self.url):
            raise ValueError(f"Valgrind version {self.version} not found.")
        print(f"Valgrind URL: {self.url}")

    def __call__(self, workers: int) -> None:
        archive = env / f"valgrind-{self.version}.tar.bz2"
        source = env / f"valgrind-{self.version}"
        build = env / f"valgrind-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Valgrind")
            extract(archive, env.dir, " Extracting Valgrind")
            archive.unlink()
            subprocess.check_call(["./autogen.sh"], cwd=source)
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [str(source / "configure"), f"--prefix={env.dir}"],
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

    def push(self) -> None:
        """Update the environment with the config for Valgrind."""
        env.info["valgrind"] = str(self.version)


class Python(Target):
    """Strategy for installing Python into a virtual environment."""

    RELEASES = "https://www.python.org/downloads/"
    PATTERN = re.compile(
        rf"^\s*Python\s*(?P<version>{packaging.version.VERSION_PATTERN})\s*$",
        re.IGNORECASE | re.VERBOSE
    )
    MIN_VERSION = Version("3.9.0")

    def __init__(self, name: str, version: str) -> None:
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

        super().__init__(name, normalize_version(v))
        self.with_valgrind = False
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
                # # TODO: just apply a global filter against the environment directory
                # f"CFLAGS=\"-DBERTRAND_TRACEBACK_EXCLUDE_PYTHON={source}:{build}:{install}\"",
            ]
            if self.with_valgrind:
                configure_args.append("--with-valgrind")
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
        env.flags["LDFLAGS"].append(f"-lpython{self.version.major}.{self.version.minor}")


# TODO: ideally, the Conan target would be subsumed by Bertrand, and would be
# installed as a dependency.


class Conan(Target):
    """Strategy for installing the Conan package manager into a virtual environment."""

    MIN_VERSION = Version("2.0.0")

    def __init__(self, name: str, version: str) -> None:
        pip_versions = pypi_versions("conan")
        if not pip_versions:
            raise RuntimeError("Could not retrieve Conan version numbers from pypi.")

        if version == "latest":
            super().__init__(name, pip_versions[0])
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
            super().__init__(name, v)

    def __call__(self, workers: int) -> None:
        os.environ["CONAN_HOME"] = str(env / ".conan")
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", "pandas"],  # TODO: for some reason, the build fails without this line
        )
        subprocess.check_call(
            [str(env / "bin" / "pip"), "install", f"conan=={self.version}"],
        )

    def push(self) -> None:
        """Update the environment with the config for Conan."""
        env.info["conan"] = str(self.version)
        env.vars["CONAN_HOME"] = str(env / ".conan")


class Bertrand(Target):
    """Strategy for installing the Bertrand package manager into a virtual environment."""

    def __init__(self, name: str, version: str) -> None:
        available = pypi_versions("bertrand")
        if not available:
            raise RuntimeError("Could not retrieve Bertrand version numbers from pypi.")
        if version == "latest":
            super().__init__(name, available[0])
        else:
            v = Version(version)
            if v not in available:
                raise ValueError(
                    f"Invalid Bertrand version '{version}'.  Must be set to 'latest' "
                    f"or a version specifier of the form 'X.Y.Z'."
                )
            super().__init__(name, v)

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

    export: set[Target]

    compiler: GCC | Clang
    generator: Ninja
    build_system: CMake
    linker: Gold | Clang | Mold
    python: Python
    conan: Conan
    bertrand: Bertrand
    clangtools: Clang | None
    valgrind: Valgrind | None

    def __init__(
        self,
        compiler: str,
        compiler_version: str,
        generator: str,
        generator_version: str,
        build_system: str,
        build_system_version: str,
        linker: str,
        linker_version: str,
        python_version: str,
        conan_version: str,
        clangtools_version: str | None,
        valgrind_version: str | None
    ) -> None:
        self.export = set()

        if compiler == "gcc":
            self.compiler = GCC(compiler, compiler_version)
            self.export.add(self.compiler)
        elif compiler == "clang":
            self.compiler = Clang(compiler, compiler_version)
            self.export.add(self.compiler)
        else:
            raise ValueError(
                f"Compiler '{compiler}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if generator == "ninja":
            self.generator = Ninja(generator, generator_version)
            self.export.add(self.generator)
        else:
            raise ValueError(
                f"Generator '{generator}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if build_system == "cmake":
            self.build_system = CMake(build_system, build_system_version)
            self.export.add(self.build_system)
        else:
            raise ValueError(
                f"Build tool '{build_system}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if linker == "ld":
            self.linker = Gold(linker, linker_version)
            self.export.add(self.linker)
        elif linker == "gold":
            self.linker = Gold(linker, linker_version)
            self.export.add(self.linker)
        elif linker == "lld":
            self.linker = Clang(linker, linker_version)
            self.export.add(self.linker)
        elif linker == "mold":
            self.linker = Mold(linker, linker_version)
            self.export.add(self.linker)
        else:
            raise ValueError(
                f"Linker '{linker}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        self.python = Python("python", python_version)
        self.export.add(self.python)
        self.conan = Conan("conan", conan_version)
        self.export.add(self.conan)
        self.bertrand = Bertrand("bertrand", __version__)
        self.export.add(self.bertrand)

        self.clangtools = None
        if clangtools_version:
            self.clangtools = Clang("clangtools", clangtools_version)
            self.export.add(self.clangtools)

        self.valgrind = None
        if valgrind_version:
            self.valgrind = Valgrind("valgrind", valgrind_version)
            self.export.add(self.valgrind)

    def consume(self, target: Target) -> None:
        """Remove the target from the export set and mark it as built.

        Parameters
        ----------
        target : Target
            The target to remove.
        """
        target.build = False
        self.export.discard(target)

    def gcc_order(self) -> list[Target]:
        """Get the install order for a GCC-based virtual environment.

        Returns
        -------
        list[Target]
            The sequence of targets to install.
        """
        order: list[Target] = []
        if self.linker.name in ("ld", "gold"):
            order.append(self.linker)
        else:  # TODO: no need to reinstall gold if it's already built
            order.append(Gold("gold", "latest"))
        order.append(self.compiler)
        order.append(self.generator)
        order.append(self.build_system)
        if self.linker.name == "lld":
            order.append(self.linker)
            if self.clangtools and self.linker.version == self.clangtools.version:
                self.consume(self.clangtools)
                self.linker.targets.extend(self.clangtools.targets)  # type: ignore
        elif self.linker.name == "mold":
            order.append(self.linker)
        if self.valgrind:
            order.append(self.valgrind)
            self.python.with_valgrind = True
        order.append(self.python)
        order.append(self.conan)
        order.append(self.bertrand)
        if self.clangtools:
            order.append(self.clangtools)
        return order

    def clang_order(self) -> list[Target]:
        """Get the install order for a Clang-based virtual environment.

        Returns
        -------
        list[Target]
            The sequence of targets to install.
        """
        order: list[Target] = []
        if self.linker.name in ("ld", "gold"):
            order.append(self.linker)
        else:  # TODO: no need to reinstall gold if it's already built
            # NOTE: the LLVMGold.so plugin is required to build Python with LTO using
            # Clang, even if we end up using mold in the final environment.  This
            # appears to be a bug in the Python build system itself.
            order.append(Gold("gold", "latest"))
        order.append(self.compiler)
        if self.linker.name == "lld" and self.linker.version == self.compiler.version:
            self.consume(self.linker)
            self.compiler.targets.extend(self.linker.targets)  # type: ignore
        if self.clangtools and self.clangtools.version == self.compiler.version:
            self.consume(self.clangtools)
            self.compiler.targets.extend(self.clangtools.targets)  # type: ignore
        order.append(self.generator)
        order.append(self.build_system)
        if self.linker.name == "lld" and self.linker:
            order.append(self.linker)
            if self.clangtools and self.clangtools.version == self.linker.version:
                self.consume(self.clangtools)
                self.linker.targets.extend(self.clangtools.targets)  # type: ignore
        elif self.linker.name == "mold":
            order.append(self.linker)
        if self.valgrind:
            order.append(self.valgrind)
            self.python.with_valgrind = True
        order.append(self.python)
        order.append(self.conan)
        order.append(self.bertrand)
        if self.clangtools:
            order.append(self.clangtools)
        return order

    def install(self, workers: int) -> None:
        """Install the virtual environment in the given directory.

        Parameters
        ----------
        workers : int
            The number of workers to use when building the tools.
        """
        # get build order
        if self.compiler.name == "gcc":
            order = self.gcc_order()
        elif self.compiler.name == "clang":
            order = self.clang_order()

        # install recipe
        for target in order:
            if target:
                target(workers)
            if target in self.export:
                target.push()
                self.consume(target)

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
    compiler: str = "gcc",
    compiler_version: str = "latest",
    generator: str = "ninja",
    generator_version: str = "latest",
    build_system: str = "cmake",
    build_system_version: str = "latest",
    linker: str = "mold",
    linker_version: str = "latest",
    python_version: str = "latest",
    conan_version: str = "latest",
    clangtools_version: str | None = None,
    valgrind_version: str | None = None,
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
    compiler : str
        The C/C++ compiler to use.
    compiler_version : str
        The version of the C/C++ compiler.
    generator : str
        The build system generator to use.
    generator_version : str
        The version of the build system generator.
    build_system : str
        The build tool to use.
    build_system_version : str
        The version of the build tool.
    linker : str
        The linker to use.
    linker_version : str
        The version of the linker.
    python_version : str
        The version of Python.
    conan_version : str
        The Conan version to install.
    clangtools_version : str | None
        The version of the clang tools to install, or None if not installing.
    valgrind_version : str | None
        The version of Valgrind to install, or None if not installing.
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
        compiler,
        compiler_version,
        generator,
        generator_version,
        build_system,
        build_system_version,
        linker,
        linker_version,
        python_version,
        conan_version,
        clangtools_version,
        valgrind_version
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
        if "c_compiler" in info and "compiler_version" in info:
            if recipe.compiler.name != info["c_compiler"]:
                raise RuntimeError(
                    f"compiler '{recipe.compiler.name}' does not match existing "
                    f"environment '{info['compiler']}'.  Use `--force` if you want to "
                    f"rebuild the environment from scratch."
                )
            if recipe.compiler.version != Version(info["compiler_version"]):
                raise RuntimeError(
                    f"compiler version '{recipe.compiler.version}' does not match "
                    f"existing environment '{info['compiler_version']}'.  Use "
                    f"`--force` if you want to rebuild the environment from scratch."
                )
            recipe.consume(recipe.compiler)
        if "generator" in info and "generator_version" in info:
            if recipe.generator.name != info["generator"]:
                raise RuntimeError(
                    f"generator '{recipe.generator.name}' does not match existing "
                    f"environment '{info['generator']}'.  Use `--force` if you want "
                    f"to rebuild the environment from scratch."
                )
            if recipe.generator.version != Version(info["generator_version"]):
                raise RuntimeError(
                    f"generator version '{recipe.generator.version}' does not match "
                    f"existing environment '{info['generator_version']}'.  Use "
                    f"`--force` if you want to rebuild the environment from scratch."
                )
            recipe.consume(recipe.generator)
        if "build_system" in info and "build_system_version" in info:
            if recipe.build_system.name != info["build_system"]:
                raise RuntimeError(
                    f"build system '{recipe.build_system.name}' does not match "
                    f"existing environment '{info['build_system']}'.  Use `--force` "
                    f"if you want to rebuild the environment from scratch."
                )
            # pylint: disable=line-too-long
            if recipe.build_system.version != Version(info["build_system_version"]):
                raise RuntimeError(
                    f"build system version '{recipe.build_system.version}' does not "
                    f"match existing environment '{info['build_system_version']}'.  "
                    f"Use `--force` if you want to rebuild the environment from "
                    f"scratch."
                )
            recipe.consume(recipe.build_system)
        if "linker" in info and "linker_version" in info:
            if recipe.linker.name != info["linker"]:
                raise RuntimeError(
                    f"linker '{recipe.linker.name}' does not match existing "
                    f"environment '{info['linker']}'.  Use `--force` if you want to "
                    f"rebuild the environment from scratch."
                )
            if recipe.linker.version != Version(info["linker_version"]):
                raise RuntimeError(
                    f"linker version '{recipe.linker.version}' does not match "
                    f"existing environment '{info['linker_version']}'.  Use `--force` "
                    f"if you want to rebuild the environment from scratch. "
                )
            recipe.consume(recipe.linker)
        if "python" in info:
            if recipe.python.version != Version(info["python"]):
                raise RuntimeError(
                    f"Python version '{recipe.python.version}' does not match "
                    f"existing environment '{info['python']}'.  Use `--force` if "
                    f"you want to rebuild the environment from scratch."
                )
            recipe.consume(recipe.python)
        if "conan" in info:
            if recipe.conan.version != Version(info["conan"]):
                raise RuntimeError(
                    f"Conan version '{recipe.conan.version}' does not match "
                    f"existing environment '{info['conan']}'.  Use `--force` if "
                    f"you want to rebuild the environment from scratch."
                )
            recipe.consume(recipe.conan)
        if recipe.clangtools and "clangtools" in info:
            if recipe.clangtools.version != Version(info["clangtools"]):
                raise RuntimeError(
                    f"Clang tools version '{recipe.clangtools.version}' does not "
                    f"match existing environment '{info['clangtools']}'.  Use "
                    f"`--force` if you want to rebuild the environment from scratch. "
                )
            recipe.consume(recipe.clangtools)
        if recipe.valgrind and "valgrind" in info:
            if recipe.valgrind.version != Version(info["valgrind"]):
                raise RuntimeError(
                    f"Valgrind version '{recipe.valgrind.version}' does not match "
                    f"existing environment '{info['valgrind']}'.  Use `--force` if "
                    f"you want to rebuild the environment from scratch."
                )
            recipe.consume(recipe.valgrind)

        # install the recipe
        recipe.install(workers)

    finally:

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
