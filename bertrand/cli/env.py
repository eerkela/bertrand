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
import pybind11
import requests
import tomlkit
from bs4 import BeautifulSoup
from tqdm import tqdm


BAR_COLOR = "#bfbfbf"
BAR_FORMAT = "{l_bar}{bar}| [{elapsed}, {rate_fmt}{postfix}]"
TIMEOUT = 30
SEMVER_REGEX = re.compile(r"(\d+)\.(\d+)\.(\d+)")


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


####################
####    GOLD    ####
####################


def install_gmp(venv: Path, workers: int) -> None:
    """Install GNU MP (a.k.a. GMP) into a virtual environment.

    Parameters
    ----------
    venv : Path
        The path to the virtual environment to install GMP into.
    workers : int
        The number of workers to use when building GMP.
    """
    response = requests.get("https://ftp.gnu.org/gnu/gmp/", timeout=TIMEOUT)
    soup = BeautifulSoup(response.text, "html.parser")
    pattern = re.compile(r"gmp-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz")
    versions: list[tuple[int, int, int]] = []
    for link in soup.find_all("a"):
        regex = pattern.match(link.text)
        if regex:
            versions.append((
                int(regex.group("major")),
                int(regex.group("minor")),
                int(regex.group("patch"))
            ))
    versions.sort(reverse=True)
    version = ".".join(map(str, versions[0]))

    archive = venv / f"gmp-{version}.tar.xz"
    source = venv / f"gmp-{version}"
    build = venv / f"gmp-{version}-build"
    download(f"https://ftp.gnu.org/gnu/gmp/gmp-{version}.tar.xz", archive, "Downloading GMP")
    extract(archive, venv, " Extracting GMP")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([f"{source}/configure", f"--prefix={venv}"], cwd=build)
    subprocess.check_call(["make", f"-j{workers}"], cwd=build)
    subprocess.check_call(["make", "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)


def install_mpfr(venv: Path, workers: int) -> None:
    """Install the MPFR library into a virtual environment.

    This depends on GMP.

    Parameters
    ----------
    venv : Path
        The path to the virtual environment to install MPFR into.
    workers : int
        The number of workers to use when building MPFR.
    """
    install_gmp(venv, workers)

    response = requests.get("https://ftp.gnu.org/gnu/mpfr/", timeout=TIMEOUT)
    soup = BeautifulSoup(response.text, "html.parser")
    pattern = re.compile(r"mpfr-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz")
    versions: list[tuple[int, int, int]] = []
    for link in soup.find_all("a"):
        regex = pattern.match(link.text)
        if regex:
            versions.append((
                int(regex.group("major")),
                int(regex.group("minor")),
                int(regex.group("patch"))
            ))
    versions.sort(reverse=True)
    version = ".".join(map(str, versions[0]))

    archive = venv / f"mpfr-{version}.tar.xz"
    source = venv / f"mpfr-{version}"
    build = venv / f"mpfr-{version}-build"
    download(f"https://ftp.gnu.org/gnu/mpfr/mpfr-{version}.tar.xz", archive, "Downloading MPFR")
    extract(archive, venv, " Extracting MPFR")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            f"{source}/configure",
            f"--prefix={venv}",
            f"--with-gmp={venv}",
        ],
        cwd=build
    )
    subprocess.check_call(["make", f"-j{workers}"], cwd=build)
    subprocess.check_call(["make", "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)


def install_gold(venv: Path, workers: int) -> None:
    """Install the LLVM gold plugin into a virtual environment.

    This depends on both GMP and MPFR.

    Parameters
    ----------
    venv : Path
        The path to the virtual environment to install the LLVM gold plugin into.
    workers : int
        The number of workers to use.
    """
    install_mpfr(venv, workers)

    response = requests.get("https://ftp.gnu.org/gnu/binutils/", timeout=TIMEOUT)
    soup = BeautifulSoup(response.text, "html.parser")
    pattern = re.compile(r"binutils-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz")
    versions: list[tuple[int, int, int]] = []
    for link in soup.find_all("a"):
        regex = pattern.match(link.text)
        if regex:
            versions.append((
                int(regex.group("major")),
                int(regex.group("minor")),
                int(regex.group("patch"))
            ))
    versions.sort(reverse=True)
    version = ".".join(map(str, versions[0]))

    archive = venv / f"binutils-{version}.tar.xz"
    source = venv / f"binutils-{version}"
    build = venv / f"binutils-{version}-build"
    download(
        f"https://ftp.gnu.org/gnu/binutils/binutils-{version}.tar.xz",
        archive,
        "Downloading Binutils"
    )
    extract(archive, venv, " Extracting Binutils")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            f"{source}/configure",
            f"--prefix={venv}",
            "--enable-gold",
            "--enable-plugins",
            "--disable-werror",
            f"--with-gmp={venv}",
            f"--with-mpfr={venv}",
        ],
        cwd=build,
    )
    subprocess.check_call(["make", f"-j{workers}"], cwd=build)
    subprocess.check_call(["make", "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)


###################
####    GCC    ####
###################


# TODO: provide a way to use the lld linker instead of ld, gold, or mold for GCC.
# This is automatically the case for Clang, but GCC would need to download and build
# lld from source, which requires a full build of the LLVM source tree.  This is
# disabled unless you specify --lld=version when building the environment.  Or just
# make lld support Clang-only.


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
        response = requests.get(url, timeout=TIMEOUT)
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

    regex = SEMVER_REGEX.match(version)
    if not regex:
        raise ValueError(
            "Invalid GCC version.  Must be set to 'latest' or a version specifier "
            "of the form 'X.Y.Z'."
        )

    major, _, _ = regex.groups()
    if int(major) < 14:
        raise ValueError("GCC version must be 14 or greater.")

    return version


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
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"GCC version {version} not found.")
    print(f"GCC URL: {result}")
    return result


def install_gcc(
    content: dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path,
    workers: int,
) -> None:
    """Install the GCC compiler into a virtual environment.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install GCC into.
    version : str
        The version of GCC to install.
    url : str
        The URL to download the GCC archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    workers : int
        The number of workers to use when building GCC.
    """
    archive = venv / f"gcc-{version}.tar.xz"
    source = venv / f"gcc-{version}"
    build = venv / f"gcc-{version}-build"
    download(url, archive, "Downloading GCC")
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
    shutil.rmtree(source)
    shutil.rmtree(build)
    content["info"] |= {
        "c_compiler": "gcc",
        "cxx_compiler": "g++",
        "compiler_version": version,
    }
    content["vars"] |= {
        "CC": str(bin_dir / "gcc"),
        "CXX": str(bin_dir / "g++"),
        "AR": str(bin_dir / "gcc-ar"),
        "NM": str(bin_dir / "gcc-nm"),
        "RANLIB": str(bin_dir / "gcc-ranlib"),
    }
    lib64 = venv / "lib64"
    content["paths"]["LIBRARY_PATH"].append(str(lib64))
    content["paths"]["LD_LIBRARY_PATH"].append(str(lib64))


#####################
####    CLANG    ####
#####################


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
        response = requests.get(url, timeout=TIMEOUT)
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

    regex = SEMVER_REGEX.match(version)
    if not regex:
        raise ValueError(
            "Invalid Clang version.  Must be set to 'latest' or a version "
            "specifier of the form 'X.Y.Z'."
        )

    major, _, _ = regex.groups()
    if int(major) < 18:
        raise ValueError("Clang version must be 18 or greater.")

    return version


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
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"Clang version {version} not found.")

    print(f"Clang URL: {result}")
    return result


