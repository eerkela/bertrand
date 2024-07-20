"""A set of wrappers around the conan package manager to simplify the process of
installing and configuring C++ dependencies within a virtual environment.
"""
from __future__ import annotations

from typing import Any

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

    def __init__(
        self,
        name: str,
        version: str | Version,
        find: str | None = None,
        link: str | None = None,
        allow_shorthand: bool = True,
    ) -> None:
        self.name = name
        self.version = Version(version) if isinstance(version, str) else version
        self.find = find
        self.link = link
        if not allow_shorthand and self.shorthand:
            raise ValueError(f"Package must have find and link symbols: {self}")

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

    @classmethod
    def from_dict(cls, table: dict[str, str], allow_shorthand: bool = True) -> Package:
        """Convert a dictionary representation of a package into a `Package` object.

        Parameters
        ----------
        table : dict[str, str]
            A dictionary representation of a package, with keys "name" and "version",
            and optionally "find" and "link".
        allow_shorthand : bool, optional
            Whether to allow shorthand package specifiers, by default True.

        Returns
        -------
        Package
            The package object.

        Raises
        ------
        ValueError
            If the package table is missing required keys or contains unexpected keys.
        """
        for key in cls.VALID_KEYS:
            if key not in table:
                raise ValueError(f"Package table is missing required key: {key}")
        for key in table:
            if key not in cls.VALID_KEYS:
                raise ValueError(f"Package table has unexpected key: {key}")

        self = super().__new__(cls)
        self.name = table["name"]
        self.version = Version(table["version"])
        self.find = table.get("find", None)
        self.link = table.get("link", None)
        if not allow_shorthand and self.shorthand:
            raise ValueError(f"Package must not be shorthand: {table}")
        return self

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
