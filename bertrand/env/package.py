"""A set of wrappers around the conan package manager to simplify the process of
installing and configuring C++ dependencies within a virtual environment.
"""
from __future__ import annotations

import re
from typing import Any, TypeAlias

from packaging.version import Version


class Package:
    """A simple data struct representing a conan package installed or to be installed
    within the virtual environment.

    Attributes
    ----------
    name : str
        The name of the package.
    version : Version
        The package's version number, parsed using the `packaging` library.
    find : str | None
        The symbol to pass to CMake's `find_package` command or None if the package
        was specified in shorthand form.
    link : str | None
        The symbol to pass to CMake's `target_link_libraries` command or None if the
        package was specified in shorthand form.
    """

    # TODO: account for version ranges/any extra conan syntax

    VALID_KEYS = {"name", "version", "find", "link"}
    PATTERN = re.compile(
        r"^(?P<name>\w+)/(?P<version>[0-9.]+)(@(?P<find>\w+)/(?P<link>\w+::\w+))?$"
    )

    name: str
    version: Version
    find: str | None
    link: str | None

    def __new__(cls, specifier: PackageLike, allow_shorthand: bool = True) -> Package:
        if isinstance(specifier, Package):
            if not allow_shorthand and specifier.shorthand:
                raise ValueError(f"Specifier must not be shorthand: {repr(specifier)}")
            return specifier

        if isinstance(specifier, str):
            regex = cls.PATTERN.match(specifier)
            if not regex:
                raise ValueError(f"Invalid package specifier: {specifier}")

            self = super().__new__(cls)
            self.name = regex.group("name")
            self.version = Version(regex.group("version"))
            self.find = regex.group("find")
            self.link = regex.group("link")
            if not allow_shorthand and self.shorthand:
                raise ValueError(f"Package must not be shorthand: {specifier}")
            return self

        if isinstance(specifier, dict):
            for key in cls.VALID_KEYS:
                if key not in specifier:
                    raise ValueError(f"Package table is missing required key: {key}")
            for key in specifier:
                if key not in cls.VALID_KEYS:
                    raise ValueError(f"Package table has unexpected key: {key}")

            self = super().__new__(cls)
            self.name = specifier["name"]
            self.version = Version(specifier["version"])
            self.find = specifier.get("find", None)
            self.link = specifier.get("link", None)
            if not allow_shorthand and self.shorthand:
                raise ValueError(f"Package must not be shorthand: {specifier}")
            return self

        raise TypeError(f"Invalid package specifier: {specifier}")

    @property
    def shorthand(self) -> bool:
        """Indicates whether the package was specified in shorthand form, without
        explicit `find` or `link` symbols.

        Returns
        -------
        bool
            True if the package lacks `find` or `link` symbols.  False otherwise.
        """
        return not self.find or not self.link

    def to_dict(self) -> dict[str, str]:
        """Convert the package to a dictionary representation.

        Returns
        -------
        dict[str, str]
            A dictionary representation of the package.
        """
        result = {
            "name": self.name,
            "version": str(self.version)
        }
        if self.find:
            result["find"] = self.find
        if self.link:
            result["link"] = self.link
        return result

    def __hash__(self) -> int:
        return hash((self.name, self.version, self.find, self.link))

    def __lt__(self, other: Package) -> bool:
        return (self.name, self.version) < (other.name, other.version)

    def __le__(self, other: Package) -> bool:
        return (self.name, self.version) <= (other.name, other.version)

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, Package):
            return NotImplemented
        return (self.name, self.version) == (other.name, other.version)

    def __ne__(self, other: Any) -> bool:
        if not isinstance(other, Package):
            return NotImplemented
        return not self == other

    def __ge__(self, other: Package) -> bool:
        return (self.name, self.version) >= (other.name, other.version)

    def __gt__(self, other: Package) -> bool:
        return (self.name, self.version) > (other.name, other.version)

    def __str__(self) -> str:
        return repr(self)

    def __repr__(self) -> str:
        return f"{self.name}/{self.version}"


PackageLike: TypeAlias = Package | str | dict[str, str]