def install_clang(
    content: dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path,
    workers: int,
) -> None:
    """Install the clang compiler into a virtual environment.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install clang into.
    version : str
        The version of clang to install.
    url : str
        The URL to download the clang archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    workers : int
        The number of workers to use when building clang.
    """
    archive = venv / f"clang-{version}.tar.gz"
    source = venv / f"llvm-project-llvmorg-{version}"
    build = venv / f"clang-{version}-build"
    download(url, archive, "Downloading Clang")
    extract(archive, venv, " Extracting Clang")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            "cmake",
            "-G",
            "Unix Makefiles",
            "-DLLVM_ENABLE_PROJECTS=clang;compiler-rt",  # TODO: add a linter option in CLI
            f"-DCMAKE_INSTALL_PREFIX={venv}",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=ON",
            f"-DLLVM_BINUTILS_INCDIR={venv / 'include'}",
            f"{source}/llvm",
        ],
        cwd=build
    )
    subprocess.check_call(["make", f"-j{workers}"], cwd=build)
    subprocess.check_call(["make", "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)
    content["info"] |= {
        "c_compiler": "clang",
        "cxx_compiler": "clang++",
        "compiler_version": version,
    }
    content["vars"] |= {
        "CC": str(bin_dir / "clang"),
        "CXX": str(bin_dir / "clang++"),
        "AR": str(bin_dir / "llvm-ar"),
        "NM": str(bin_dir / "llvm-nm"),
        "RANLIB": str(bin_dir / "llvm-ranlib"),
    }


#####################
####    NINJA    ####
#####################


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
        response = requests.get(url, timeout=TIMEOUT)
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

    regex = SEMVER_REGEX.match(version)
    if not regex:
        raise ValueError(
            "Invalid Ninja version.  Must be set to 'latest' or a version "
            "specifier of the form 'X.Y.Z'."
        )

    major, minor, _ = regex.groups()
    if int(major) < 1 or (int(major) == 1 and int(minor) < 11):
        raise ValueError("Ninja version must be 1.11 or greater.")

    return version


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
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"Ninja version {version} not found.")

    print(f"Ninja URL: {result}")
    return result


def install_ninja(
    content: dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path
) -> None:
    """Install the ninja build system into a virtual environment.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install ninja into.
    version : str
        The version of ninja to install.
    url : str
        The URL to download the ninja archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    """
    archive = venv / f"ninja-{version}.tar.gz"
    install = venv / f"ninja-{version}"
    download(url, archive, "Downloading Ninja")
    extract(archive, venv, " Extracting Ninja")
    archive.unlink()
    subprocess.check_call([str(install / "configure.py"), "--bootstrap"], cwd=install)
    shutil.move(install / "ninja", bin_dir / "ninja")
    shutil.rmtree(install)
    content["info"]["generator"] = "ninja"
    content["info"]["generator_version"] = version


#####################
####    CMAKE    ####
#####################


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
        response = requests.get(url, timeout=TIMEOUT)
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

    regex = SEMVER_REGEX.match(version)
    if not regex:
        raise ValueError(
            "Invalid CMake version.  Must be set to 'latest' or a version specifier "
            "of the form 'X.Y.Z'."
        )

    major, minor, _ = regex.groups()
    if int(major) < 3 or (int(major) == 3 and int(minor) < 28):
        raise ValueError("CMake version must be 3.28 or greater.")

    return version


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
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"CMake version {version} not found.")

    print(f"CMake URL: {result}")
    return result


