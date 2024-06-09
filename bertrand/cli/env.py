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


# TODO: properly order pypi packages by version number

def pypi_versions(package_name: str, limit: int | None = None) -> list[str]:
    """Get the version numbers for a package on PyPI.

    Parameters
    ----------
    package_name : str
        The name of the package to get the version numbers for.
    limit : int, optional
        The maximum number of version numbers to return.  Supports negative indexing.
    """
    response = requests.get(f"https://pypi.org/pypi/{package_name}/json", timeout=TIMEOUT)
    versions = list(response["releases"].keys())
    versions.sort(reverse=True)
    if limit is None:
        return versions
    return versions[:limit]


class Target:
    """Abstract base class for build targets when installing a virtual environment."""

    def __init__(self, name: str, version: str, url: str) -> None:
        self.name = name
        self.version = version
        self.url = url
        self.build = True

    def __bool__(self) -> bool:
        return self.build

    def __call__(self, venv: Path, workers: int) -> None:
        raise NotImplementedError()

    def update_toml(self, venv: Path) -> None:
        pass

    def update_environment(self, venv: Path) -> None:
        pass


class Gold(Target):
    """Strategy for installing the LD and gold linkers into a virtual environment."""

    gmp_releases_url = "https://ftp.gnu.org/gnu/gmp/"
    gmp_version_pattern = re.compile(
        r"^gmp-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz$"
    )
    mpfr_releases_url = "https://ftp.gnu.org/gnu/mpfr/"
    mpfr_version_pattern = re.compile(
        r"^mpfr-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz$"
    )
    binutils_releases_url = "https://ftp.gnu.org/gnu/binutils/"
    binutils_version_pattern = re.compile(
        r"^binutils-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.xz$"
    )

    def install_gmp(self, venv: Path, workers: int) -> None:
        """Install GNU MP (a.k.a. GMP) into a virtual environment.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment to install GMP into.
        workers : int
            The number of workers to use when building GMP.
        """
        response = requests.get(self.gmp_releases_url, timeout=TIMEOUT)
        soup = BeautifulSoup(response.text, "html.parser")
        versions: list[tuple[int, int, int]] = []
        for link in soup.find_all("a"):
            regex = self.gmp_version_pattern.match(link.text)
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

    def install_mpfr(self, venv: Path, workers: int) -> None:
        """Install the MPFR library into a virtual environment.

        This depends on GMP.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment to install MPFR into.
        workers : int
            The number of workers to use when building MPFR.
        """
        self.install_gmp(venv, workers)
        response = requests.get(self.mpfr_releases_url, timeout=TIMEOUT)
        soup = BeautifulSoup(response.text, "html.parser")
        versions: list[tuple[int, int, int]] = []
        for link in soup.find_all("a"):
            regex = self.mpfr_version_pattern.match(link.text)
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

    def __init__(self, name: str, version: str):
        if version == "latest":
            response = requests.get(self.binutils_releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.binutils_version_pattern.match(link.text)
                if regex:
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch"))
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest ld/gold version.  Please specify a "
                    "version number manually."
                )

            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid ld/gold version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        super().__init__(
            name,
            version,
            f"https://ftp.gnu.org/gnu/binutils/binutils-{version}.tar.xz"
        )
        if not ping(self.url):
            raise ValueError(f"ld/gold version {self.version} not found.")

        print(f"ld/gold URL: {self.url}")  # TODO: move these somewhere else

    def __call__(self, venv: Path, workers: int) -> None:
        self.install_mpfr(venv, workers)
        response = requests.get(self.binutils_releases_url, timeout=TIMEOUT)
        soup = BeautifulSoup(response.text, "html.parser")
        versions: list[tuple[int, int, int]] = []
        for link in soup.find_all("a"):
            regex = self.binutils_version_pattern.match(link.text)
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

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the ld/gold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            # TODO: may need to symlink ld.gold to gold
            env = tomlkit.load(f)
            env["info"]["linker"] = self.name  # type: ignore
            env["info"]["linker_version"] = self.version  # type: ignore
            env["vars"]["LD"] = str(venv / "bin" / self.name)  # type: ignore
            env["flags"]["LDFLAGS"].extend([f"-fuse-ld={self.name}"])  # type: ignore
            tomlkit.dump(env, f)

    def update_environment(self, venv: Path) -> None:
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

    releases_url = "https://gcc.gnu.org/releases.html"
    version_pattern = re.compile(
        r"^GCC (?P<major>\d+)\.(?P<minor>\d+)(\.(?P<patch>\d+))?$"
    )

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://ftpmirror.gnu.org/gnu/gcc/gcc-{version}/gcc-{version}.tar.xz"
        )
        if not ping(self.url):
            raise ValueError(f"GCC version {self.version} not found.")

        print(f"GCC URL: {self.url}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"gcc-{self.version}.tar.xz"
        source = venv / f"gcc-{self.version}"
        build = venv / f"gcc-{self.version}-build"
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
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the GCC compiler.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["c_compiler"] = "gcc"  # type: ignore
            env["info"]["cxx_compiler"] = "g++"  # type: ignore
            env["info"]["compiler_version"] = self.version  # type: ignore
            env["vars"]["CC"] = str(venv / "bin" / "gcc")  # type: ignore
            env["vars"]["CXX"] = str(venv / "bin" / "g++")  # type: ignore
            env["vars"]["AR"] = str(venv / "bin" / "gcc-ar")  # type: ignore
            env["vars"]["NM"] = str(venv / "bin" / "gcc-nm")  # type: ignore
            env["vars"]["RANLIB"] = str(venv / "bin" / "gcc-ranlib")  # type: ignore
            env["paths"]["LIBRARY_PATH"].append(str(venv / "lib64"))  # type: ignore
            env["paths"]["LD_LIBRARY_PATH"].append(str(venv / "lib64"))  # type: ignore
            tomlkit.dump(env, f)

    def update_environment(self, venv: Path) -> None:
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

    releases_url = "https://github.com/llvm/llvm-project/releases"
    version_pattern = re.compile(
        r"^LLVM (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)$"
    )
    valid_targets = {
        "clang",
        "clang-tools-extra",
        "compiler-rt",
        "lld",
        "cross-project-tests",
        "libc",
        "libclc",
        "lldb",
        "openmp",
        "polly",
        "pstl"
    }

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-{version}.tar.gz"
        )
        self.targets: list[str] = []
        if not ping(self.url):
            raise ValueError(f"Clang version {version} not found.")

        print(f"Clang URL: {self.url}")

    def __call__(self, venv: Path, workers: int, **kwargs: Any) -> None:
        if not self.targets:
            raise RuntimeError("Clang targets must be set before building.")

        archive = venv / f"clang-{self.version}.tar.gz"
        source = venv / f"llvm-project-llvmorg-{self.version}"
        build = venv / f"clang-{self.version}-build"
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
                f"{source}/llvm",
            ],
            cwd=build
        )
        subprocess.check_call(["make", f"-j{workers}"], cwd=build)
        subprocess.check_call(["make", "install"], cwd=build)
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the Clang compiler.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        # TODO: add different config for lld/clangtools
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["c_compiler"] = "clang"  # type: ignore
            env["info"]["cxx_compiler"] = "clang++"  # type: ignore
            env["info"]["compiler_version"] = self.version  # type: ignore
            env["vars"]["CC"] = str(venv / "bin" / "clang")  # type: ignore
            env["vars"]["CXX"] = str(venv / "bin" / "clang++")  # type: ignore
            env["vars"]["AR"] = str(venv / "bin" / "llvm-ar")  # type: ignore
            env["vars"]["NM"] = str(venv / "bin" / "llvm-nm")  # type: ignore
            env["vars"]["RANLIB"] = str(venv / "bin" / "llvm-ranlib")  # type: ignore
            tomlkit.dump(env, f)

    def update_environment(self, venv: Path) -> None:
        """Update the environment with paths to the Clang targets that were built.

        Parameters
        ----------
        venv : Path
            The path to the virtual environment containing the Clang targets.
        """
        os.environ["CC"] = str(venv / "bin" / "clang")
        os.environ["CXX"] = str(venv / "bin" / "clang++")
        os.environ["AR"] = str(venv / "bin" / "llvm-ar")
        os.environ["NM"] = str(venv / "bin" / "llvm-nm")
        os.environ["RANLIB"] = str(venv / "bin" / "llvm-ranlib")


