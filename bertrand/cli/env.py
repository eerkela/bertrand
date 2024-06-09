"""Create and manage Python/C/C++ virtual environments using the Bertrand CLI."""
from __future__ import annotations

import functools
import os
import re
import shutil
import stat
import subprocess
import sys
import sysconfig
import tarfile
import time
from pathlib import Path
from typing import Any, Iterator

import numpy
import packaging.version
import pybind11
import requests
import tomlkit
from bs4 import BeautifulSoup
from tqdm import tqdm


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

    def __call__(self, venv: Path, workers: int) -> None:
        raise NotImplementedError()

    def push_toml(self, venv: Path) -> None:
        pass

    def push_environment(self, venv: Path) -> None:
        pass


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

    def install_gmp(self, venv: Path, workers: int) -> None:
        """Install GNU MP (a.k.a. GMP) into a virtual environment.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment to install GMP into.
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

        archive = venv / f"gmp-{version}.tar.xz"
        source = venv / f"gmp-{version}"
        build = venv / f"gmp-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/gmp/gmp-{version}.tar.xz",
                archive,
                "Downloading GMP"
            )
            extract(archive, venv, " Extracting GMP")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call([str(source / 'configure'), f"--prefix={venv}"], cwd=build)
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def install_mpfr(self, venv: Path, workers: int) -> None:
        """Install the MPFR library into a virtual environment.

        This depends on GMP.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment to install MPFR into.
        workers : int
            The number of workers to use when building MPFR.

        Raises
        ------
        ValueError
            If the latest MPFR version cannot be detected.
        """
        self.install_gmp(venv, workers)
        versions = scrape_versions(self.MPFR_RELEASES, self.MPFR_PATTERN, 1)
        if not versions:
            raise ValueError("Could not detect latest MPFR version.")
        version = normalize_version(versions[0])

        archive = venv / f"mpfr-{version}.tar.xz"
        source = venv / f"mpfr-{version}"
        build = venv / f"mpfr-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/mpfr/mpfr-{version}.tar.xz",
                archive,
                "Downloading MPFR"
            )
            extract(archive, venv, " Extracting MPFR")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / 'configure'),
                    f"--prefix={venv}",
                    f"--with-gmp={venv}",
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

    def install_texinfo(self, venv: Path, workers: int) -> None:
        """Install the GNU Texinfo system into a virtual environment.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment to install Texinfo into.
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

        archive = venv / f"texinfo-{version}.tar.xz"
        source = venv / f"texinfo-{version}"
        build = venv / f"texinfo-{version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/texinfo/texinfo-{version}.tar.xz",
                archive,
                "Downloading Texinfo"
            )
            extract(archive, venv, " Extracting Texinfo")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call([str(source / 'configure'), f"--prefix={venv}"], cwd=build)
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def __call__(self, venv: Path, workers: int) -> None:
        self.install_mpfr(venv, workers)
        self.install_texinfo(venv, workers)
        archive = venv / f"binutils-{self.version}.tar.xz"
        source = venv / f"binutils-{self.version}"
        build = venv / f"binutils-{self.version}-build"
        try:
            download(
                f"https://ftp.gnu.org/gnu/binutils/binutils-{self.version}.tar.xz",
                archive,
                "Downloading Binutils"
            )
            extract(archive, venv, " Extracting Binutils")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / 'configure'),
                    f"--prefix={venv}",
                    "--enable-gold",
                    "--enable-plugins",
                    "--enable-shared",
                    "--disable-werror",
                    f"--with-gmp={venv}",
                    f"--with-mpfr={venv}",
                ],
                cwd=build,
            )
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the ld/gold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            # TODO: may need to symlink ld.gold to gold
            env = tomlkit.load(f)
        env["info"]["linker"] = self.name  # type: ignore
        env["info"]["linker_version"] = str(self.version)  # type: ignore
        env["vars"]["LD"] = str(venv / "bin" / self.name)  # type: ignore
        env["flags"]["LDFLAGS"].extend([f"-fuse-ld={self.name}"])  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)

    def push_environment(self, venv: Path) -> None:
        """Update the current environment with the config for the ld/gold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the ld/gold linker.
        """
        os.environ["LD"] = str(venv / "bin" / self.name)  # TODO: same as above
        os.environ["LDFLAGS"] = " ".join(
            [os.environ["LDFLAGS"], f"-fuse-ld={self.name}"]
            if os.environ.get("LDFLAGS", None) else
            [f"-fuse-ld={self.name}"]
        )


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

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"gcc-{self.version}.tar.xz"
        source = venv / f"gcc-{self.version}"
        build = venv / f"gcc-{self.version}-build"
        try:
            download(self.url, archive, "Downloading GCC")
            extract(archive, venv, " Extracting GCC")
            archive.unlink()
            subprocess.check_call(["./contrib/download_prerequisites"], cwd=source)
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / "configure"),
                    f"--prefix={venv}",
                    "--enable-languages=c,c++",
                    "--enable-shared",
                    "--disable-werror",
                    "--disable-multilib",
                    "--disable-bootstrap"
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

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the GCC compiler.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["c_compiler"] = "gcc"  # type: ignore
        env["info"]["cxx_compiler"] = "g++"  # type: ignore
        env["info"]["compiler_version"] = str(self.version)  # type: ignore
        env["vars"]["CC"] = str(venv / "bin" / "gcc")  # type: ignore
        env["vars"]["CXX"] = str(venv / "bin" / "g++")  # type: ignore
        env["vars"]["AR"] = str(venv / "bin" / "gcc-ar")  # type: ignore
        env["vars"]["NM"] = str(venv / "bin" / "gcc-nm")  # type: ignore
        env["vars"]["RANLIB"] = str(venv / "bin" / "gcc-ranlib")  # type: ignore
        env["paths"]["LIBRARY_PATH"].append(str(venv / "lib64"))  # type: ignore
        env["paths"]["LD_LIBRARY_PATH"].append(str(venv / "lib64"))  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)

    def push_environment(self, venv: Path) -> None:
        """Update the current environment with the config for the GCC compiler.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the GCC compiler.
        """
        os.environ["CC"] = str(venv / "bin" / "gcc")
        os.environ["CXX"] = str(venv / "bin" / "g++")
        os.environ["AR"] = str(venv / "bin" / "gcc-ar")
        os.environ["NM"] = str(venv / "bin" / "gcc-nm")
        os.environ["RANLIB"] = str(venv / "bin" / "gcc-ranlib")
        lib64 = venv / "lib64"
        os.environ["LIBRARY_PATH"] = os.pathsep.join(
            [str(lib64), os.environ["LIBRARY_PATH"]]
            if os.environ.get("LIBRARY_PATH", None) else
            [str(lib64)]
        )
        os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(lib64), os.environ["LD_LIBRARY_PATH"]]
            if os.environ.get("LD_LIBRARY_PATH", None) else
            [str(lib64)]
        )


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
        self.url = f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{self.version}.tar.gz"
        if not ping(self.url):
            raise ValueError(f"Clang version {self.version} not found.")

    def __call__(self, venv: Path, workers: int, **kwargs: Any) -> None:
        archive = venv / f"clang-{self.version}.tar.gz"
        source = venv / f"llvm-project-llvmorg-{self.version}"
        build = venv / f"clang-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Clang")
            extract(archive, venv, " Extracting Clang")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    "cmake",
                    "-G",
                    "Unix Makefiles",
                    f"-DLLVM_ENABLE_PROJECTS={';'.join(self.targets)}",
                    f"-DCMAKE_INSTALL_PREFIX={venv}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DBUILD_SHARED_LIBS=ON",
                    f"-DLLVM_BINUTILS_INCDIR={venv / 'include'}",
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

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the Clang compiler.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        if "clang" in self.targets:
            env["info"]["c_compiler"] = "clang"  # type: ignore
            env["info"]["cxx_compiler"] = "clang++"  # type: ignore
            env["info"]["compiler_version"] = str(self.version)  # type: ignore
            env["vars"]["CC"] = str(venv / "bin" / "clang")  # type: ignore
            env["vars"]["CXX"] = str(venv / "bin" / "clang++")  # type: ignore
            env["vars"]["AR"] = str(venv / "bin" / "llvm-ar")  # type: ignore
            env["vars"]["NM"] = str(venv / "bin" / "llvm-nm")  # type: ignore
            env["vars"]["RANLIB"] = str(venv / "bin" / "llvm-ranlib")  # type: ignore
        if "lld" in self.targets:
            env["info"]["linker"] = "lld"  # type: ignore
            env["info"]["linker_version"] = str(self.version)  # type: ignore
            env["vars"]["LD"] = str(venv / "bin" / "lld")  # type: ignore  # TODO: check for lld/ld.lld
            env["flags"]["LDFLAGS"].extend([f"-fuse-ld={self.name}"])  # type: ignore
        if "clang-tools-extra" in self.targets:
            env["info"]["clangtools"] = str(self.version)  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)

    def push_environment(self, venv: Path) -> None:
        """Update the environment with paths to the Clang targets that were built.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment containing the Clang targets.
        """
        if "clang" in self.targets:
            os.environ["CC"] = str(venv / "bin" / "clang")
            os.environ["CXX"] = str(venv / "bin" / "clang++")
            os.environ["AR"] = str(venv / "bin" / "llvm-ar")
            os.environ["NM"] = str(venv / "bin" / "llvm-nm")
            os.environ["RANLIB"] = str(venv / "bin" / "llvm-ranlib")
        if "lld" in self.targets:
            os.environ["LD"] = str(venv / "bin" / "lld")  # TODO: same as above
            os.environ["LDFLAGS"] = " ".join(
                [os.environ["LDFLAGS"], f"-fuse-ld={self.name}"]
                if os.environ.get("LDFLAGS", None) else
                [f"-fuse-ld={self.name}"]
            )


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

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"ninja-{self.version}.tar.gz"
        install = venv / f"ninja-{self.version}"
        try:
            download(self.url, archive, "Downloading Ninja")
            extract(archive, venv, " Extracting Ninja")
            archive.unlink()
            subprocess.check_call(
                [str(install / "configure.py"), "--bootstrap"],
                cwd=install
            )
            shutil.move(install / "ninja", venv / "bin" / "ninja")
        finally:
            archive.unlink(missing_ok=True)
            if install.exists():
                shutil.rmtree(install)

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the Ninja build system.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["generator"] = self.name  # type: ignore
        env["info"]["generator_version"] = str(self.version)  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)


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
        self.url = f"https://github.com/Kitware/CMake/releases/download/v{self.version}/cmake-{self.version}.tar.gz"
        if not ping(self.url):
            raise ValueError(f"CMake version {self.version} not found.")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"cmake-{self.version}.tar.gz"
        source = venv / f"cmake-{self.version}"
        build = venv / f"cmake-{self.version}-build"
        try:
            download(self.url, archive, "Downloading CMake")
            extract(archive, venv, " Extracting CMake")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                [
                    str(source / "bootstrap"),
                    f"--prefix={venv}",
                    "--generator=Ninja",
                    f"--parallel={workers}",
                ],
                cwd=build
            )
            subprocess.check_call([str(venv / "bin" / "ninja"), f"-j{workers}"], cwd=build)
            subprocess.check_call([str(venv / "bin" / "ninja"), "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the CMake build system.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["build_system"] = self.name  # type: ignore
        env["info"]["build_system_version"] = str(self.version)  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)


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

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"mold-{self.version}.tar.gz"
        source = venv / f"mold-{self.version}"
        build = venv / f"mold-{self.version}-build"
        try:
            download(self.url, archive, "Downloading mold")
            extract(archive, venv, " Extracting mold")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            cmake = str(venv / "bin" / "cmake")
            subprocess.check_call(
                [
                    cmake,
                    "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={venv}",
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

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the mold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["linker"] = self.name  # type: ignore
        env["info"]["linker_version"] = str(self.version)  # type: ignore
        env["vars"]["LD"] = str(venv / "bin" / self.name)  # type: ignore
        env["flags"]["LDFLAGS"].extend([f"-fuse-ld={self.name}"])  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)

    def push_environment(self, venv: Path) -> None:
        """Update the current environment with the config for the mold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the mold linker.
        """
        os.environ["LD"] = str(venv / "bin" / self.name)
        os.environ["LDFLAGS"] = " ".join(
            [os.environ["LDFLAGS"], f"-fuse-ld={self.name}"]
            if os.environ.get("LDFLAGS", None) else
            [f"-fuse-ld={self.name}"]
        )


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

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"valgrind-{self.version}.tar.bz2"
        source = venv / f"valgrind-{self.version}"
        build = venv / f"valgrind-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Valgrind")
            extract(archive, venv, " Extracting Valgrind")
            archive.unlink()
            subprocess.check_call(["./autogen.sh"], cwd=source)
            build.mkdir(parents=True, exist_ok=True)
            subprocess.check_call([str(source / "configure"), f"--prefix={venv}"], cwd=build)
            subprocess.check_call(["make", f"-j{workers}"], cwd=build)
            subprocess.check_call(["make", "install"], cwd=build)
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for Valgrind.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["valgrind"] = str(self.version)  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)


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

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"Python-{self.version}.tar.xz"
        source = venv / f"Python-{self.version}"
        build = venv / f"Python-{self.version}-build"
        try:
            download(self.url, archive, "Downloading Python")
            extract(archive, venv, " Extracting Python")
            archive.unlink()
            build.mkdir(parents=True, exist_ok=True)
            configure_args = [
                str(source / "configure"),
                "--prefix",
                str(venv),
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
            os.symlink(venv / "bin" / "python3", venv / "bin" / "python")
            os.symlink(venv / "bin" / "pip3", venv / "bin" / "pip")
        finally:
            archive.unlink(missing_ok=True)
            if source.exists():
                shutil.rmtree(source)
            if build.exists():
                shutil.rmtree(build)

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for Python.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["python"] = str(self.version)  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)


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
            p_version = Version(version)
            if p_version not in pip_versions:
                raise ValueError(
                    f"Invalid Conan version '{version}'.  Must be set to 'latest' or a "
                    f"version specifier of the form 'X.Y.Z'."
                )
            if p_version < self.MIN_VERSION:
                raise ValueError(
                    f"Conan version must be {self.MIN_VERSION} or greater."
                )
            super().__init__(name, p_version)

    def __call__(self, venv: Path, workers: int) -> None:
        self.push_environment(venv)
        subprocess.check_call(
            [
                str(venv / "bin" / "pip"),
                "install",
                f"conan=={self.version}"
            ],
            cwd=venv
        )

    def push_toml(self, venv: Path) -> None:
        """Update the toml file with the config for Conan.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("r") as f:
            env = tomlkit.load(f)
        env["info"]["conan"] = str(self.version)  # type: ignore
        env["vars"]["CONAN_HOME"] = str(venv / ".conan")  # type: ignore
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(env, f)

    def push_environment(self, venv: Path) -> None:
        """Update the current environment with the config for Conan.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing Conan.
        """
        os.environ["CONAN_HOME"] = str(venv / ".conan")



# TODO: Bertrand target sets an environment variable before building, which gets
# caught in setup.py and triggers the compilation of extension modules.  The same
# environment variable is set within the virtual environment as part of env.toml,
# and bertrand is installed in headless mode without it.



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
    # bertrand: Bertrand
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
            print(f"GCC URL: {self.compiler.url}")
        elif compiler == "clang":
            self.compiler = Clang(compiler, compiler_version)
            self.export.add(self.compiler)
            print(f"Clang URL: {self.compiler.url}")
        else:
            raise ValueError(
                f"Compiler '{compiler}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if generator == "ninja":
            self.generator = Ninja(generator, generator_version)
            self.export.add(self.generator)
            print(f"Ninja URL: {self.generator.url}")
        else:
            raise ValueError(
                f"Generator '{generator}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if build_system == "cmake":
            self.build_system = CMake(build_system, build_system_version)
            self.export.add(self.build_system)
            print(f"CMake URL: {self.build_system.url}")
        else:
            raise ValueError(
                f"Build tool '{build_system}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        if linker == "ld":
            self.linker = Gold(linker, linker_version)
            self.export.add(self.linker)
            print(f"ld URL: {self.linker.url}")
        elif linker == "gold":
            self.linker = Gold(linker, linker_version)
            self.export.add(self.linker)
            print(f"gold URL: {self.linker.url}")
        elif linker == "lld":
            self.linker = Clang(linker, linker_version)
            self.export.add(self.linker)
            print(f"lld URL: {self.linker.url}")
        elif linker == "mold":
            self.linker = Mold(linker, linker_version)
            self.export.add(self.linker)
            print(f"mold URL: {self.linker.url}")
        else:
            raise ValueError(
                f"Linker '{linker}' not recognized.  Run "
                f"`$ bertrand init --help` for options."
            )

        self.python = Python("python", python_version)
        self.export.add(self.python)
        print(f"Python URL: {self.python.url}")

        self.conan = Conan("conan", conan_version)
        self.export.add(self.conan)

        # self.bertrand = Bertrand("bertrand", bertrand_version)  # TODO: get version from this environment
        # self.export.add(self.bertrand)
        # print(f"Bertrand command: {self.bertrand.command}")

        self.clangtools = None
        if clangtools_version:
            self.clangtools = Clang("clangtools", clangtools_version)
            self.export.add(self.clangtools)
            print(f"Clangtools URL: {self.clangtools.url}")

        self.valgrind = None
        if valgrind_version:
            self.valgrind = Valgrind("valgrind", valgrind_version)
            self.export.add(self.valgrind)
            print(f"Valgrind URL: {self.valgrind.url}")

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
        # order.append(self.bertrand)
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
        # order.append(Make("make", "latest"))
        if self.linker.name in ("ld", "gold"):
            order.append(self.linker)
        elif self.linker.name == "mold" and self.compiler:
            # NOTE: the LLVMGold.so plugin is required to build Python with LTO using
            # Clang, even if we end up using mold in the final environment.  This
            # appears to be a bug in the Python build system itself.
            order.append(Gold("gold", "latest"))  # TODO: fails unless I use 2.37 or below
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
        # order.append(self.bertrand)
        if self.clangtools:
            order.append(self.clangtools)
        return order

    def install(self, venv: Path, workers: int) -> None:
        """Install the virtual environment in the given directory.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment.
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
                target(venv, workers)
            if target in self.export:
                target.push_environment(venv)
                target.push_toml(venv)
                self.consume(target)

        # write activation script
        file = venv / "activate"
        with file.open("w") as f:
            f.write(f"""
# This file must be used with "source venv/activate" *from bash*
# You cannot run it directly

# Exit immediately if a command exits with non-zero status
set -e

# on Windows, a path can contain colons and backslashes and has to be converted:
if [ "${{OSTYPE:-}}" == "cygwin" ] || [ "${{OSTYPE:-}}" == "msys" ]; then
    # transform D:\\path\\to\\venv into /d/path/to/venv on MSYS
    # and to /cygdrive/d/path/to/venv on Cygwin
    THIS_VIRTUAL_ENV=$(cygpath "{venv}")
else
    THIS_VIRTUAL_ENV="{venv}"
fi

# virtual environments cannot be nested
if [ -n "$VIRTUAL_ENV" ]; then
    if [ "$VIRTUAL_ENV" == "$THIS_VIRTUAL_ENV" ]; then
        exit 0
    else
        echo "Deactivating virtual environment at ${{VIRTUAL_ENV}}"
        deactivate
    fi
fi

# save the previous variables and set those from env.toml
eval "$(bertrand activate "${{THIS_VIRTUAL_ENV}}/env.toml")"

export VIRTUAL_ENV="${{THIS_VIRTUAL_ENV}}"
export BERTRAND_HOME="${{THIS_VIRTUAL_ENV}}"
export PYTHONHOME="${{THIS_VIRTUAL_ENV}}"

if [ -z "${{VIRTUAL_ENV_DISABLE_PROMPT:-}}" ] ; then
    export PS1="({venv.name}) ${{PS1:-}}"
    export VIRTUAL_ENV_PROMPT="({venv.name}) "
fi

deactivate() {{
    eval "$(bertrand deactivate)"
    hash -r 2> /dev/null
}}

# Call hash to forget past commands.  Without forgetting past commands,
# the $PATH changes we made may not be respected.
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
    if os.environ.get("BERTRAND_HOME", None):
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

    # remove old environment if force installing
    venv = cwd / name
    if force and venv.exists():
        shutil.rmtree(venv)
    venv.mkdir(parents=True, exist_ok=True)

    # check if an environment already exists and skip previously built targets
    toml_path = venv / "env.toml"
    if toml_path.exists():
        with toml_path.open("r") as f:
            env = dict(tomlkit.load(f)["info"])  # type: ignore
            if "c_compiler" in env and "compiler_version" in env:
                if recipe.compiler.name != env["c_compiler"]:
                    raise RuntimeError(
                        f"compiler '{recipe.compiler.name}' does not match existing "
                        f"environment '{env['compiler']}'.  Use `--force` if you want "
                        f"to rebuild the environment from scratch."
                    )
                if recipe.compiler.version != Version(env["compiler_version"]):  # type: ignore
                    raise RuntimeError(
                        f"compiler version '{recipe.compiler.version}' does not match "
                        f"existing environment '{env['compiler_version']}'.  Use "
                        f"`--force` if you want to rebuild the environment from "
                        f"scratch."
                    )
                recipe.consume(recipe.compiler)
                recipe.compiler.push_environment(venv)
            if "generator" in env and "generator_version" in env:
                if recipe.generator.name != env["generator"]:
                    raise RuntimeError(
                        f"generator '{recipe.generator.name}' does not match existing "
                        f"environment '{env['generator']}'.  Use `--force` if you want "
                        f"to rebuild the environment from scratch."
                    )
                if recipe.generator.version != Version(env["generator_version"]):  # type: ignore
                    raise RuntimeError(
                        f"generator version '{recipe.generator.version}' does not match "
                        f"existing environment '{env['generator_version']}'.  Use "
                        f"`--force` if you want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.generator)
                recipe.generator.push_environment(venv)
            if "build_system" in env and "build_system_version" in env:
                if recipe.build_system.name != env["build_system"]:
                    raise RuntimeError(
                        f"build system '{recipe.build_system.name}' does not match "
                        f"existing environment '{env['build_system']}'.  Use `--force` "
                        f"if you want to rebuild the environment from scratch."
                    )
                if recipe.build_system.version != Version(env["build_system_version"]):  # type: ignore
                    raise RuntimeError(
                        f"build system version '{recipe.build_system.version}' does not "
                        f"match existing environment '{env['build_system_version']}'.  "
                        f"Use `--force` if you want to rebuild the environment from "
                        f"scratch."
                    )
                recipe.consume(recipe.build_system)
                recipe.build_system.push_environment(venv)
            if "linker" in env and "linker_version" in env:
                if recipe.linker.name != env["linker"]:
                    raise RuntimeError(
                        f"linker '{recipe.linker.name}' does not match existing "
                        f"environment '{env['linker']}'.  Use `--force` if you want to "
                        f"rebuild the environment from scratch."
                    )
                if recipe.linker.version != Version(env["linker_version"]):  # type: ignore
                    raise RuntimeError(
                        f"linker version '{recipe.linker.version}' does not match "
                        f"existing environment '{env['linker_version']}'.  Use `--force` "
                        f"if you want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.linker)
                recipe.linker.push_environment(venv)
            if "python" in env:
                if recipe.python.version != Version(env["python"]):  # type: ignore
                    raise RuntimeError(
                        f"Python version '{recipe.python.version}' does not match "
                        f"existing environment '{env['python']}'.  Use `--force` if you "
                        f"want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.python)
                recipe.python.push_environment(venv)
            if "conan" in env:
                if recipe.conan.version != Version(env["conan"]):  # type: ignore
                    raise RuntimeError(
                        f"Conan version '{recipe.conan.version}' does not match "
                        f"existing environment '{env['conan']}'.  Use `--force` if you "
                        f"want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.conan)
                recipe.conan.push_environment(venv)
            if recipe.clangtools and "clangtools" in env:
                if recipe.clangtools.version != Version(env["clangtools"]):  # type: ignore
                    raise RuntimeError(
                        f"Clang tools version '{recipe.clangtools.version}' does not "
                        f"match existing environment '{env['clangtools']}'.  Use "
                        f"`--force` if you want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.clangtools)
                recipe.clangtools.push_environment(venv)
            if recipe.valgrind and "valgrind" in env:
                if recipe.valgrind.version != Version(env["valgrind"]):  # type: ignore
                    raise RuntimeError(
                        f"Valgrind version '{recipe.valgrind.version}' does not match "
                        f"existing environment '{env['valgrind']}'.  Use `--force` if you "
                        f"want to rebuild the environment from scratch."
                    )
                recipe.consume(recipe.valgrind)
                recipe.valgrind.push_environment(venv)
    else:
        with toml_path.open("w") as f:
            empty: dict[str, dict[str, Any]] = {
                "info": {},
                "vars": {},
                "paths": {
                    "PATH": [str(venv / "bin")],
                    "CPATH": [str(venv / "include")],
                    "LIBRARY_PATH": [str(venv / "lib")],
                    "LD_LIBRARY_PATH": [str(venv / "lib")],
                },
                "flags": {
                    "CFLAGS": [],
                    "CXXFLAGS": [],
                    "LDFLAGS": [],
                }
            }
            tomlkit.dump(empty, f)

    old_environment = os.environ.copy()
    os.environ["PATH"] = os.pathsep.join(
        [str(venv / "bin"), os.environ["PATH"]]
        if os.environ.get("PATH", None) else
        [str(venv / "bin")]
    )
    os.environ["CPATH"] = os.pathsep.join(
        [str(venv / "include"), os.environ["CPATH"]]
        if os.environ.get("CPATH", None) else
        [str(venv / "include")]
    )
    os.environ["LIBRARY_PATH"] = os.pathsep.join(
        [str(venv / "lib"), os.environ["LIBRARY_PATH"]]
        if os.environ.get("LIBRARY_PATH", None) else
        [str(venv / "lib")]
    )
    os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(venv / "lib"), os.environ["LD_LIBRARY_PATH"]]
        if os.environ.get("LD_LIBRARY_PATH", None) else
        [str(venv / "lib")]
    )

    # install the recipe
    try:
        recipe.install(venv, workers)
    except subprocess.CalledProcessError as error:
        print(error.stderr)
        raise
    (venv / "built").touch(exist_ok=True)

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


###########################
####    ENVIRONMENT    ####
###########################


class Environment:
    """A class that initializes a virtual environment with a full C++ compiler suite
    and a custom Python install to match.
    """

    # TODO: Environment should have properties that mirror the structure of the env.toml
    # file, so that it can be modified and reflected in both the environment and the
    # config file.  Then, the file can be dumped easily.

    OLD_PREFIX = "_OLD_VIRTUAL_"

    def __init__(self, file: Path) -> None:
        self.file = file
        if not self.file.exists():
            with file.open("rb") as f:
                self.content: tomlkit.TOMLDocument | None = tomlkit.load(f)
        else:
            self.content: tomlkit.TOMLDocument | None = None  # type: ignore

    @classmethod
    def current(cls) -> Environment | None:
        """Get the current virtual environment or None if no environment is active.

        Returns
        -------
        Environment | None
            None if no virtual environment could be found for the active process,
            otherwise an instance of this class representing the current environment.

        Notes
        -----
        This will locate the current environment by searching for a BERTRAND_HOME
        environment variable, which points to the root directory of the virtual
        environment.  It will then source the env.toml file in that directory to
        fill in the configuration for the resulting instance.

        There may be multiple Environment objects referencing the same virtual
        environment 
        """
        if os.environ.get("BERTRAND_HOME", None):
            return cls(Path(os.environ["BERTRAND_HOME"]) / "env.toml")
        return None

    def activate(self) -> None:
        """Print the sequence of bash commands used to enter the virtual environment.

        Raises
        ------
        ValueError
            If no environment file was found.

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
        if self.content is None:
            raise ValueError("Environment file not found.")

        commands = []

        # save the current environment variables
        for key, value in os.environ.items():
            commands.append(f"export {Environment.OLD_PREFIX}{key}=\"{value}\"")

        # [vars] get exported directly
        for key, val_str in self.content.get("vars", {}).items():
            if not isinstance(val_str, str):
                print(f"ValueError: value for {key} must be a string.")
                sys.exit(1)
            commands.append(f'export {key}=\"{val_str}\"')

        # [paths] get appended to the existing paths if possible
        for key, val_list in self.content.get("paths", {}).items():
            if not isinstance(val_list, list) or not all(isinstance(v, str) for v in val_list):
                print(f"ValueError: value for {key} must be a list of strings.")
                sys.exit(1)
            if os.environ.get(key, None):
                fragment = os.pathsep.join([*val_list, os.environ[key]])
            else:
                fragment = os.pathsep.join(val_list)
            commands.append(f'export {key}=\"{fragment}\"')

        # [flags] get appended to the existing flags if possible
        for key, val_list in self.content.get("flags", {}).items():
            if not isinstance(val_list, list) or not all(isinstance(v, str) for v in val_list):
                print(f"ValueError: value for {key} must be a list of strings.")
                sys.exit(1)
            if os.environ.get(key, None):
                fragment = " ".join([*val_list, os.environ[key]])
            else:
                fragment = " ".join(val_list)
            commands.append(f'export {key}=\"{fragment}\"')

        for command in commands:
            print(command)

    @staticmethod
    def deactivate() -> None:
        """Print the sequence of bash commands used to exit the virtual environment.

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

    # TODO: modifications made to the environment should be forwarded to the
    # env.toml file.

    def save(self, file: Path) -> None:
        """Write the current environment to a TOML file.

        Parameters
        ----------
        file : Path
            The path to the file to write the environment to.

        Raises
        ------
        ValueError
            If no environment file was found.
        """
        if self.content is None:
            raise ValueError("Environment file not found.")

        with file.open("w") as f:
            tomlkit.dump(self.content, f)

    def __len__(self) -> int:
        return len(self.content)

    def __getitem__(self, key: str) -> Any:
        return os.environ[key]

    def __setitem__(self, key: str, value: Any) -> None:
        os.environ[key] = value

    def __delitem__(self, key: str) -> None:
        del os.environ[key]

    def __contains__(self, key: str) -> bool:
        return key in os.environ

    def __iter__(self) -> Iterator[str]:
        return iter(os.environ)

    def __str__(self) -> str:
        return str(self.content)

    def __repr__(self) -> str:
        return repr(self.content)

    def __bool__(self) -> bool:
        return bool(self.content)











def get_bin() -> list[str]:
    """Return a list of paths to the binary directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the binary directories of the current virtual environment.
    """
    env = Environment.current()
    if not env:
        return []

    # TODO: Can't use div operator?  This should probably be enabled?

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
    env = Environment.current()
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
    env = Environment.current()
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
    env = Environment.current()
    if not env:
        return []

    return [
        f"-lpython{sysconfig.get_python_version()}",
    ]
