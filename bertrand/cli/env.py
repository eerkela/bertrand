"""Activate/deactivate the current environment and query its state."""
from __future__ import annotations

import os
import sys
import sysconfig
from collections import deque
from collections.abc import KeysView, ValuesView, ItemsView
from pathlib import Path
from typing import (
    Any, Callable, Generic, Iterable, Iterator, SupportsIndex, TypeVar, overload
)

import numpy
import pybind11
import tomlkit

from bertrand import __file__ as bertrand_root


T = TypeVar("T")


class Table(Generic[T]):
    """A wrapper around a TOML table that is lazily loaded and written back to the
    env.toml file when modified.  Subclasses can add behavior for modifying the
    environment as needed.
    """

    def __init__(
        self,
        environment: Environment,
        name: str,
        table: dict[str, T] | None = None
    ) -> None:
        self.environment = environment
        self.name = name
        self._table = table

    @property
    def table(self) -> dict[str, T]:
        """Read the table from the env.toml file.

        Returns
        -------
        dict[str, T]
            An up-to-date view of the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        if self._table is not None:
            return self._table

        toml = self.environment.toml
        with toml.open("r") as f:
            content = tomlkit.load(f)

        if self.name not in content:
            raise KeyError(f"no [{self.name}] table in file: {toml}")
        table = content[self.name]
        if not isinstance(table, tomlkit.items.AbstractTable):
            raise TypeError(f"[{self.name}] must be a table, not {type(table)}")

        return dict(table)

    @table.setter
    def table(self, value: dict[str, T]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            content[self.name] = value
            f.seek(0)
            tomlkit.dump(content, f)
            f.truncate()

    def keys(self) -> KeysView[str]:
        """
        Returns
        -------
        KeysView[str]
            A view on the keys stored in the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return self.table.keys()

    def values(self) -> ValuesView[T]:
        """
        Returns
        -------
        ValuesView[T]
            A view on the values stored in the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return self.table.values()

    def items(self) -> ItemsView[str, T]:
        """
        Returns
        -------
        ItemsView[str, T]
            A view on the key-value pairs stored in the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return self.table.items()

    def copy(self) -> dict[str, T]:
        """
        Returns
        -------
        dict[str, T]
            A shallow copy of the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return self.table.copy()

    def clear(self) -> None:
        """Remove all items from the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        table.clear()
        self.table = table

    def get(self, key: str, default: T | None = None) -> T | None:
        """Get an item from the table or a default value if the key is not found.

        Parameters
        ----------
        key : str
            The key to look up.
        default : T | None, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        T | None
            The value associated with the key, or the default value if the key is not
            found.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return self.table.get(key, default)

    def pop(self, key: str, default: T | None = None) -> T | None:
        """Remove an item from the table and return its value or a default value if the
        key is not found.

        Parameters
        ----------
        key : str
            The key to remove from the table.
        default : T, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        T
            The value associated with the key, or the default value if the key is not
            found.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        result = table.pop(key, default)
        self.table = table
        return result

    def update(self, other: dict[str, T]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, T]
            The dictionary to update the table with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        table.update(other)
        self.table = table

    def __bool__(self) -> bool:
        return bool(self.table)

    def __len__(self) -> int:
        return len(self.table)

    def __iter__(self) -> Iterator[str]:
        return iter(self.table)

    def __reversed__(self) -> Iterator[str]:
        return reversed(self.table)

    def __getitem__(self, key: str) -> T:
        return self.table[key]

    def __setitem__(self, key: str, value: T) -> None:
        table = self.table
        table[key] = value
        self.table = table

    def __delitem__(self, key: str) -> None:
        table = self.table
        del table[key]
        self.table = table

    def __contains__(self, key: str) -> bool:
        return key in self.table

    def __eq__(self, other: Any) -> bool:
        return self.table == other

    def __ne__(self, other: Any) -> bool:
        return self.table != other

    def __or__(self, other: dict[str, T]) -> dict[str, T]:
        return self.table | other

    def __ior__(self, other: dict[str, T]) -> Table[T]:
        self.update(other)
        return self

    def __str__(self) -> str:
        return str(self.table)

    def __repr__(self) -> str:
        return repr(self.table)