class Ninja(Target):
    """Strategy for installing the Ninja build system into a virtual environment."""

    releases_url = "https://github.com/ninja-build/ninja/releases"
    version_pattern = re.compile(r"^v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)$")

    def __init__(self, name: str, version: str):
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://github.com/ninja-build/ninja/archive/refs/tags/v{self.version}.tar.gz"
        )
        if not ping(self.url):
            raise ValueError(f"Ninja version {self.version} not found.")

        print(f"Ninja URL: {self.version}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"ninja-{self.version}.tar.gz"
        install = venv / f"ninja-{self.version}"
        download(self.url, archive, "Downloading Ninja")
        extract(archive, venv, " Extracting Ninja")
        archive.unlink()
        subprocess.check_call([str(install / "configure.py"), "--bootstrap"], cwd=install)
        shutil.move(install / "ninja", venv / "bin" / "ninja")
        shutil.rmtree(install)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the Ninja build system.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["generator"] = self.name  # type: ignore
            env["info"]["generator_version"] = self.version  # type: ignore
            tomlkit.dump(env, f)


class CMake(Target):
    """Strategy for installing the CMake build system into a virtual environment."""

    releases_url = "https://github.com/Kitware/CMake/releases"
    version_pattern = re.compile(r"^v(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)$")

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}.tar.gz"
        )
        if not ping(self.url):
            raise ValueError(f"CMake version {version} not found.")

        print(f"CMake URL: {self.url}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"cmake-{self.version}.tar.gz"
        source = venv / f"cmake-{self.version}"
        build = venv / f"cmake-{self.version}-build"
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
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the CMake build system.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["build_tool"] = self.name  # type: ignore
            env["info"]["build_tool_version"] = self.version  # type: ignore
            tomlkit.dump(env, f)


class Mold(Target):
    """Strategy for installing the mold linker into a virtual environment."""

    releases_url = "https://github.com/rui314/mold/releases"
    version_pattern = re.compile(r"^mold (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)$")

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://github.com/rui314/mold/archive/refs/tags/v{version}.tar.gz"
        )
        if not ping(self.url):
            raise ValueError(f"mold version {version} not found.")

        print(f"mold URL: {self.url}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"mold-{self.version}.tar.gz"
        source = venv / f"mold-{self.version}"
        build = venv / f"mold-{self.version}-build"
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
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for the mold linker.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["linker"] = self.name  # type: ignore
            env["info"]["linker_version"] = self.version  # type: ignore
            env["vars"]["LD"] = str(venv / "bin" / self.name)  # type: ignore
            env["flags"]["LDFLAGS"].extend([f"-fuse-ld={self.name}"])  # type: ignore
            tomlkit.dump(env, f)

    def update_environment(self, venv: Path) -> None:
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

    releases_url = "https://sourceware.org/pub/valgrind/"
    version_pattern = re.compile(
        r"^valgrind-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)\.tar\.bz2$"
    )

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
                if regex:
                    versions.append((
                        int(regex.group("major")),
                        int(regex.group("minor")),
                        int(regex.group("patch"))
                    ))
            if not versions:
                raise ValueError(
                    "Could not detect latest Valgrind version.  Please specify a version "
                    "number manually."
                )
            versions.sort(reverse=True)
            version = ".".join(map(str, versions[0]))

        regex = SEMVER_REGEX.match(version)
        if not regex:
            raise ValueError(
                "Invalid Valgrind version.  Must be set to 'latest' or a version "
                "specifier of the form 'X.Y.Z'."
            )

        super().__init__(
            name,
            version,
            f"https://sourceware.org/pub/valgrind/valgrind-{version}.tar.bz2"
        )
        if not ping(self.url):
            raise ValueError(f"Valgrind version {self.version} not found.")

        print(f"Valgrind URL: {self.url}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"valgrind-{self.version}.tar.bz2"
        source = venv / f"valgrind-{self.version}"
        build = venv / f"valgrind-{self.version}-build"
        download(self.url, archive, "Downloading Valgrind")
        extract(archive, venv, " Extracting Valgrind")
        archive.unlink()
        subprocess.check_call(["./autogen.sh"], cwd=source)
        build.mkdir(parents=True, exist_ok=True)
        subprocess.check_call([str(source / "configure"), f"--prefix={venv}"], cwd=build)
        subprocess.check_call(["make", f"-j{workers}"], cwd=build)
        subprocess.check_call(["make", "install"], cwd=build)
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for Valgrind.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["valgrind"] = self.version  # type: ignore
            tomlkit.dump(env, f)


class Python(Target):
    """Strategy for installing Python into a virtual environment."""

    releases_url = "https://www.python.org/downloads/"
    version_pattern = re.compile(
        r"^Python (?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)$"
    )

    def __init__(self, name: str, version: str) -> None:
        if version == "latest":
            response = requests.get(self.releases_url, timeout=TIMEOUT)
            soup = BeautifulSoup(response.text, "html.parser")
            versions: list[tuple[int, int, int]] = []
            for link in soup.find_all("a"):
                regex = self.version_pattern.match(link.text)
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

        super().__init__(
            name,
            version,
            f"https://www.python.org/ftp/python/{version}/Python-{version}.tar.xz"
        )
        if not ping(self.url):
            raise ValueError(f"Python version {self.version} not found.")

        print(f"Python URL: {self.url}")

    def __call__(self, venv: Path, workers: int) -> None:
        archive = venv / f"Python-{self.version}.tar.xz"
        source = venv / f"Python-{self.version}"
        build = venv / f"Python-{self.version}-build"
        download(self.url, archive, "Downloading Python")
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
        os.symlink(venv / "bin" / "python3", venv / "bin" / "python")
        os.symlink(venv / "bin" / "pip3", venv / "bin" / "pip")
        shutil.rmtree(source)
        shutil.rmtree(build)

    def update_toml(self, venv: Path) -> None:
        """Update the toml file with the config for Python.

        Parameters
        ----------
        venv : Path
            A path to the virtual environment containing the env.toml file.
        """
        with (venv / "env.toml").open("w") as f:
            env = tomlkit.load(f)
            env["info"]["python"] = self.version  # type: ignore
            tomlkit.dump(env, f)


class Recipe:
    """Determines the correct order in which to install each target in order to
    bootstrap the virtual environment.
    """

    def __init__(
        self,
        compiler: Target,
        generator: Target,
        build_system: Target,
        linker: Target,
        python: Target,
        conan: Target,
        clangtools: Target | None,
        valgrind: Target | None
    ) -> None:
        self.compiler = compiler
        self.generator = generator
        self.build_system = build_system
        self.linker = linker
        self.python = python
        self.conan = conan
        self.clangtools = clangtools
        self.valgrind = valgrind

    def gcc_order(self) -> list[Target]:
        """Get the install order for a GCC-based virtual environment.

        Returns
        -------
        list[Target]
            The sequence of targets to install.
        """
        order = []
        if self.linker.name in ("ld", "gold"):
            order.append(self.linker)
        order.append(self.compiler)
        order.append(self.generator)
        order.append(self.build_system)
        if self.linker.name == "lld":
            order.append(self.linker)
            if self.clangtools and self.linker.version == self.clangtools.version:
                self.linker.targets.extend(self.clangtools.targets)
                self.clangtools.build = False
        elif self.linker.name == "mold":
            order.append(self.linker)
        if self.valgrind:
            order.append(self.valgrind)
            self.python.with_valgrind = True
        order.append(self.python)
        # order.append(self.conan)
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
        order = []
        if self.linker.name in ("ld", "gold"):
            order.append(self.linker)
        elif self.linker.name == "mold":
            order.append(Gold("gold", "latest"))
        order.append(self.compiler)
        if self.linker.name == "lld" and self.linker.version == self.compiler.version:
            self.compiler.targets.extend(self.linker.targets)
            self.linker.build = False
        if self.clangtools and self.clangtools.version == self.compiler.version:
            self.compiler.targets.extend(self.clangtools.targets)
            self.clangtools.build = False
        order.append(self.generator)
        order.append(self.build_system)
        if self.linker.name == "lld" and self.linker:
            order.append(self.linker)
            if self.clangtools and self.clangtools.version == self.linker.version:
                self.linker.targets.extend(self.clangtools.targets)
                self.clangtools.build = False    
        elif self.linker.name == "mold":
            order.append(self.linker)
        if self.valgrind:
            order.append(self.valgrind)
            self.python.with_valgrind = True
        order.append(self.python)
        # order.append(self.conan)
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

        Raises
        ------
        RuntimeError
            If the compiler is not recognized.  This should never occur.
        """
        # get build order
        if self.compiler.name == "gcc":
            order = self.gcc_order()
        elif self.compiler.name == "clang":
            order = self.clang_order()
        else:
            raise RuntimeError(
                f"bad compiler choice (should never occur): '{self.compiler.name}'"
            )

        # install recipe
        for target in order:
            if target:
                target(venv, workers)
                target.update_environment(venv)
                target.update_toml(venv)
            else:
                target.update_environment(venv)

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



###########################
####    ENVIRONMENT    ####
###########################



def init(
    cwd: Path,
    name: str = "venv",
    *,
    compiler_name: str = "gcc",
    compiler_version: str = "latest",
    generator_name: str = "ninja",
    generator_version: str = "latest",
    build_tool_name: str = "cmake",
    build_tool_version: str = "latest",
    linker_name: str = "mold",
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
    compiler_name : str
        The C/C++ compiler to use.
    compiler_version : str
        The version of the C/C++ compiler.
    generator_name : str
        The build system generator to use.
    generator_version : str
        The version of the build system generator.
    build_tool_name: str
        The build tool to use.
    build_tool_version : str
        The version of the build tool.
    linker_name : str
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

    Notes
    -----
    This is the target function for the `bertrand init` CLI command.  It is safe to
    call from Python, but can take a long time to run, and is mostly intended for
    command-line use.
    """
    # start = time.time()
    # if os.environ.get("BERTRAND_HOME", None):
    #     raise RuntimeError(
    #         "`$ bertrand init` should not be run from inside a virtual environment. "
    #         "Exit the virtual environment before running this command."
    #     )

    # # validate and normalize inputs
    # recipe = ToolChain(...)
    # old_environment = os.environ.copy()
    # venv = cwd / name
    # if force and venv.exists():
    #     shutil.rmtree(venv)

    # # check if an environment already exists
    # toml_path = venv / "env.toml"
    # if toml_path.exists():
    #     with toml_path.open("r") as f:
    #         env = tomlkit.load(f)["info"]
    #         if "compiler" in env and "compiler_version" in env:
    #             if (
    #                 recipe.compiler.name == env["compiler"] and
    #                 recipe.compiler.version == env["compiler_version"]
    #             ):
    #                 recipe.compiler.build = False
    #             else:
    #                 raise RuntimeError(
    #                     "compiler version does not match existing environment.  "
    #                     "Apply `--force` if you want to rebuild the environment."
    #                 )
    #         if "generator" in env and "generator_version" in env:
    #             if (
    #                 recipe.generator.name == env["generator"] and
    #                 recipe.generator.version == env["generator_version"]
    #             ):
    #                 recipe.generator.build = False
    #             else:
    #                 raise RuntimeError()
    #         if "build_system" in env and "build_system_version" in env:
    #             if (
    #                 build_system == env["build_system"] and
    #                 build_system_version == env["build_system_version"]
    #             ):
    #                 recipe.build_system.build = False
    #             else:
    #                 raise RuntimeError()
    #         if "linker" in env and "linker_version" in env:
    #             if (
    #                 recipe.linker.name == env["linker"] and
    #                 recipe.linker.version == env["linker_version"]
    #             ):
    #                 recipe.linker.build = False
    #             else:
    #                 raise RuntimeError()
    #         if "python" in env:
    #             if recipe.python.version == env["python"]:
    #                 recipe.python.build = False
    #             else:
    #                 raise RuntimeError()
    #         if "conan" in env:
    #             if recipe.conan.version == env["conan"]:
    #                 recipe.conan.build = False
    #             else:
    #                 raise RuntimeError()
    #         if recipe.clangtools and "clangtools" in env:
    #             if recipe.clangtools.version == env["clangtools"]:
    #                 recipe.clangtools.build = False
    #             else:
    #                 raise RuntimeError()
    #         if recipe.valgrind and "valgrind" in env:
    #             if recipe.valgrind.version == env["valgrind"]:
    #                 recipe.valgrind.build = False
    #             else:
    #                 raise RuntimeError()

    # recipe.install(venv)
    # (venv / "built").touch(exist_ok=True)

    # # restore old environment

    # # -> print using strptime or whatever it's called.
    # print(f"Elapsed time: {time.time() - start:.2f}")


    start = time.time()

    venv = cwd / name
    flag = venv / "built"
    if not force and flag.exists():
        # TODO: load the env.toml file and check if everything matches
        raise NotImplementedError("TODO: Environment already exists.")

    if compiler_name == "gcc":
        compiler = GCC(compiler_version)
    elif compiler_name == "clang":
        compiler = Clang(compiler_version)
    else:
        raise ValueError(
            f"Compiler '{compiler_name}' not recognized.  Run `$ bertrand init --help` "
            f"for options."
        )

    if generator_name == "ninja":
        generator = Ninja(generator_version)
    else:
        raise ValueError(
            f"Generator '{generator_name}' not recognized.  Run `$ bertrand init --help` "
            f"for options."
        )

    if build_tool_name == "cmake":
        build_tool = CMake(build_tool_version)
    else:
        raise ValueError(
            f"Build tool '{build_tool_name}' not recognized.  Run `$ bertrand init --help` "
            f"for options."
        )

    if linker_name in ("ld", "gold"):
        linker = Gold(linker_version)
    elif linker_name == "lld":
        linker = Clang(linker_version)
    elif linker_name == "mold":
        linker = Mold(linker_version)
    else:
        raise ValueError(
            f"Linker '{linker_name}' not recognized.  Run `$ bertrand init --help` "
            f"for options."
        )

    python = Python(python_version)
    # conan = Conan(conan_version)

    if clangtools_version:
        clangtools = Clang(clangtools_version)
    else:
        clangtools = None

    if valgrind_version:
        valgrind = Valgrind(valgrind_version)
    else:
        valgrind = None

    workers = workers or os.cpu_count() or 1
    if workers < 1:
        raise ValueError("workers must be a positive integer.")

    bin_dir = venv / "bin"
    include_dir = venv / "include"
    lib_dir = venv / "lib"

    # TODO: allow version of Gold to be correctly specified
    # TODO: configure Python with valgrind support if enabled

    # GCC recipe
    if compiler_name == "gcc":
        if linker in ("ld", "gold"):
            linker()

        recipe.append(self)
        recipe.append(Ninja(generator_version))
        recipe.append(CMake(build_tool_version))
        if linker == "mold":
            recipe.append(Mold(linker_version))

        # bundle lld together with clang-tools-extra if possible
        if linker == "lld":
            if clangtools:
                if clangtools == linker_version:
                    recipe.append(Clang(linker_version, targets=["lld", "clang-tools-extra"]))
                else:
                    recipe.append(Clang(linker_version, targets=["lld"]))
                    recipe.append(Clang(clangtools, targets=["clang-tools-extra"]))
            else:
                recipe.append(Clang(linker_version, targets=["lld"]))
        elif clangtools:
            recipe.append(Clang(clangtools, targets=["clang-tools-extra"]))

        if valgrind:
            recipe.append(Valgrind(valgrind))
        recipe.append(Python(python_version))
        return recipe

    # Clang recipe
    elif compiler == "clang":
        if linker in ("ld", "gold", "mold"):
            # NOTE: gold must be preinstalled for Python to build with LTO using Clang,
            # even if we're using mold as the final linker.  This appears to be a bug
            # in the Python build system.  It doesn't occur with lld.
            recipe.append(Gold(linker_version))

        # bundle clang together with lld, clangtools if possible
        clang_targets = ["clang", "compiler-rt"]
        if linker == "lld" and linker_version == compiler_version:
            clang_targets.append("lld")
        if clangtools and clangtools == compiler_version:
            clang_targets.append("clang-tools-extra")
        recipe.append(Clang(compiler_version, targets=clang_targets))

        recipe.append(Ninja(generator_version))
        recipe.append(CMake(build_tool_version))
        if linker == "mold":
            recipe.append(Mold(linker_version))

        # if lld/clangtools are not the same version as clang, install them separately
        if linker == "lld":
            if clangtools:
                if clangtools == linker_version:
                    recipe.append(Clang(linker_version, targets=["lld", "clang-tools-extra"]))
                else:
                    recipe.append(Clang(linker_version, targets=["lld"]))
                    recipe.append(Clang(clangtools, targets=["clang-tools-extra"]))
            else:
                recipe.append(Clang(linker_version, targets=["lld"]))
        elif clangtools:
            recipe.append(Clang(clangtools, targets=["clang-tools-extra"]))

        if valgrind:
            recipe.append(Valgrind(valgrind))
        recipe.append(Python(python_version))

    # TODO: ensure Conan version is >=2.0.0
    # TODO: install bertrand into the virtual environment

    if venv.exists():
        shutil.rmtree(venv)
    venv.mkdir(parents=True)

    # content dictionary eventually gets written to the env.toml file.  Each tool can
    # modify it for its own purposes.  By the end of the script, it should contain
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

    old_environ = os.environ.copy()  # restore after installation

    try:
        for target in recipe:
            target(
                content=content,
                venv=venv,
                bin_dir=bin_dir,
                workers=workers
            )

        with (venv / "env.toml").open("w") as f:
            tomlkit.dump(content, f)

        create_activation_script(venv)

        # os.environ["CC"] = content["vars"]["CC"]
        # os.environ["CXX"] = content["vars"]["CXX"]
        # os.environ["AR"] = content["vars"]["AR"]
        # os.environ["NM"] = content["vars"]["NM"]
        # os.environ["RANLIB"] = content["vars"]["RANLIB"]
        # os.environ["PATH"] = os.pathsep.join(
        #     [*content["paths"]["PATH"], os.environ["PATH"]]
        #     if os.environ.get("PATH", None) else
        #     content["paths"]["PATH"]
        # )
        # os.environ["CPATH"] = os.pathsep.join(
        #     [*content["paths"]["CPATH"], os.environ["CPATH"]]
        #     if os.environ.get("CPATH", None) else
        #     content["paths"]["CPATH"]
        # )
        # os.environ["LIBRARY_PATH"] = os.pathsep.join(
        #     [*content["paths"]["LIBRARY_PATH"], os.environ["LIBRARY_PATH"]]
        #     if os.environ.get("LIBRARY_PATH", None) else
        #     content["paths"]["LIBRARY_PATH"]
        # )
        # os.environ["LD_LIBRARY_PATH"] = os.pathsep.join(
        #     [*content["paths"]["LD_LIBRARY_PATH"], os.environ["LD_LIBRARY_PATH"]]
        #     if os.environ.get("LD_LIBRARY_PATH", None) else
        #     content["paths"]["LD_LIBRARY_PATH"]
        # )

        # os.environ["LD"] = content["vars"]["LD"]
        # os.environ["LDFLAGS"] = " ".join(
        #     [*content["flags"]["LDFLAGS"], os.environ["LDFLAGS"]]
        #     if os.environ.get("LDFLAGS", None) else
        #     content["flags"]["LDFLAGS"]
        # )

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
