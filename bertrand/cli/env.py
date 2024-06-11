"""Activate/deactivate the current environment and query its state."""
from __future__ import annotations

import os
import sys
import sysconfig
from collections import deque
from collections.abc import KeysView, ValuesView, ItemsView
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator, SupportsIndex

import numpy
import pybind11
import tomlkit


# TODO: environment should actually be a global object, so there's no need for the
# current() or exists() methods.  Just check the truthiness of the object itself.

# TODO: this class could be imported into init.py and used to standardize everything
# and incrementally build the environment.


class Vars:
    """A wrapper around a TOML table representing the [vars] section of an env.toml
    file.  Synchronizes any changes with the current environment.
    """

    def __init__(self, env: Environment, table: dict[str, Any]) -> None:
        self.env = env
        self.table = table

    def keys(self) -> KeysView[str]:
        """Return a view of the keys in the table.

        Returns
        -------
        KeysView[str]
            A view of the keys in the table.
        """
        return self.table.keys()

    def values(self) -> ValuesView[Any]:
        """Return a view of the values in the table.

        Returns
        -------
        ValuesView[Any]
            A view of the values in the table.
        """
        return self.table.values()

    def items(self) -> ItemsView[str, Any]:
        """Return a view of the items in the table.

        Returns
        -------
        ItemsView[str, Any]
            A view of the items in the table.
        """
        return self.table.items()

    def copy(self) -> dict[str, Any]:
        """Return a shallow copy of the table.

        Returns
        -------
        dict[str, Any]
            A shallow copy of the table.
        """
        return self.table.copy()

    def clear(self) -> None:
        """Remove all items from the table."""
        for key in self.table:
            os.environ.pop(key, None)
        self.table.clear()
        self.env.save()

    def get(self, key: str, default: Any = None) -> Any:
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : Any, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Any
            The value associated with the key, or the default value if the key is not
            found.
        """
        return self.table.get(key, default)

    def pop(self, key: str, default: Any = None) -> Any:
        """Remove an item from the table and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table.
        default : Any, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Any
            The value associated with the key, or the default value if the key is not
            found.
        """
        os.environ.pop(key, None)
        result = self.table.pop(key, default)
        self.env.save()
        return result

    def setdefault(self, key: str, default: Any = None) -> Any:
        """Set the value of a key if it is not already present, and then return the
        value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : Any, optional
            The value to set if the key is not already present, by default None.

        Returns
        -------
        Any
            The value associated with the key.
        """
        if key not in self.table:
            os.environ[key] = default
        result = self.table.setdefault(key, default)
        self.env.save()
        return result

    def update(self, other: dict[str, Any]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the table with.
        """
        for key, value in other.items():
            os.environ[key] = value
        self.table.update(other)
        self.env.save()

    def __bool__(self) -> bool:
        return bool(self.table)

    def __len__(self) -> int:
        return len(self.table)

    def __iter__(self) -> Iterator[str]:
        return iter(self.table)

    def __reversed__(self) -> Iterator[str]:
        return reversed(self.table)

    def __getitem__(self, key: str) -> Any:
        return self.table[key]

    def __setitem__(self, key: str, value: Any) -> None:
        os.environ[key] = value
        self.table[key] = value
        self.env.save()

    def __delitem__(self, key: str) -> None:
        os.environ.pop(key, None)
        del self.table[key]
        self.env.save()

    def __getattr__(self, key: str) -> Any:
        return self.table[key]

    def __setattr__(self, key: str, value: Any) -> None:
        os.environ[key] = value
        self.table[key] = value
        self.env.save()

    def __delattr__(self, key: str) -> None:
        os.environ.pop(key, None)
        del self.table[key]
        self.env.save()

    def __contains__(self, key: str) -> bool:
        return key in self.table

    def __eq__(self, other: Any) -> bool:
        return self.table == other

    def __ne__(self, other: Any) -> bool:
        return self.table != other

    def __or__(self, other: dict[str, Any]) -> dict[str, Any]:
        return self.table | other

    def __ior__(self, other: dict[str, Any]) -> Vars:
        for key, value in other.items():
            os.environ[key] = value
        self.table |= other
        self.env.save()
        return self