# TODO: CMakeLists.txt needs to include add_link_options("-fuse-ld=mold") to all files
# -> Maybe not, since LDFLAGS is set to include it?

def install_cmake(
    content: dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path,
    workers: int
) -> None:
    """Install the cmake build system using Ninja as the backend generator.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install cmake into.
    version : str
        The version of cmake to install.
    url : str
        The URL to download the cmake archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    workers : int
        The number of workers to use when building cmake.
    """
    archive = venv / f"cmake-{version}.tar.gz"
    source = venv / f"cmake-{version}"
    build = venv / f"cmake-{version}-build"
    download(url, archive, "Downloading CMake")
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
    subprocess.check_call([str(bin_dir / "ninja"), f"-j{workers}"], cwd=build)
    subprocess.check_call([str(bin_dir / "ninja"), "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)
    content["info"]["build_tool"] = "cmake"
    content["info"]["build_tool_version"] = version


# TODO: install lld here?


####################
####    MOLD    ####
####################


def get_mold_version(version: str) -> str:
    """Parse the version specifier for the mold linker.

    Parameters
    ----------
    version : str
        The version specifier to parse

    Returns
    -------
    str
        The version of mold to download.

    Raises
    ------
    ValueError
        If the version number was invalid or could not be detected.
    """
    if version == "latest":
        url = "https://github.com/rui314/mold/releases"
        response = requests.get(url, timeout=TIMEOUT)
        soup = BeautifulSoup(response.text, "html.parser")
        pattern = re.compile(
            r"mold (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
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
                "Could not detect latest mold version.  Please specify a version "
                "number manually."
            )
        versions.sort(reverse=True)
        version = ".".join(map(str, versions[0]))

    regex = SEMVER_REGEX.match(version)
    if not regex:
        raise ValueError(
            "Invalid mold version.  Must be set to 'latest' or a version specifier "
            "of the form 'X.Y.Z'."
        )

    return version


def get_mold_url(version: str) -> str:
    """Return the URL for the specified mold version.

    Parameters
    ----------
    version : str
        The version of mold to download.

    Returns
    -------
    str
        The URL for the specified mold version.

    Raises
    ------
    ValueError
        If the version number could not be found.
    """
    result = f"https://github.com/rui314/mold/archive/refs/tags/v{version}.tar.gz"
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"mold version {version} not found.")

    print(f"mold URL: {result}")
    return result


def install_mold(
    content: dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path,
    workers: int
) -> None:
    """Install the mold linker using the bootstrapped compiler and CMake.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install mold into.
    version : str
        The version of mold to install.
    url : str
        The URL to download the mold archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    workers : int
        The number of workers to use when building mold.
    """
    archive = venv / f"mold-{version}.tar.gz"
    source = venv / f"mold-{version}"
    build = venv / f"mold-{version}-build"
    download(url, archive, "Downloading mold")
    extract(archive, venv, " Extracting mold")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    cmake = str(bin_dir / "cmake")
    subprocess.check_call(
        [
            cmake,
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={venv}",
            "-B",
            str(build),
        ],
        cwd=source
    )
    subprocess.check_call([cmake, "--build", ".", f"-j{workers}"], cwd=build)
    subprocess.check_call([cmake, "--build", ".", "--target", "install"], cwd=build)
    shutil.rmtree(source)
    shutil.rmtree(build)
    content["info"]["linker"] = "mold"
    content["info"]["linker_version"] = version
    content["vars"]["LD"] = str(bin_dir / "mold")
    content["flags"]["LDFLAGS"].extend([f"-B{bin_dir}", "-fuse-ld=mold"])


# TODO: maybe this is the best place to install lld?


######################
####    PYTHON    ####
######################


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
        response = requests.get(url, timeout=TIMEOUT)
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

    regex = SEMVER_REGEX.match(version)
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
    response = requests.head(result, timeout=TIMEOUT, allow_redirects=True)
    if response.status_code >= 400:
        raise ValueError(f"Python version {version} not found.")

    print(f"Python URL: {result}")
    return result


def install_python(
    content : dict[str, dict[str, Any]],
    venv: Path,
    version: str,
    url: str,
    bin_dir: Path,
    workers: int
) -> None:
    """Install a custom Python distribution with the selected compiler.

    Parameters
    ----------
    content : dict[str, dict[str, Any]]
        An out parameter holding the eventual contents of the `env.toml` file.
    venv : Path
        The path to the virtual environment to install Python into.
    version : str
        The version of Python to install.
    url : str
        The URL to download the Python archive from.
    bin_dir : Path
        The path to the bin directory in the virtual environment.
    workers : int
        The number of workers to use when building Python.
    """
    archive = venv / f"Python-{version}.tar.xz"
    source = venv / f"Python-{version}"
    build = venv / f"Python-{version}-build"
    download(url, archive, "Downloading Python")
    extract(archive, venv, " Extracting Python")
    archive.unlink()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            str(source / "configure"),
            f"--prefix={venv}",
            "--with-ensurepip=upgrade",
            "--enable-shared",
            "--enable-optimizations",
            "--with-lto",
            # f"CFLAGS=\"-DBERTRAND_TRACEBACK_EXCLUDE_PYTHON={source}:{build}:{install}\"",
        ],
        cwd=build
    )
    subprocess.check_call(["make", f"-j{workers}"], cwd=build)
    subprocess.check_call(["make", "install"], cwd=build)
    os.symlink(bin_dir / "python3", bin_dir / "python")
    os.symlink(bin_dir / "pip3", bin_dir / "pip")
    shutil.rmtree(source)
    shutil.rmtree(build)
    content["info"]["python_version"] = version