class Array(Generic[T]):
    """A wrapper around a TOML array that is lazily loaded and written back to the
    env.toml file when modified.  Subclasses can add behavior for modifying the
    environment as needed.
    """

    def __init__(
        self,
        environment: Environment,
        name: str,
        array: list[T] | None = None
    ) -> None:
        self.environment = environment
        self.name = name
        self._array = array

    @property
    def array(self) -> list[T]:
        """Read the array from the env.toml file.

        Returns
        -------
        list[T]
            An up-to-date view of the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        if self._array is not None:
            return self._array

        toml = self.environment.toml
        with toml.open("r") as f:
            content = tomlkit.load(f)

        if self.name not in content:
            raise KeyError(f"no [{self.name}] array in file: {toml}")
        array = content[self.name]
        if not isinstance(array, tomlkit.items.AbstractTable):
            raise TypeError(f"[{self.name}] must be an array, not {type(array)}")

        return list(array)

    @array.setter
    def array(self, value: list[T]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            content[self.name] = value
            f.seek(0)
            tomlkit.dump(content, f)
            f.truncate()

    def append(self, value: T) -> None:
        """Append a value to the array.

        Parameters
        ----------
        value : T
            The value to append to the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = self.array
        array.append(value)
        self.array = array

    def appendleft(self, value: T) -> None:
        """Prepend a value to the array.

        Parameters
        ----------
        value : T
            The value to prepend to the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = [value]
        array.extend(self.array)
        self.array = array

    def extend(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array, or the sequence is not iterable.
        """
        array = self.array
        array.extend(other)
        self.array = array

    def extendleft(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array, or the sequence is not iterable.
        """
        array = deque(self.array)
        array.extendleft(other)
        self.array = list(array)

    def insert(self, index: int, value: T) -> None:
        """Insert a value into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the value at.
        value : T
            The value to insert into the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = self.array
        array.insert(index, value)
        self.array = array

    def remove(self, value: T) -> None:
        """Remove the first occurrence of a value from the array.

        Parameters
        ----------
        value : Path
            The value to remove from the array.

        Raises
        ------
        ValueError
            If the value is not found in the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        ValueError
            If the value is not found in the array.
        """
        array = self.array
        array.remove(value)
        self.array = array

    def pop(self) -> T:
        """Remove and return the last value in the array.

        Returns
        -------
        T
            The value of the value that was removed.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        IndexError
            If the array is empty.
        """
        array = self.array
        result = array.pop()
        self.array = array
        return result

    def popleft(self) -> T:
        """Remove and return the first item in the array.

        Returns
        -------
        T
            The value of the value that was removed.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        IndexError
            If the array is empty.
        """
        array = self.array
        if not array:
            raise IndexError("pop from empty list")
        result = array[0]
        self.array = array[1:]
        return result

    def copy(self) -> list[T]:
        """Return a shallow copy of the array.

        Returns
        -------
        list[T]
            A shallow copy of the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        return self.array.copy()

    def clear(self) -> None:
        """Remove all items from the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = self.array
        array.clear()
        self.array = array

    def index(self, value: T) -> int:
        """Return the index of the first occurrence of a value in the array.

        Parameters
        ----------
        value : T
            The value to search for in the array.

        Returns
        -------
        int
            The index of the first occurrence of the value in the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        ValueError
            If the value is not found in the array.
        """
        return self.array.index(value)

    def count(self, value: T) -> int:
        """Return the number of occurrences of a value in the array.

        Parameters
        ----------
        value : T
            The value to count in the array.

        Returns
        -------
        int
            The number of occurrences of the value in the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        ValueError
            If the value is not found in the array.
        """
        return self.array.count(value)

    def sort(
        self,
        *,
        key: Callable[[T], bool] | None = None,
        reverse: bool = False
    ) -> None:
        """Sort the array in place.

        Parameters
        ----------
        key : Callable, optional
            A function to use as the key for sorting.  Defaults to None.
        reverse : bool, optional
            Whether to sort the array in descending order, by default False.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array, or the keys are not comparable.
        """
        array = self.array
        array.sort(key=key, reverse=reverse)
        self.array = array

    def reverse(self) -> None:
        """Reverse the order of the items in the array.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = self.array
        array.reverse()
        self.array = array

    def rotate(self, n: int) -> None:
        """Rotate the array n steps to the right.

        Parameters
        ----------
        n : int
            The number of steps to rotate the array to the right.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        array = deque(self.array)
        array.rotate(n)
        self.array = list(array)

    def __len__(self) -> int:
        return len(self.array)

    def __bool__(self) -> bool:
        return bool(self.array)

    def __iter__(self) -> Iterator[T]:
        return iter(self.array)

    def __reversed__(self) -> Iterator[T]:
        return reversed(self.array)

    @overload
    def __getitem__(self, index: SupportsIndex) -> T: ...
    @overload
    def __getitem__(self, index: slice) -> list[T]: ...
    def __getitem__(self, index: SupportsIndex | slice) -> T | list[T]:
        return self.array[index]

    @overload
    def __setitem__(self, index: SupportsIndex, value: T) -> None: ...
    @overload
    def __setitem__(self, index: slice, value: Iterable[T]) -> None: ...
    def __setitem__(self, index: SupportsIndex | slice, value: T | Iterable[T]) -> None:
        array = self.array
        array[index] = value  # type: ignore
        self.array = array

    def __delitem__(self, index: SupportsIndex | slice) -> None:
        array = self.array
        del array[index]
        self.array = array

    def __eq__(self, other: Any) -> bool:
        return self.array == other

    def __ne__(self, other: Any) -> bool:
        return self.array != other

    def __add__(self, other: Iterable[T]) -> list[T]:
        return self.array + list(other)

    def __iadd__(self, other: Iterable[T]) -> Array[T]:
        self.extend(other)
        return self

    def __str__(self) -> str:
        return str(self.array)

    def __repr__(self) -> str:
        return repr(self.array)


class Vars(Table[Any]):
    """A table that represents the [vars] section of an env.toml file.  Pushes any
    changes to both the toml file and the current environment.
    """

    def clear(self) -> None:
        """Remove all items from the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        keys = list(table)
        table.clear()
        for key in keys:
            os.environ.pop(key, None)
        self.table = table

    def pop(self, key: str, default: Any = None) -> Any:
        """Remove an item from the table and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table.
        default : Any, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        Any
            The value associated with the key, or the default value if the key is not
            found.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        result = table.pop(key, default)
        os.environ.pop(key, None)
        self.table = table
        return result

    def update(self, other: dict[str, Any]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the table with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        table.update(other)
        for key, value in other.items():
            os.environ[key] = value
        self.table = table

    def __setitem__(self, key: str, value: Any) -> None:
        table = self.table
        table[key] = value
        os.environ[key] = value
        self.table = table

    def __delitem__(self, key: str) -> None:
        table = self.table
        del table[key]
        os.environ.pop(key, None)
        self.table = table


class Paths(Table["Paths.Entry"]):
    """A table that represents the [paths] section of an env.toml file.  Entries in the
    table are joined with the system's path separator and prepended to the corresponding
    environment variable when modified.
    """

    @staticmethod
    def _reset_path(key: str, old: list[Path]) -> None:
        if key in os.environ:
            os.environ[key] = os.pathsep.join(
                p for p in os.environ[key].split(os.pathsep) if Path(p) not in old
            )

    @staticmethod
    def _extend_path(key: str, value: list[Path]) -> None:
        if key in os.environ:
            os.environ[key] = os.pathsep.join([*[str(p) for p in value], os.environ[key]])
        else:
            os.environ[key] = os.pathsep.join(str(p) for p in value)

    class Entry(Array[Path]):
        """Represents a single value in the [paths] table."""

        # pylint: disable=protected-access

        def __init__(
            self,
            paths: Paths,
            key: str,
            array: list[Path] | None = None
        ) -> None:
            super().__init__(paths.environment, key)
            self.paths = paths
            self._array = array

        @property
        def array(self) -> list[Path]:
            """Read the array from the env.toml file.

            Returns
            -------
            list[Path]
                An up-to-date view of the array.

            Raises
            ------
            RuntimeError
                If no environment is currently active.
            FileNotFoundError
                If the env.toml file is not found.
            KeyError
                If the [paths] table is not found in the file, or if it does not
                contain the key.
            TypeError
                If the [paths] table is not a table, or the array is not an array of
                pathlike strings.
            """
            if self._array is not None:
                return self._array

            toml = self.paths.environment.toml
            with toml.open("r") as f:
                content = tomlkit.load(f)

            if "paths" not in content:
                raise KeyError(f"no [paths] table in file: {toml}")
            table = content["paths"]
            if not isinstance(table, tomlkit.items.AbstractTable):
                raise TypeError(f"[paths] must be a table, not {type(table)}")

            if self.name not in table:
                raise KeyError(f"no '{self.name}' entry in [paths] table")
            array = table[self.name]
            if not isinstance(array, list):
                raise TypeError(
                    f"[paths.{self.name}] must be an array, not {type(array)}"
                )
            elif not all(isinstance(p, str) for p in array):
                raise TypeError(
                    f"[paths.{self.name}] must be an array of pathlike strings"
                )

            return [Path(p) for p in array]

        @array.setter
        def array(self, value: list[Path]) -> None:
            toml = self.paths.environment.toml
            with toml.open("r+") as f:
                content = tomlkit.load(f)
                content.setdefault("paths", {})[self.name] = [str(p) for p in value]
                f.seek(0)
                tomlkit.dump(content, f)
                f.truncate()

        def append(self, value: Path) -> None:
            """Append a path to the entry.

            Parameters
            ----------
            value : Path
                The path to append.
            """
            array = self.array
            old = array.copy()
            array.append(value)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, self.array)
            self.array = array

        def appendleft(self, value: Path) -> None:
            """Prepend a path to the entry.

            Parameters
            ----------
            value : Path
                The path to prepend.
            """
            array = self.array
            old = array.copy()
            array = [value]
            array.extend(old)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def extend(self, other: Iterable[Path]) -> None:
            """Extend the entry with the contents of another sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the entry with.
            """
            array = self.array
            old = array.copy()
            array.extend(other)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def extendleft(self, other: Iterable[Path]) -> None:
            """Extend the entry with the contents of another sequence, prepending each
            item.

            Note that this implicitly reverses the order of the items in the sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the entry with.
            """
            array = self.array
            old = array.copy()
            temp = deque(array)
            temp.extendleft(other)
            array = list(temp)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def insert(self, index: int, value: Path) -> None:
            """Insert a path into the entry at a specific index.

            Parameters
            ----------
            index : int
                The index to insert the path at.
            value : Path
                The path to insert into the entry.
            """
            array = self.array
            old = array.copy()
            array.insert(index, value)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def remove(self, value: Path) -> None:
            """Remove the first occurrence of a path from the entry.

            Parameters
            ----------
            value : Path
                The path to remove from the entry.
                
            Raises
            ------
            ValueError
                If the path is not found in the entry.
            """
            array = self.array
            old = array.copy()
            array.remove(value)
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def pop(self) -> Path:
            """Remove and return the last path in the entry.

            Returns
            -------
            Path
                The path that was removed.
            """
            array = self.array
            old = array.copy()
            result = array.pop()
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array
            return result

        def popleft(self) -> Path:
            """Remove and return the first path in the entry.

            Returns
            -------
            Path
                The path that was removed.
            """
            array = self.array
            if not array:
                raise IndexError("pop from empty list")
            old = array.copy()
            result = array[0]
            array = array[1:]
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array
            return result

        def clear(self) -> None:
            """Remove all paths from the entry."""
            array = self.array
            old = array.copy()
            array.clear()
            Paths._reset_path(self.name, old)
            self.array = array

        def sort(
            self,
            *,
            key: Callable[[Path], bool] | None = None,
            reverse: bool = False
        ) -> None:
            """Sort the entry in place.

            Parameters
            ----------
            key : Callable, optional
                A function to use as the key for sorting.  Defaults to None.
            reverse : bool, optional
                Whether to sort the entry in descending order, by default False.
            """
            array = self.array
            array.sort(key=key, reverse=reverse)
            Paths._reset_path(self.name, array)
            Paths._extend_path(self.name, array)
            self.array = array

        def reverse(self) -> None:
            """Reverse the order of the paths in the entry."""
            array = self.array
            array.reverse()
            Paths._reset_path(self.name, array)
            Paths._extend_path(self.name, array)
            self.array = array

        def rotate(self, n: int = 1) -> None:
            """Rotate the entry n steps to the right.

            Parameters
            ----------
            n : int
                The number of steps to rotate the entry to the right.
            """
            array = self.array
            temp = deque(array)
            temp.rotate(n)
            array = list(temp)
            Paths._reset_path(self.name, array)
            Paths._extend_path(self.name, array)
            self.array = array

        @overload
        def __setitem__(self, index: SupportsIndex, value: Path) -> None: ...
        @overload
        def __setitem__(self, index: slice, value: Iterable[Path]) -> None: ...
        def __setitem__(
            self,
            index: SupportsIndex | slice,
            value: Path | Iterable[Path]
        ) -> None:
            array = self.array
            old = array.copy()
            array[index] = value  # type: ignore
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

        def __delitem__(self, index: SupportsIndex | slice) -> None:
            array = self.array
            old = array.copy()
            del array[index]
            Paths._reset_path(self.name, old)
            Paths._extend_path(self.name, array)
            self.array = array

    @property
    def table(self) -> dict[str, Entry]:
        """Read the table from the env.toml file.

        Returns
        -------
        dict[str, Entry]
            An up-to-date view of the table.  The values are lazily-evaluated so that
            they always reflect the current state of the environment.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return {k: Paths.Entry(self, k) for k in super().table}

    @table.setter
    def table(self, value: dict[str, Entry]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            content["paths"] = {k: [str(p) for p in v] for k, v in value.items()}
            f.seek(0)
            tomlkit.dump(content, f)
            f.truncate()

    def copy(self) -> dict[str, list[Path]]:  # type: ignore
        """Return a shallow copy of the table.

        Returns
        -------
        dict[str, list[Path]]
            A shallow copy of the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return {k: list(v) for k, v in self.table.items()}

    def clear(self) -> None:
        """Remove all items from the table and the environment.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        old = {k: list(v) for k, v in table.items()}
        table.clear()
        for key in old:
            Paths._reset_path(key, old[key])
        self.table = table

    def pop(self, key: str, default: list[Path] | None = None) -> list[Path] | None:  # type: ignore
        """Remove an item from the table and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : list[Path] | None, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        list[Path] | None
            The value associated with the key, or the default value if the key is not
            present.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        if key in table:
            result = list(table.pop(key))
            Paths._reset_path(key, result)
            self.table = table
            return result
        return default

    def get(self, key: str, default: list[Path] | None = None) -> list[Path] | None:  # type: ignore
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[Path] | None, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        list[Path] | None
            The value associated with the key, or the default value if the key is not
            present.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        result = self.table.get(key, default)
        if isinstance(result, Paths.Entry):
            return list(result)
        return result

    def setdefault(self, key: str, default: list[Path]) -> Entry:
        """Get an item from the table or set it to a default value and return the
        default value.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[Path]
            The value to set if the key is not found.

        Returns
        -------
        Entry
            A lazily-evaluated view of the value associated with the key.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        if key in table:
            return table[key]
        result = Paths.Entry(self, key, default)
        table[key] = result
        Paths._extend_path(key, default)
        self.table = table
        return result

    def update(self, other: dict[str, list[Path]]) -> None:  # type: ignore
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, list[Path]]
            The dictionary to update the table with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        old = {k: list(v) for k, v in table.items()}
        table.update({k: Paths.Entry(self, k, v) for k, v in other.items()})
        for key, value in other.items():
            if key in old:
                Paths._reset_path(key, old[key])
            Paths._extend_path(key, value)
        self.table = table

    def __getitem__(self, key: str) -> Entry:
        return Paths.Entry(self, key)

    def __setitem__(self, key: str, value: list[Path]) -> None:  # type: ignore
        table = self.table
        entry = Paths.Entry(self, key, value)
        if key in table:
            old = list(table[key])
            table[key] = entry
            Paths._reset_path(key, old)
        else:
            table[key] = entry
        Paths._extend_path(key, value)
        self.table = table

    def __delitem__(self, key: str) -> None:
        table = self.table
        if key not in table:
            raise KeyError(key)
        old = list(table[key])
        del table[key]
        Paths._reset_path(key, old)
        self.table = table

    def __or__(self, other: dict[str, list[Path]]) -> dict[str, list[Path]]:  # type: ignore
        return {k: list(v) for k, v in self.table.items()} | other

    def __ior__(self, other: dict[str, list[Path]]) -> Paths:  # type: ignore
        self.update(other)
        return self


class Flags(Table["Flags.Entry"]):
    """A table that represents the [flags] section of an env.toml file.  Entries in the
    table are joined with spaces and prepended to the corresponding environment variable
    when modified.
    """

    @staticmethod
    def _reset_flags(key: str, old: list[str]) -> None:
        if key in os.environ:
            os.environ[key] = " ".join(
                f for f in os.environ[key].split(" ") if f not in old
            )

    @staticmethod
    def _extend_flags(key: str, value: list[str]) -> None:
        if key in os.environ:
            os.environ[key] = " ".join([os.environ[key], *value])
        else:
            os.environ[key] = " ".join(value)

    class Entry(Array[str]):
        """Represents a single entry in the [flags] table."""

        # pylint: disable=protected-access

        def __init__(
            self,
            flags: Flags,
            key: str,
            array: list[str] | None = None
        ) -> None:
            super().__init__(flags.environment, key)
            self.flags = flags
            self._array = array

        @property
        def array(self) -> list[str]:
            """Read the array from the env.toml file.

            Returns
            -------
            list[str]

            Raises
            ------
            RuntimeError
                If no environment is currently active.
            FileNotFoundError
                If the env.toml file is not found.
            KeyError
                If the [flags] table is not found in the file, or if it does not contain
                the key.
            TypeError
                If the [flags] table is not a table, or the array is not an array of
                strings.
            """
            if self._array is not None:
                return self._array

            toml = self.flags.environment.toml
            with toml.open("r") as f:
                content = tomlkit.load(f)

            if "flags" not in content:
                raise KeyError(f"no [flags] table in file: {toml}")
            table = content["flags"]
            if not isinstance(table, tomlkit.items.AbstractTable):
                raise TypeError(f"[flags] must be a table, not {type(table)}")

            if self.name not in table:
                raise KeyError(f"no '{self.name}' entry in [flags] table")
            array = table[self.name]
            if not isinstance(array, list):
                raise TypeError(
                    f"[flags.{self.name}] must be an array, not {type(array)}"
                )
            elif not all(isinstance(f, str) for f in array):
                raise TypeError(
                    f"[flags.{self.name}] must be an array of strings"
                )

            return array

        @array.setter
        def array(self, value: list[str]) -> None:
            toml = self.flags.environment.toml
            with toml.open("r+") as f:
                content = tomlkit.load(f)
                content.setdefault("flags", {})[self.name] = value
                f.seek(0)
                tomlkit.dump(content, f)
                f.truncate()

        def append(self, value: str) -> None:
            """Append a flag to the entry.

            Parameters
            ----------
            value : str
                The flag to append.
            """
            array = self.array
            old = array.copy()
            array.append(value)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def appendleft(self, value: str) -> None:
            """Prepend a flag to the entry.

            Parameters
            ----------
            value : str
                The flag to prepend.
            """
            array = self.array
            old = array.copy()
            array = [value]
            array.extend(old)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def extend(self, other: Iterable[str]) -> None:
            """Extend the entry with the contents of another sequence.

            Parameters
            ----------
            other : Iterable[str]
                The sequence to extend the entry with.
            """
            array = self.array
            old = array.copy()
            array.extend(other)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def extendleft(self, other: Iterable[str]) -> None:
            """Extend the entry with the contents of another sequence, prepending each
            item.

            Note that this implicitly reverses the order of the items in the sequence.

            Parameters
            ----------
            other : Iterable[str]
                The sequence to extend the entry with.
            """
            array = self.array
            old = array.copy()
            temp = deque(array)
            temp.extendleft(other)
            array = list(temp)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def insert(self, index: int, value: str) -> None:
            """Insert a flag into the entry at a specific index.

            Parameters
            ----------
            index : int
                The index to insert the flag at.
            value : str
                The flag to insert into the entry.
            """
            array = self.array
            old = array.copy()
            array.insert(index, value)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def remove(self, value: str) -> None:
            """Remove the first occurrence of a flag from the entry.

            Parameters
            ----------
            value : str
                The flag to remove from the entry.
                
            Raises
            ------
            ValueError
                If the flag is not found in the entry.
            """
            array = self.array
            old = array.copy()
            array.remove(value)
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def pop(self) -> str:
            """Remove and return the last flag in the entry.

            Returns
            -------
            str
                The flag that was removed.
            """
            array = self.array
            old = array.copy()
            result = array.pop()
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array
            return result

        def popleft(self) -> str:
            """Remove and return the first flag in the entry.

            Returns
            -------
            str
                The flag that was removed.
            """
            array = self.array
            if not array:
                raise IndexError("pop from empty list")
            old = array.copy()
            result = array[0]
            array = array[1:]
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array
            return result

        def clear(self) -> None:
            """Remove all flags from the entry."""
            array = self.array
            old = array.copy()
            array.clear()
            Flags._reset_flags(self.name, old)
            self.array = array

        def sort(
            self,
            *,
            key: Callable[[str], bool] | None = None,
            reverse: bool = False
        ) -> None:
            """Sort the entry in place.

            Parameters
            ----------
            key : Callable, optional
                A function to use as the key for sorting.  Defaults to None.
            reverse : bool, optional
                Whether to sort the entry in descending order, by default False.
            """
            array = self.array
            array.sort(key=key, reverse=reverse)
            Flags._reset_flags(self.name, array)
            Flags._extend_flags(self.name, array)
            self.array = array

        def reverse(self) -> None:
            """Reverse the order of the flags in the entry."""
            array = self.array
            array.reverse()
            Flags._reset_flags(self.name, array)
            Flags._extend_flags(self.name, array)
            self.array = array

        def rotate(self, n: int = 1) -> None:
            """Rotate the entry n steps to the right.

            Parameters
            ----------
            n : int
                The number of steps to rotate the entry to the right.
            """
            array = self.array
            temp = deque(array)
            temp.rotate(n)
            array = list(temp)
            Flags._reset_flags(self.name, array)
            Flags._extend_flags(self.name, array)
            self.array = array

        @overload
        def __setitem__(self, index: SupportsIndex, value: str) -> None: ...
        @overload
        def __setitem__(self, index: slice, value: Iterable[str]) -> None: ...
        def __setitem__(
            self,
            index: SupportsIndex | slice,
            value: str | Iterable[str]
        ) -> None:
            array = self.array
            old = array.copy()
            array[index] = value  # type: ignore
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

        def __delitem__(self, index: SupportsIndex | slice) -> None:
            array = self.array
            old = array.copy()
            del array[index]
            Flags._reset_flags(self.name, old)
            Flags._extend_flags(self.name, array)
            self.array = array

    @property
    def table(self) -> dict[str, Entry]:
        """Read the table from the env.toml file.

        Returns
        -------
        dict[str, Entry]
            An up-to-date view of the table.  The values are lazily-evaluated so that
            they always reflect the current state of the environment.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return {k: Flags.Entry(self, k) for k in super().table}

    @table.setter
    def table(self, value: dict[str, Entry]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            content["flags"] = {k: list(v) for k, v in value.items()}
            f.seek(0)
            tomlkit.dump(content, f)
            f.truncate()

    def copy(self) -> dict[str, list[str]]:  # type: ignore
        """Return a shallow copy of the table.

        Returns
        -------
        dict[str, list[Path]]
            A shallow copy of the table.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        return {k: list(v) for k, v in self.table.items()}

    def clear(self) -> None:
        """Remove all items from the table and the environment.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        old = {k: list(v) for k, v in table.items()}
        table.clear()
        for key in old:
            Flags._reset_flags(key, old[key])
        self.table = table

    def pop(self, key: str, default: list[str] | None = None) -> list[str] | None:  # type: ignore
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : list[str] | None, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        list[str] | None
            The value associated with the key, or the default value if the key is not
            found.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        if key in table:
            result = list(table.pop(key))
            Flags._reset_flags(key, result)
            self.table = table
            return result
        return default

    def get(self, key: str, default: list[str] | None = None) -> list[str] | None:  # type: ignore
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[str] | None, optional
            The value to return if the key is not found.  Defaults to None.

        Returns
        -------
        list[str] | None
            The value associated with the key, or the default value if the key is not
            found.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        result = self.table.get(key, default)
        if isinstance(result, Flags.Entry):
            return list(result)
        return result

    def setdefault(self, key: str, default: list[str]) -> Entry:
        """Get an item from the table or set it to a default value and return the
        default value.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[str]
            The value to set if the key is not found.

        Returns
        -------
        Entry
            A lazily-evaluated view of the value associated with the key.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        if key in table:
            return table[key]
        result = Flags.Entry(self, key, default)
        table[key] = result
        Flags._extend_flags(key, default)
        self.table = table
        return result

    def update(self, other: dict[str, list[str]]) -> None:  # type: ignore
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, list[str]]
            The dictionary to update the table with.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the table is not found in the file.
        TypeError
            If the table is not a table.
        """
        table = self.table
        old = {k: list(v) for k, v in table.items()}
        table.update({k: Flags.Entry(self, k, v) for k, v in other.items()})
        for key, value in other.items():
            if key in old:
                Flags._reset_flags(key, old[key])
            Flags._extend_flags(key, value)
        self.table = table

    def __getitem__(self, key: str) -> Entry:
        return Flags.Entry(self, key)

    def __setitem__(self, key: str, value: list[str]) -> None:  # type: ignore
        table = self.table
        entry = Flags.Entry(self, key, value)
        if key in table:
            old = list(table[key])
            table[key] = entry
            Flags._reset_flags(key, old)
        else:
            table[key] = entry
        Flags._extend_flags(key, value)
        self.table = table

    def __delitem__(self, key: str) -> None:
        table = self.table
        if key not in table:
            raise KeyError(key)
        old = list(table[key])
        del table[key]
        Flags._reset_flags(key, old)
        self.table = table

    def __or__(self, other: dict[str, list[str]]) -> dict[str, list[str]]:  # type: ignore
        return {k: list(v) for k, v in self.table.items()} | other

    def __ior__(self, other: dict[str, list[str]]) -> Flags:  # type: ignore
        self.update(other)
        return self


class Packages(Array["Packages.Entry"]):
    """An array of tables that represent the [packages] section of an env.toml file.
    Each table must have the following fields:

        - name: The name of the package.
        - version: The installed version of the package.
        - find: The symbol to pass to CMake's `find_package()` function in order to
            locate the package's headers and libraries.
        - link: The symbol to pass to CMake's `target_link_libraries()` function in
            order to link against the package's libraries.

    There is no environment variable associated with this section.
    """

    class Entry:
        """Represents a single entry in the packages array."""

        VALID_KEYS = {"name", "version", "find", "link"}

        def __init__(self, packages: Packages, table: dict[str, str]) -> None:
            for key in self.VALID_KEYS:
                if key not in table:
                    raise ValueError(f"Package entry must have a '{key}' field.")

            for key in table:
                if key not in self.VALID_KEYS:
                    raise ValueError(f"Unexpected key '{key}' in package entry.")

            self.packages = packages
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

        @property
        def version(self) -> str:
            """The installed version of the package.

            Returns
            -------
            str
                The version of the package.
            """
            return self.table["version"]

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

        def __getitem__(self, key: str) -> str:
            return self.table[key]

        def __hash__(self) -> int:
            return hash((self.name, self.version, self.find, self.link))

        def __lt__(self, other: Packages.Entry) -> bool:
            return (self.name, self.version) < (other.name, other.version)

        def __le__(self, other: Packages.Entry) -> bool:
            return self < other or self == other

        def __eq__(self, other: Any) -> bool:
            if isinstance(other, Packages.Entry):
                return self.table == other.table
            return self.table == other

        def __ne__(self, other: Any) -> bool:
            return not self == other

        def __ge__(self, other: Packages.Entry) -> bool:
            return self > other or self == other

        def __gt__(self, other: Packages.Entry) -> bool:
            return (self.name, self.version) > (other.name, other.version)

    @property
    def array(self) -> list[Entry]:
        """Read the array from the env.toml file.

        Returns
        -------
        list[Entry]
            An up-to-date view of the array.  The values are lazily-evaluated so that
            they always reflect the current state of the environment.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the env.toml file is not found.
        KeyError
            If the array is not found in the file.
        TypeError
            If the array is not an array.
        """
        return [Packages.Entry(self, table) for table in super().array]  # type: ignore

    @array.setter
    def array(self, value: list[Entry]) -> None:
        super().array = [entry.table for entry in value]  # type: ignore

    def append(self, value: dict[str, str]) -> None:  # type: ignore
        """Append a package entry to the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to append.
        """
        super().append(Packages.Entry(self, value))

    def appendleft(self, value: dict[str, str]) -> None:  # type: ignore
        """Prepend a package entry to the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to prepend.
        """
        super().appendleft(Packages.Entry(self, value))

    def extend(self, other: Iterable[dict[str, str]]) -> None:  # type: ignore
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[dict[str, str] | Entry]
            The sequence to extend the array with.
        """
        super().extend(Packages.Entry(self, value) for value in other)

    def extendleft(self, other: Iterable[dict[str, str]]) -> None:  # type: ignore
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[dict[str, str] | Entry]
            The sequence to extend the array with.
        """
        super().extendleft(Packages.Entry(self, value) for value in other)

    def insert(self, index: int, value: dict[str, str]) -> None:  # type: ignore
        """Insert a package entry into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the package entry at.
        value : dict[str, str] | Entry
            The package entry to insert into the array.
        """
        super().insert(index, Packages.Entry(self, value))

    def remove(self, value: dict[str, str]) -> None:  # type: ignore
        """Remove the first occurrence of a package entry from the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to remove from the array.
        """
        super().remove(Packages.Entry(self, value))

    def index(self, value: dict[str, str]) -> int:  # type: ignore
        """Return the index of the first occurrence of a package entry in the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to search for.

        Returns
        -------
        int
            The index of the package entry in the array.

        Raises
        ------
        ValueError
            If the package entry is not found in the array.
        """
        return super().index(Packages.Entry(self, value))

    def count(self, value: dict[str, str]) -> int:  # type: ignore
        """Return the number of occurrences of a package entry in the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to count.

        Returns
        -------
        int
            The number of occurrences of the package entry in the array.
        """
        return super().count(Packages.Entry(self, value))

    @overload  # type: ignore
    def __setitem__(self, index: SupportsIndex, value: dict[str, str]) -> None: ...
    @overload
    def __setitem__(self, index: slice, value: Iterable[dict[str, str]]) -> None: ...
    def __setitem__(
        self,
        index: SupportsIndex | slice,
        value: dict[str, str] | Iterable[dict[str, str]]
    ) -> None:
        if isinstance(index, slice):
            super().__setitem__(Packages.Entry(self, v) for v in value)  # type: ignore
        else:
            super().__setitem__(index, Packages.Entry(self, value))  # type: ignore

    def __add__(self, other: Iterable[dict[str, str]]) -> list[dict[str, str]]:  # type: ignore
        return [entry.table for entry in self.array] + list(other)

    def __iadd__(self, other: Iterable[dict[str, str]]) -> Packages:  # type: ignore
        self.extend(other)
        return self


class Environment:
    """A wrapper around an env.toml file that can be used to activate and deactivate
    the environment, as well as check or modify its state.
    """

    OLD_PREFIX = "_OLD_VIRTUAL_"
    _info: Table[Any]
    _vars: Vars
    _paths: Paths
    _flags: Flags
    _packages: Packages

    def __init__(self) -> None:
        raise NotImplementedError(
            "Environment should be used as a global object, not instantiated directly."
        )

    def __new__(cls) -> Environment:
        self = super().__new__(cls)
        self._info = Table(self, "info")
        self._vars = Vars(self, "vars")
        self._paths = Paths(self, "paths")
        self._flags = Flags(self, "flags")
        self._packages = Packages(self, "packages")
        return self

    @property
    def toml(self) -> Path:
        """The path to the env.toml file for the current virtual environment.

        Returns
        -------
        Path
            A path to the environment's configuration file.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the environment file does not exist.
        """
        if "BERTRAND_HOME" not in os.environ:
            raise RuntimeError("no environment is currently active.")
        path = Path(os.environ["BERTRAND_HOME"]) / "env.toml"
        if not path.exists():
            raise FileNotFoundError("environment file not found.")
        return path

    # TODO: no top-level table property for environment as a whole.  This is
    # intentional, since each table has its own semantics for conversions to/from
    # TOML format.  I just have to make sure that these values are written to the
    # toml file as part of initialization:

    # result = {
    #     "info": Table(self, "info", {}),
    #     "vars": Vars(self, "vars", {}),
    #     "paths": Paths(self, "paths", {
    #         "PATH": Paths.Entry(
    #             self._paths,
    #             "PATH",
    #             [venv / "bin"]
    #         ),
    #         "CPATH": Paths.Entry(
    #             self._paths,
    #             "CPATH",
    #             [venv / "include", Path(numpy.get_include()), Path(pybind11.get_include())]
    #         ),
    #         "LIBRARY_PATH": Paths.Entry(
    #             self._paths,
    #             "LIBRARY_PATH",
    #             [venv / "lib"]
    #         ),
    #         "LD_LIBRARY_PATH": Paths.Entry(
    #             self._paths,
    #             "LD_LIBRARY_PATH",
    #             [venv / "lib"]
    #         ),
    #     }),
    #     "flags": Flags(self, "flags", {
    #         "CFLAGS": Flags.Entry(self._flags, "CFLAGS", []),
    #         "CXXFLAGS": Flags.Entry(self._flags, "CXXFLAGS", []),
    #         "LDFLAGS": Flags.Entry(self._flags, "LDFLAGS", []),  
    #     }),
    #     "packages": Packages(self, "packages", []),
    # }

    @property
    def info(self) -> Table[Any]:
        """The [info] table from the env.toml file.

        Returns
        -------
        Table[Any]
            A dict-like object representing the [info] table in the env.toml file.
            Mutating the dictionary will automatically update the env.toml file with
            the new values.
        """
        return self._info

    @info.setter
    def info(self, value: dict[str, Any]) -> None:
        self._info.clear()
        self._info.update(value)

    @info.deleter
    def info(self) -> None:
        self._info.clear()

    @property
    def vars(self) -> Vars:
        """The [vars] table from the env.toml file.

        Returns
        -------
        Vars
            A dict-like object representing the [vars] table in the env.toml file.
            Mutating the dictionary will automatically update both the env.toml file
            and the system environment with the new values.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [vars] table in the environment file.
        TypeError
            If the [vars] table is not a table.
        """
        return self._vars

    @vars.setter
    def vars(self, value: dict[str, str]) -> None:
        self._vars.clear()
        self._vars.update(value)

    @vars.deleter
    def vars(self) -> None:
        self._vars.clear()

    @property
    def paths(self) -> Paths:
        """The [paths] table from the env.toml file.

        Returns
        -------
        Paths
            A dict-like object representing the [paths] table in the env.toml file.
            Mutating the dictionary will automatically update both the env.toml file
            and the system environment with the new values.

        Notes
        -----
        The [paths] table can only contain lists of Path objects.  Each path will be
        joined using the system's path separator and prepended to the corresponding
        environment variable when modified.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [paths] table in the environment file.
        TypeError
            If the [paths] table is not a table containing lists of strings.
        """
        with self.toml.open("r") as f:
            content = tomlkit.load(f)
            if "paths" not in content:
                raise KeyError(f"no [paths] table in file: {self.toml}")
            paths = content["paths"]
            if not isinstance(paths, tomlkit.items.AbstractTable):
                raise TypeError(f"[paths] must be a table, not {type(paths)}")
            if not all(
                isinstance(x, list) and all(isinstance(y, str) for y in x)
                for x in paths.values()
            ):
                raise TypeError("values in [paths] table must all be lists of strings")
            return Paths(self, {
                k: Paths.Entry(self, k, deque(Path(s) for s in v))
                for k, v in paths.items()
            })

    @paths.setter
    def paths(self, value: dict[str, list[Path]]) -> None:
        paths = self.paths
        paths.clear()
        paths.update(value)

    @paths.deleter
    def paths(self) -> None:
        self.paths.clear()

    @property
    def flags(self) -> Flags:
        """The [flags] table from the env.toml file.

        Returns
        -------
        Flags
            A dict-like object representing the [flags] table in the env.toml file.
            Mutating the dictionary will automatically update both the env.toml file
            and the system environment with the new values.

        Notes
        -----
        The [flags] table can only contain lists of strings.  Each flag will be joined
        using spaces and appended to the corresponding environment variable when
        modified.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [flags] table in the environment file.
        TypeError
            If the [flags] table is not a table containing lists of strings.
        """
        with self.toml.open("r") as f:
            content = tomlkit.load(f)
            if "flags" not in content:
                raise KeyError(f"No [flags] table in file: {self.toml}")
            flags = content["flags"]
            if not isinstance(flags, tomlkit.items.AbstractTable):
                raise TypeError(f"[flags] must be a table, not {type(flags)}")
            if not all(
                isinstance(x, list) and all(isinstance(y, str) for y in x)
                for x in flags.values()
            ):
                raise TypeError("values in [flags] table must all be lists of strings")
            return Flags(self, {k: Flags.Entry(self, k, deque(v)) for k, v in flags.items()})

    @flags.setter
    def flags(self, value: dict[str, list[str]]) -> None:
        flags = self.flags
        flags.clear()
        flags.update(value)

    @flags.deleter
    def flags(self) -> None:
        self.flags.clear()

    @property
    def packages(self) -> Packages:
        """The [packages] table from the env.toml file.

        Returns
        -------
        Packages
            A list-like object representing the [[packages]] array in the env.toml file.
            Each item in the list is a dict-like object with the fields 'name', 'version',
            'find', and 'link'.  Mutating the table will automatically update the env.toml
            file with the new values.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [packages] table in the environment file.
        TypeError
            If the [packages] table is not a list of tables.
        """
        with self.toml.open("r") as f:
            content = tomlkit.load(f)
            if "packages" not in content:
                raise KeyError(f"No [packages] table in file: {self.toml}")
            packages = content["packages"]
            if not isinstance(packages, list):
                raise TypeError(f"[packages] must be a list, not {type(packages)}")
            if not all(isinstance(p, tomlkit.items.Table) for p in packages):
                raise TypeError("[packages] must be a list of tables")
            return Packages(self, deque(Packages.Entry(self, p) for p in packages))

    @packages.setter
    def packages(self, value: list[dict[str, str]]) -> None:
        packages = self.packages
        packages.clear()
        packages.extend(value)

    @packages.deleter
    def packages(self) -> None:
        self.packages.clear()

    def activate(self, toml: Path) -> None:
        """Print a sequence of bash commands used to enter the virtual environment.

        Parameters
        ----------
        toml : Path
            The path to the env.toml file used to configure the environment.

        Raises
        ------
        ValueError
            If no environment file was found.

        Notes
        -----
        This method is called by the `$ bertrand activate` shell command, which is in
        turn called by the virtual environment's `activate` script.  Each command will
        be executed verbatim when the script is sourced.
        """
        if not toml.exists():
            raise ValueError("Environment file not found.")

        commands = []
        for key, value in os.environ.items():
            commands.append(f"export {Environment.OLD_PREFIX}{key}=\"{value}\"")

        # [vars] get exported directly
        for key, val_str in self.vars.items():
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




    def keys(self) -> KeysView[str]:
        """
        Returns
        -------
        KeysView[str]
            The keys of the environment.
        """
        return os.environ.keys()

    def values(self) -> ValuesView[Any]:
        """
        Returns
        -------
        ValuesView[str]
            The values of the environment.
        """
        return os.environ.values()

    def items(self) -> ItemsView[str, Any]:
        """
        Returns
        -------
        ItemsView[str, str]
            The items of the environment.
        """
        return os.environ.items()

    def copy(self) -> dict[str, Any]:
        """
        Returns
        -------
        dict[str, Any]
            A shallow copy of the environment.
        """
        return os.environ.copy()

    # TODO: methods that modify the environment should propagate to toml as well?

    def clear(self) -> None:
        """Clear the environment."""
        os.environ.clear()

    def get(self, key: str, default: Any = None) -> Any:
        """Get a value from the environment.

        Parameters
        ----------
        key : str
            The name of the variable to get.
        default : Any, optional
            The value to return if the variable is not found.  Defaults to None.

        Returns
        -------
        Any
            The value of the variable, or the default value if the variable is not
            found.
        """
        return os.environ.get(key, default)

    def pop(self, key: str, default: Any = None) -> Any:
        """Remove a variable from the environment and return its value.

        Parameters
        ----------
        key : str
            The name of the variable to remove.
        default : Any, optional
            The value to return if the variable is not found.  Defaults to None.

        Returns
        -------
        Any
            The value of the variable, or the default value if the variable is not
            found.
        """
        return os.environ.pop(key, default)

    def update(self, other: dict[str, Any]) -> None:
        """Update the environment with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the environment with.
        """
        os.environ.update(other)

    def __bool__(self) -> bool:
        """Check if there is an active virtual environment."""
        return "BERTRAND_HOME" in os.environ

    def __len__(self) -> int:
        return len(os.environ)

    def __iter__(self) -> Iterator[str]:
        return iter(os.environ.keys())

    def __reversed__(self) -> Iterator[str]:
        return reversed(os.environ.keys())

    def __getitem__(self, key: str) -> Any:
        return os.environ[key]

    def __setitem__(self, key: str, value: Any) -> None:
        vars_ = self.vars
        paths = self.paths
        flags = self.flags
        if key in paths:
            paths[key].clear()
            paths[key].extend(value)
        elif key in flags:
            flags[key].clear()
            flags[key].extend(value)
        else:
            vars_[key] = value

    def __delitem__(self, key: str) -> None:
        self.vars.pop(key)
        self.paths.pop(key)
        self.flags.pop(key)

    def __contains__(self, key: str) -> bool:
        return key in os.environ

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Environment):
            return self.toml == other.toml
        return False

    def __ne__(self, other: Any) -> bool:
        return not self == other

    def __or__(self, other: dict[str, Any]) -> dict[str, Any]:
        return os.environ | other

    def __ior__(self, other: dict[str, Any]) -> Environment:
        self.update(other)
        return self

    def __str__(self) -> str:
        return repr(self)

    def __repr__(self) -> str:
        if not self:
            return "<Environment: headless>"
        return f"<Environment: {self.toml}>"

    def __truediv__(self, key: Path | str) -> Path:
        """Navigate from the root of the virtual environment's directory."""
        if not self:
            raise RuntimeError("No environment is currently active.")
        return Path(os.environ["BERTRAND_HOME"]) / key


env = Environment.__new__(Environment)








# TODO: numpy.get_include() and pybind11.get_include() should be automatically added
# to CPATH so that they're always available without needing a get_include() method.



def get_include() -> str:
    """Return a list of paths to the include directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the include directories of the current virtual environment.
    """
    return str(Path(bertrand_root).absolute().parent)




def get_lib() -> list[str]:
    """Return a list of paths to the library directories of the current virtual
    environment.

    Returns
    -------
    list[str]
        A list of paths to the library directories of the current virtual environment.
    """
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