class Paths:
    """A wrapper around a TOML table of arrays representing the [paths] section of an
    env.toml file.  Entries in the table are joined with the system's path separator
    and prepended to the corresponding environment variable when modified.
    """

    class Entry:
        """A wrapper around a TOML array representing a single entry in the [paths]
        table.  Changes are automatically propagated to both the environment and the
        env.toml file.
        """

        def _normalize_index(self, index: int, truncate: bool) -> int:
            if index < 0:
                index += len(self.array)

            if truncate:
                if index < 0:
                    index = 0
                elif index > len(self.array):
                    index = len(self.array)
            elif index < 0 or index > len(self.array):
                raise IndexError("Index out of range.")

            return index

        def __init__(self, env: Environment, key: str, array: deque[Path]) -> None:
            self.env = env
            self.key = key
            self.array = array

        def append(self, path: Path) -> None:
            """Append a path to the array.

            Parameters
            ----------
            path : Path
                The path to append to the array.
            """
            n = len(self.array)
            self.array.append(path)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:n], str(path), *components[n:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def appendleft(self, path: Path) -> None:
            """Prepend a path to the array.

            Parameters
            ----------
            path : Path
                The path to prepend to the array.
            """
            self.array.appendleft(path)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join([str(path), *components])
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def extend(self, other: Iterable[Path]) -> None:
            """Extend the path with the contents of another sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the array with.
            """
            n = len(self.array)
            self.array.extend(other)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:n], *[str(p) for p in other], *components[n:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def extendleft(self, other: Iterable[Path]) -> None:
            """Extend the path with the contents of another sequence, prepending each
            item.

            Note that this implicitly reverses the order of the items in the sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the array with.
            """
            self.array.extendleft(other)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*[str(p) for p in other], *components]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def insert(self, index: int, path: Path) -> None:
            """Insert a path into the array at a specific index.

            Parameters
            ----------
            index : int
                The index to insert the path at.
            path : Path
                The path to insert into the array.
            """
            index = self._normalize_index(index, truncate=True)
            self.array.insert(index, path)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:index], str(path), *components[index:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def remove(self, path: Path) -> None:
            """Remove the first occurrence of a path from the array.

            Parameters
            ----------
            path : Path
                The path to remove from the array.

            Raises
            ------
            ValueError
                If the path is not found in the array.
            """
            index = self.array.index(path)
            self.array.remove(path)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:index], *components[index + 1:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def pop(self) -> Path:
            """Remove and return the last path in the array.

            Returns
            -------
            Path
                The value of the path that was removed.
            """
            n = len(self.array)
            result = self.array.pop()
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:n - 1], *components[n:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()
            return result

        def popleft(self) -> Path:
            """Remove and return the first item in the array.

            Returns
            -------
            Path
                The value of the path that was removed.
            """
            result = self.array.popleft()
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(components[1:])
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()
            return result

        def copy(self) -> deque[Path]:
            """Return a shallow copy of the array.

            Returns
            -------
            deque[Path]
                A shallow copy of the array.
            """
            return self.array.copy()

        def clear(self) -> None:
            """Remove all items from the array."""
            n = len(self.array)
            self.array.clear()
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(components[n:])
            self.env.save()

        def index(self, path: Path) -> int:
            """Return the index of the first occurrence of a path in the array.

            Parameters
            ----------
            path : Path
                The path to search for in the array.

            Returns
            -------
            int
                The index of the first occurrence of the path in the array.

            Raises
            ------
            ValueError
                If the path is not found in the array.
            """
            return self.array.index(path)

        def count(self, path: Path) -> int:
            """Return the number of occurrences of a path in the array.

            Parameters
            ----------
            path : Path
                The path to count in the array.

            Returns
            -------
            int
                The number of occurrences of the path in the array.
            """
            return self.array.count(path)

        def sort(
            self,
            *,
            key: Callable[[Path], bool] | None = None,
            reverse: bool = False
        ) -> None:
            """Sort the array in place.

            Parameters
            ----------
            key : Callable, optional
                A function to use as the key for sorting, by default None.
            reverse : bool, optional
                Whether to sort the array in descending order, by default False.
            """
            temp = list(self.array)
            temp.sort(key=key, reverse=reverse)
            self.array = deque(temp)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*[str(p) for p in self.array], *components[len(self.array):]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def reverse(self) -> None:
            """Reverse the order of the items in the array."""
            self.array.reverse()
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*[str(p) for p in self.array], *components[len(self.array):]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def rotate(self, n: int) -> None:
            """Rotate the array n steps to the right.

            Parameters
            ----------
            n : int
                The number of steps to rotate the array to the right.
            """
            self.array.rotate(n)
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*[str(p) for p in self.array], *components[len(self.array):]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def __len__(self) -> int:
            return len(self.array)

        def __bool__(self) -> bool:
            return bool(self.array)

        def __iter__(self) -> Iterator[Path]:
            return iter(self.array)

        def __reversed__(self) -> Iterator[Path]:
            return reversed(self.array)

        # TODO: account for slicing

        def __getitem__(self, index: SupportsIndex) -> Path:
            return self.array[index]

        def __setitem__(self, index: SupportsIndex, path: Path) -> None:
            index = self._normalize_index(index, truncate=False)
            self.array[index] = path
            if self.key in os.environ:
                components = os.pathsep.split(os.environ[self.key])
                os.environ[self.key] = os.pathsep.join(
                    [*components[:index], str(path), *components[index + 1:]]
                )
            else:
                os.environ[self.key] = os.pathsep.join(str(p) for p in self.array)
            self.env.save()

        def __delitem__(self, index: SupportsIndex) -> None:
            # TODO
            pass

        def __eq__(self, other: Any) -> bool:
            return self.array == other

        def __ne__(self, other: Any) -> bool:
            return self.array != other

        def __add__(self, other: deque[Path]) -> deque[Path]:
            return self.array + other

        def __iadd__(self, other: deque[Path]) -> Paths.Entry:
            self.extend(other)
            return self

    def _reset(self, key: str) -> None:
        """Reset an environment variable to its original value.

        Parameters
        ----------
        key : str
            The key to reset.
        """
        existing = os.environ.get(key, None)
        if existing:
            value = self.table[key]
            components = os.pathsep.split(existing)
            new = [c for c in components if c not in value]
            os.environ[key] = os.pathsep.join(new)
        else:
            os.environ.pop(key, None)

    def clear(self) -> None:
        """Remove all items from the table and the environment."""
        for key in self.table:
            self._reset(key)
        self.table.clear()
        self.env.save()

    def pop(self, key: str, default: Any = None) -> deque[str]:
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : Any, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        deque[str]
            The value associated with the key, or the default value if the key is not
            found.
        """
        self._reset(key)
        return super().pop(key, default)

    def setdefault(self, key: str, default: deque[str]) -> deque[str]:
        """Set the value of a key if it is not already present in the table or the
        environment, and then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : deque[str]
            The value to set if the key is not already present.

        Returns
        -------
        deque[str]
            The value associated with the key.
        """
        if key not in self.table:
            os.environ[key] = os.pathsep.join(default)
        return super().setdefault(key, default)




class Packages:
    """A wrapper around a TOML array of structured tables representing the [[packages]]
    section of an env.toml file.  Appending to the Packages list will automatically
    update the env.toml file.
    """

    class Entry:
        """Represents a single entry in the packages array."""

        VALID_KEYS = {"name", "version", "find", "link"}

        def __init__(self, env: Environment, table: dict[str, Any]) -> None:
            if "name" not in table:
                raise ValueError("Package entry must have a 'name' field.")
            if "version" not in table:
                raise ValueError("Package entry must have a 'version' field.")
            if "find" not in table:
                raise ValueError("Package entry must have a 'find' field.")
            if "link" not in table:
                raise ValueError("Package entry must have a 'link' field.")
            for key in table:
                if key not in self.VALID_KEYS:
                    raise ValueError(f"Unexpected key '{key}' in package entry.")

            self.env = env
            self.name = table["name"]
            self.name = table["version"]
            self.find = table["find"]
            self.link = table["link"]
            self.table = table

        @property
        def name(self) -> str:
            """The name of the package.

            Returns
            -------
            str
                The name of the package.
            """
            return self.table["name"]

        @name.setter
        def name(self, value: str) -> None:
            self.table["name"] = value
            self.env.save()

        @property
        def version(self) -> str:
            """The installed version of the package.

            Returns
            -------
            str
                The version of the package.
            """
            return self.table["version"]

        @version.setter
        def version(self, value: str) -> None:
            self.table["version"] = value
            self.env.save()

        @property
        def find(self) -> str:
            """The symbol to pass to CMake's `find_package()` function in order to
            locate the package's headers and libraries.

            Returns
            -------
            str
                The find symbol for the package.
            """
            return self.table["find"]

        @find.setter
        def find(self, value: str) -> None:
            self.table["find"] = value
            self.env.save()

        @property
        def link(self) -> str:
            """The symbol to pass to CMake's `target_link_libraries()` function in
            order to link the package's libraries.

            Returns
            -------
            str
                The link symbol for the package.
            """
            return self.table["link"]

        @link.setter
        def link(self, value: str) -> None:
            self.table["link"] = value
            self.env.save()

    def __init__(self, env: Environment, array: deque[Entry]) -> None:
        self.env = env
        self.array = deque(array)

    def append(self, entry: Entry) -> None:
        """Append an entry to the array.

        Parameters
        ----------
        entry : Entry
            The entry to append to the array.
        """
        self.array.append(entry)
        self.env.save()

    def appendleft(self, entry: Entry) -> None:
        """Prepend an entry to the array.

        Parameters
        ----------
        entry : Entry
            The entry to prepend to the array.
        """
        self.array.appendleft(entry)
        self.env.save()

    def extend(self, other: Iterable[Entry]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[Entry]
            The sequence to extend the array with.
        """
        self.array.extend(other)
        self.env.save()

    def extendleft(self, other: Iterable[Entry]) -> None:
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[Entry]
            The sequence to extend the array with.
        """
        self.array.extendleft(other)
        self.env.save()

    def insert(self, index: int, entry: Entry) -> None:
        """Insert an entry into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the entry at.
        entry : Entry
            The entry to insert into the array.
        """
        self.array.insert(index, entry)
        self.env.save()

    def remove(self, entry: Entry) -> None:
        """Remove the first occurrence of an entry from the array.

        Parameters
        ----------
        entry : Entry
            The entry to remove from the array.

        Raises
        ------
        ValueError
            If the entry is not found in the array.
        """
        self.array.remove(entry)
        self.env.save()

    def pop(self) -> Entry:
        """Remove and return the last entry in the array.

        Returns
        -------
        Entry
            The value of the entry that was removed.
        """
        result = self.array.pop()
        self.env.save()
        return result

    def popleft(self) -> Entry:
        """Remove and return the first item in the array.

        Returns
        -------
        Any
            The value of the entry that was removed.
        """
        result = self.array.popleft()
        self.env.save()
        return result

    def copy(self) -> deque[Entry]:
        """Return a shallow copy of the array.

        Returns
        -------
        deque[Any]
            A shallow copy of the array.
        """
        return self.array.copy()

    def clear(self) -> None:
        """Remove all items from the array."""
        self.array.clear()
        self.env.save()

    def index(self, entry: Entry) -> int:
        """Return the index of the first occurrence of an entry in the array.

        Parameters
        ----------
        entry : Any
            The entry to search for in the array.

        Returns
        -------
        int
            The index of the first occurrence of the entry in the array.

        Raises
        ------
        ValueError
            If the entry is not found in the array.
        """
        return self.array.index(entry)

    def count(self, entry: Entry) -> int:
        """Return the number of occurrences of a entry in the array.

        Parameters
        ----------
        entry : Any
            The entry to count in the array.

        Returns
        -------
        int
            The number of occurrences of the entry in the array.
        """
        return self.array.count(entry)

    def sort(
        self,
        *,
        key: Callable[[Entry], bool] | None = None,
        reverse: bool = False
    ) -> None:
        """Sort the array in place.

        Parameters
        ----------
        key : Callable, optional
            A function to use as the key for sorting, by default None.
        reverse : bool, optional
            Whether to sort the array in descending order, by default False.
        """
        temp = list(self.array)
        temp.sort(key=key, reverse=reverse)
        self.array = deque(temp)
        self.env.save()

    def reverse(self) -> None:
        """Reverse the order of the items in the array."""
        self.array.reverse()
        self.env.save()

    def rotate(self, n: int) -> None:
        """Rotate the array n steps to the right.

        Parameters
        ----------
        n : int
            The number of steps to rotate the array to the right.
        """
        self.array.rotate(n)
        self.env.save()

    def __len__(self) -> int:
        return len(self.array)

    def __bool__(self) -> bool:
        return bool(self.array)

    def __iter__(self) -> Iterator[Entry]:
        return iter(self.array)

    def __reversed__(self) -> Iterator[Entry]:
        return reversed(self.array)

    def __getitem__(self, index: SupportsIndex) -> Entry:
        return self.array[index]

    def __setitem__(self, index: SupportsIndex, entry: Entry) -> None:
        self.array[index] = entry
        self.env.save()

    def __delitem__(self, index: SupportsIndex) -> None:
        del self.array[index]
        self.env.save()

    def __eq__(self, other: Any) -> bool:
        return self.array == other

    def __ne__(self, other: Any) -> bool:
        return self.array != other

    def __add__(self, other: deque[Entry]) -> deque[Entry]:
        return self.array + other

    def __iadd__(self, other: deque[Entry]) -> Packages:
        self.array += other
        self.env.save()
        return self






# TODO: indexing into an environment should check os.environ.
# -> the / operator will compute from the root of the virtual environment.


# TODO: Environment should be a global object


class Environment:
    """A wrapper around an env.toml file that can be used to activate and deactivate
    the environment, as well as check or modify its state.
    """

    OLD_PREFIX = "_OLD_VIRTUAL_"
    venv: Path | None
    toml: Path | None

    def __init__(self) -> None:
        raise NotImplementedError(
            "Environment should be used as a global object, not instantiated directly."
        )

    def __new__(cls) -> Environment:
        self = super().__new__(cls)
        if "BERTRAND_HOME" in os.environ:
            self.venv = Path(os.environ["BERTRAND_HOME"])
            self.toml = self.venv / "env.toml"
            if self.toml.exists():
                with self.toml.open("r") as f:
                    content = tomlkit.load(f)

                # if "info" in content:
                #     info = content["info"]
                #     if isinstance(info, tomlkit.items.AbstractTable):
                #         self.info = Info(self, dict(info))
                #     else:
                #         raise ValueError(f"[info] must be a table, not {type(info)}")
                # else:
                #     raise ValueError(f"No [info] table in file: {self.toml}")

                # if "vars" in content:
                #     _vars = content["vars"]
                #     if isinstance(_vars, tomlkit.items.AbstractTable):
                #         self.vars = Vars(self, dict(_vars))
                #     else:
                #         raise ValueError(f"[vars] must be a table, not {type(vars)}")
                # else:
                #     raise ValueError(f"No [vars] table in file: {self.toml}")

                # if "paths" in content:
                #     paths = content["paths"]
                #     if isinstance(paths, tomlkit.items.AbstractTable):
                #         self.paths = Table(self, dict(paths))
                #     else:
                #         raise ValueError(f"[paths] must be a table, not {type(paths)}")
                # else:
                #     raise ValueError(f"No [paths] table in file: {self.toml}")

                # if "flags" in content:
                #     flags = content["flags"]
                #     if isinstance(flags, tomlkit.items.AbstractTable):
                #         self.flags = Table(self, dict(flags))
                #     else:
                #         raise ValueError(f"[flags] must be a table, not {type(flags)}")
                # else:
                #     raise ValueError(f"No [flags] table in file: {self.toml}")
        else:
            self.venv = None
            self.toml = None
        return self

    def __bool__(self) -> bool:
        """Check if there is an active virtual environment."""
        return "BERTRAND_HOME" in os.environ

    def save(self) -> None:
        """Write the current environment to the TOML file.

        Raises
        ------
        ValueError
            If no environment file was found.
        """
        # TODO: this should be atomic

        with self.file.open("w") as f:
            tomlkit.dump(self.content, f)








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