###########################
####    ENVIRONMENT    ####
###########################


def create_activation_script(venv: Path) -> Path:
    """Create a shell script that enters the virtual environment when sourced.

    Parameters
    ----------
    venv : Path
        The path to the virtual environment.

    Returns
    -------
    Path
        The path to the resulting activation script.
    """
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
    # reset old environment variables
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

    return file


def init(
    cwd: Path,
    name: str = "venv",
    *,
    compiler: str = "gcc",
    compiler_version: str = "latest",
    generator: str = "ninja",
    generator_version: str = "latest",
    build_tool: str = "cmake",
    build_tool_version: str = "latest",
    linker: str = "mold",
    linker_version: str = "latest",
    python_version: str = "latest",
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
    build_tool: str
        The build tool to use.
    build_tool_version : str
        The version of the build tool.
    linker : str
        The linker to use.
    linker_version : str
        The version of the linker.
    python_version : str
        The version of Python.
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

    Notes
    -----
    This is the target function for the `bertrand init` CLI command.  It is safe to
    call from Python, but can take a long time to run, and is mostly intended for
    command-line use.
    """
    start = time.time()

    venv = cwd / name
    flag = venv / "built"
    if not force and flag.exists():
        # TODO: load the env.toml file and check if everything matches
        raise NotImplementedError("TODO: Environment already exists.")

    bin_dir = venv / "bin"
    include_dir = venv / "include"
    lib_dir = venv / "lib"
    workers = workers or os.cpu_count() or 1
    if workers < 1:
        raise ValueError("workers must be a positive integer.")

    if compiler == "gcc":
        compiler_version = get_gcc_version(compiler_version)
        compiler_url = get_gcc_url(compiler_version)
    elif compiler == "clang":
        compiler_version = get_clang_version(compiler_version)
        compiler_url = get_clang_url(compiler_version)
    else:
        raise ValueError(
            "Compiler not recognized.  Run `$ bertrand init --help` for options."
        )

    if generator == "ninja":
        generator_version = get_ninja_version(generator_version)
        generator_url = get_ninja_url(generator_version)
    else:
        raise ValueError(
            "Generator not recognized.  Run `$ bertrand init --help` for options."
        )

    if build_tool == "cmake":
        build_tool_version = get_cmake_version(build_tool_version)
        build_tool_url = get_cmake_url(build_tool_version)
    else:
        raise ValueError(
            "Build tool not recognized.  Run `$ bertrand init --help` for options."
        )

    if linker == "mold":
        linker_version = get_mold_version(linker_version)
        linker_url = get_mold_url(linker_version)
    else:
        raise ValueError(
            "Linker not recognized.  Run `$ bertrand init --help` for options."
        )

    python_version = get_python_version(python_version)
    python_url = get_python_url(python_version)

    if venv.exists():
        shutil.rmtree(venv)
    venv.mkdir(parents=True)

    # content dictionary eventually gets written to the env.toml file.  Each tool can
    # modify it for its own purposes.  By the end of the function, it should contain
    # complete tables for [info], [vars], [paths], and [flags].
    content: dict[str, dict[str, Any]] = {
        "info": {},
        "vars": {},
        "paths": {
            "PATH": [str(bin_dir)],             # executables
            "CPATH": [str(include_dir)],        # headers
            "LIBRARY_PATH": [str(lib_dir)],     # libraries
            "LD_LIBRARY_PATH": [str(lib_dir)],  # runtime libraries
        },
        "flags": {
            "CFLAGS": [],
            "CXXFLAGS": [],
            "LDFLAGS": [],
        },
    }

    old_environ = os.environ.copy()

    try:
        if compiler == "gcc":
            if linker == "ld" or linker == "gold":
                install_gold(venv, workers)
            install_gcc(content, venv, compiler_version, compiler_url, bin_dir, workers)
        else:
            # NOTE: we have to install gold as a minimum requirement for LTO support
            # in Python.  This is true even if we use mold in the virtual environment,
            # since the Python build process can fail when using Clang without it.
            if linker == "ld" or linker == "gold" or linker == "mold":
                install_gold(venv, workers)
            install_clang(content, venv, compiler_version, compiler_url, bin_dir, workers)
        os.environ["CC"] = content["vars"]["CC"]
        os.environ["CXX"] = content["vars"]["CXX"]
        os.environ["AR"] = content["vars"]["AR"]
        os.environ["NM"] = content["vars"]["NM"]
        os.environ["RANLIB"] = content["vars"]["RANLIB"]
        os.environ["PATH"] = os.pathsep.join(
            [*content["paths"]["PATH"], os.environ["PATH"]]
            if os.environ.get("PATH", None) else
            content["paths"]["PATH"]
        )
        os.environ["CPATH"] = os.pathsep.join(
            [*content["paths"]["CPATH"], os.environ["CPATH"]]
            if os.environ.get("CPATH", None) else
            content["paths"]["CPATH"]
        )
        os.environ["LIBRARY_PATH"] = os.pathsep.join(
            [*content["paths"]["LIBRARY_PATH"], os.environ["LIBRARY_PATH"]]
            if os.environ.get("LIBRARY_PATH", None) else
            content["paths"]["LIBRARY_PATH"]
        )
        os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(
            [*content["paths"]["LD_LIBRARY_PATH"], os.environ["LD_LIBRARY_PATH"]]
            if os.environ.get("LD_LIBRARY_PATH", None) else
            content["paths"]["LD_LIBRARY_PATH"]
        )

        install_ninja(content, venv, generator_version, generator_url, bin_dir)
        install_cmake(content, venv, build_tool_version, build_tool_url, bin_dir, workers)

        # TODO: if lld or mold are set as linker, install them here
        if linker == "lld":
            # install_lld(content, venv, workers)
            raise NotImplementedError()
        else:
            install_mold(content, venv, linker_version, linker_url, bin_dir, workers)
        os.environ["LD"] = content["vars"]["LD"]
        os.environ["LDFLAGS"] = " ".join(
            [*content["flags"]["LDFLAGS"], os.environ["LDFLAGS"]]
            if os.environ.get("LDFLAGS", None) else
            content["flags"]["LDFLAGS"]
        )

        install_python(content, venv, python_version, python_url, bin_dir, workers)
        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(content, f)

        create_activation_script(venv)

    except:
        breakpoint()
        shutil.rmtree(venv)
        raise

    for k in os.environ:
        if k in old_environ:
            os.environ[k] = old_environ[k]
        else:
            del os.environ[k]

    flag.touch()
    print(f"Elapsed time: {time.time() - start:.2f} seconds")


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
