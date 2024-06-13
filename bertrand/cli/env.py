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
    """A wrapper around a TOML table that automatically pushes changes to the env.toml
    file.  Subclasses can add behavior for modifying the environment as needed.
    """

    def __init__(self, environment: Environment, table: dict[str, T]) -> None:
        self.environment = environment
        self.table = table

    def keys(self) -> KeysView[str]:
        """
        Returns
        -------
        KeysView[str]
            A view on the keys stored in the table.
        """
        return self.table.keys()

    def values(self) -> ValuesView[T]:
        """
        Returns
        -------
        ValuesView[T]
            A view on the values stored in the table.
        """
        return self.table.values()

    def items(self) -> ItemsView[str, T]:
        """
        Returns
        -------
        ItemsView[str, T]
            A view on the key-value pairs stored in the table.
        """
        return self.table.items()

    def copy(self) -> dict[str, T]:
        """
        Returns
        -------
        dict[str, T]
            A shallow copy of the table.
        """
        return self.table.copy()

    def clear(self) -> None:
        """Remove all items from the table."""
        self.table.clear()
        self.environment.save()

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
        """
        result = self.table.pop(key, default)
        self.environment.save()
        return result

    def setdefault(self, key: str, default: T) -> T:
        """Set the value of a key if it is not already present, then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : T
            The value to set if the key is not already present.

        Returns
        -------
        T
            The value associated with the key.
        """
        result = self.table.setdefault(key, default)
        self.environment.save()
        return result

    def update(self, other: dict[str, T]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, T]
            The dictionary to update the table with.
        """
        self.table.update(other)
        self.environment.save()

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
        self.table[key] = value
        self.environment.save()

    def __delitem__(self, key: str) -> None:
        del self.table[key]
        self.environment.save()

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


class Array(Generic[T]):
    """A wrapper around a TOML array that automatically pushes changes to the env.toml
    file.  Subclasses can add behavior for modifying the environment as needed.
    """

    def __init__(self, environment: Environment, array: deque[T]) -> None:
        self.environment = environment
        self.array = array

    def append(self, value: T) -> None:
        """Append a value to the array.

        Parameters
        ----------
        value : T
            The value to append to the array.
        """
        self.array.append(value)
        self.environment.save()

    def appendleft(self, value: T) -> None:
        """Prepend a value to the array.

        Parameters
        ----------
        value : T
            The value to prepend to the array.
        """
        self.array.appendleft(value)
        self.environment.save()

    def extend(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        self.array.extend(other)
        self.environment.save()

    def extendleft(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        self.array.extendleft(other)
        self.environment.save()

    def insert(self, index: int, value: T) -> None:
        """Insert a value into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the value at.
        value : T
            The value to insert into the array.
        """
        self.array.insert(index, value)
        self.environment.save()

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
        """
        self.array.remove(value)
        self.environment.save()

    def pop(self) -> T:
        """Remove and return the last value in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        result = self.array.pop()
        self.environment.save()
        return result

    def popleft(self) -> T:
        """Remove and return the first item in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        result = self.array.popleft()
        self.environment.save()
        return result

    def copy(self) -> deque[T]:
        """Return a shallow copy of the array.

        Returns
        -------
        deque[T]
            A shallow copy of the array.
        """
        return self.array.copy()

    def clear(self) -> None:
        """Remove all items from the array."""
        self.array.clear()
        self.environment.save()

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
            A function to use as the key for sorting, by default None.
        reverse : bool, optional
            Whether to sort the array in descending order, by default False.
        """
        temp = list(self.array)
        temp.sort(key=key, reverse=reverse)
        self.array = deque(temp)
        self.environment.save()

    def reverse(self) -> None:
        """Reverse the order of the items in the array."""
        self.array.reverse()
        self.environment.save()

    def rotate(self, n: int) -> None:
        """Rotate the array n steps to the right.

        Parameters
        ----------
        n : int
            The number of steps to rotate the array to the right.
        """
        self.array.rotate(n)
        self.environment.save()

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
    def __getitem__(self, index: slice) -> deque[T]: ...
    def __getitem__(self, index: SupportsIndex | slice) -> T | deque[T]:
        if isinstance(index, slice):
            return deque(list(self.array)[index])
        return self.array[index]

    @overload
    def __setitem__(self, index: SupportsIndex, value: T) -> None: ...
    @overload
    def __setitem__(self, index: slice, value: Iterable[T]) -> None: ...
    def __setitem__(self, index: SupportsIndex | slice, value: T | Iterable[T]) -> None:
        if isinstance(index, slice):
            temp = list(self.array)
            temp[index] = value  # type: ignore
            self.array = deque(temp)
        else:
            self.array[index] = value  # type: ignore
        self.environment.save()

    def __delitem__(self, index: SupportsIndex | slice) -> None:
        if isinstance(index, slice):
            temp = list(self.array)
            del temp[index]
            self.array = deque(temp)
        else:
            del self.array[index]
        self.environment.save()

    def __eq__(self, other: Any) -> bool:
        return self.array == other

    def __ne__(self, other: Any) -> bool:
        return self.array != other

    def __add__(self, other: Iterable[T]) -> deque[T]:
        return self.array + deque(other)

    def __iadd__(self, other: Iterable[T]) -> Array[T]:
        self.extend(other)
        return self


class Vars(Table[Any]):
    """A table that represents the [vars] section of an env.toml file.  Pushes any
    changes to both the toml file and the current environment.
    """

    def clear(self) -> None:
        """Remove all items from the table."""
        keys = list(self.table)
        self.table.clear()
        for key in keys:
            os.environ.pop(key, None)
        self.environment.save()

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
        result = self.table.pop(key, default)
        os.environ.pop(key, None)
        self.environment.save()
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
        result = self.table.setdefault(key, default)
        os.environ[key] = result
        self.environment.save()
        return result

    def update(self, other: dict[str, Any]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the table with.
        """
        self.table.update(other)
        for key, value in other.items():
            os.environ[key] = value
        self.environment.save()

    def __setitem__(self, key: str, value: Any) -> None:
        self.table[key] = value
        os.environ[key] = value
        self.environment.save()

    def __delitem__(self, key: str) -> None:
        del self.table[key]
        os.environ.pop(key, None)
        self.environment.save()


class Paths(Table["Paths.Entry"]):
    """A table that represents the [paths] section of an env.toml file.  Entries in the
    table are joined with the system's path separator and prepended to the corresponding
    environment variable when modified.
    """

    @staticmethod
    def _reset_path(key: str, old: Iterable[Path]) -> None:
        if key in os.environ:
            ignore = {str(p) for p in old}
            os.environ[key] = os.pathsep.join(
                p for p in os.environ[key].split(os.pathsep) if p not in ignore
            )

    @staticmethod
    def _extend_path(key: str, value: Iterable[Path]) -> None:
        if key in os.environ:
            os.environ[key] = os.pathsep.join([*[str(p) for p in value], os.environ[key]])
        else:
            os.environ[key] = os.pathsep.join(str(p) for p in value)

    class Entry(Array[Path]):
        """Represents a single entry in the [paths] table."""

        # pylint: disable=protected-access

        def __init__(self, environment: Environment, key: str, array: deque[Path]) -> None:
            super().__init__(environment, array)
            self.key = key

        def append(self, value: Path) -> None:
            """Append a path to the entry.

            Parameters
            ----------
            value : Path
                The path to append.
            """
            temp = self.array.copy()
            self.array.append(value)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def appendleft(self, value: Path) -> None:
            """Prepend a path to the entry.

            Parameters
            ----------
            value : Path
                The path to prepend.
            """
            temp = self.array.copy()
            self.array.appendleft(value)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def extend(self, other: Iterable[Path]) -> None:
            """Extend the entry with the contents of another sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the entry with.
            """
            temp = self.array.copy()
            self.array.extend(other)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def extendleft(self, other: Iterable[Path]) -> None:
            """Extend the entry with the contents of another sequence, prepending each
            item.

            Note that this implicitly reverses the order of the items in the sequence.

            Parameters
            ----------
            other : Iterable[Path]
                The sequence to extend the entry with.
            """
            temp = self.array.copy()
            self.array.extendleft(other)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def insert(self, index: int, value: Path) -> None:
            """Insert a path into the entry at a specific index.

            Parameters
            ----------
            index : int
                The index to insert the path at.
            value : Path
                The path to insert into the entry.
            """
            temp = self.array.copy()
            self.array.insert(index, value)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

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
            temp = self.array.copy()
            self.array.remove(value)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def pop(self) -> Path:
            """Remove and return the last path in the entry.

            Returns
            -------
            Path
                The path that was removed.
            """
            temp = self.array.copy()
            result = self.array.pop()
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()
            return result

        def popleft(self) -> Path:
            """Remove and return the first path in the entry.

            Returns
            -------
            Path
                The path that was removed.
            """
            temp = self.array.copy()
            result = self.array.popleft()
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()
            return result

        def clear(self) -> None:
            """Remove all paths from the entry."""
            temp = self.array.copy()
            self.array.clear()
            Paths._reset_path(self.key, temp)
            self.environment.save()

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
                A function to use as the key for sorting, by default None.
            reverse : bool, optional
                Whether to sort the entry in descending order, by default False.
            """
            temp = list(self.array)
            temp.sort(key=key, reverse=reverse)
            self.array = deque(temp)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def reverse(self) -> None:
            """Reverse the order of the paths in the entry."""
            temp = self.array.copy()
            self.array.reverse()
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def rotate(self, n: int = 1) -> None:
            """Rotate the entry n steps to the right.

            Parameters
            ----------
            n : int
                The number of steps to rotate the entry to the right.
            """
            temp = self.array.copy()
            self.array.rotate(n)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        @overload
        def __setitem__(self, index: SupportsIndex, value: Path) -> None: ...
        @overload
        def __setitem__(self, index: slice, value: Iterable[Path]) -> None: ...
        def __setitem__(
            self,
            index: SupportsIndex | slice,
            value: Path | Iterable[Path]
        ) -> None:
            temp = list(self.array)
            temp[index] = value  # type: ignore
            self.array = deque(temp)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

        def __delitem__(self, index: SupportsIndex | slice) -> None:
            temp = list(self.array)
            del temp[index]
            self.array = deque(temp)
            Paths._reset_path(self.key, temp)
            Paths._extend_path(self.key, self.array)
            self.environment.save()

    def clear(self) -> None:
        """Remove all items from the table and the environment."""
        temp = self.table.copy()
        self.table.clear()
        for key in temp:
            Paths._reset_path(key, temp[key])
        self.environment.save()

    def get(self, key: str, default: list[Path] | Entry | None = None) -> Entry | None:
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[Path] | Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        if isinstance(default, list):
            default = Paths.Entry(self.environment, key, deque(default))
        return self.table.get(key, default)

    def pop(self, key: str, default: list[Path] | Entry | None = None) -> Entry | None:
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : list[Path] | Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        if isinstance(default, list):
            default = Paths.Entry(self.environment, key, deque(default))
        result = self.table.pop(key, default)
        if result is not None:
            Paths._reset_path(key, result)
        self.environment.save()
        return result

    def setdefault(self, key: str, default: list[Path] | Entry) -> Entry:
        """Set the value of a key if it is not already present in the table or the
        environment, and then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : list[Path] | Entry
            The value to set if the key is not already present.

        Returns
        -------
        Entry
            The value associated with the key.
        """
        if isinstance(default, list):
            default = Paths.Entry(self.environment, key, deque(default))
        replace = key not in self.table
        result = self.table.setdefault(key, default)
        if replace:
            Paths._extend_path(key, result)
        self.environment.save()
        return result

    def update(self, other: dict[str, list[Path]] | dict[str, Entry]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, list[Path]]
            The dictionary to update the table with.
        """
        # convert lists to Entries
        normalized: dict[str, Paths.Entry] = {}
        for key, value in other.items():
            if isinstance(value, list):
                normalized[key] = Paths.Entry(self.environment, key, deque(value))
            else:
                normalized[key] = value

        # do the update
        temp = self.table.copy()
        self.table.update(normalized)

        # update the environment
        for key, value in normalized.items():
            if key in temp:
                Paths._reset_path(key, temp[key])
            Paths._extend_path(key, value)

        # save the changes
        self.environment.save()

    def __setitem__(self, key: str, value: list[Path] | Entry) -> None:
        if isinstance(value, list):
            value = Paths.Entry(self.environment, key, deque(value))

        if key in self.table:
            old = self.table[key]
            self.table[key] = value
            Paths._reset_path(key, old)
        else:
            self.table[key] = value

        Paths._extend_path(key, value)
        self.environment.save()

    def __delitem__(self, key: str) -> None:
        if key in self.table:
            old = self.table[key]
            del self.table[key]
            Paths._reset_path(key, old)
        else:
            del self.table[key]
        self.environment.save()

    def __or__(self, other: dict[str, list[Path]] | dict[str, Entry]) -> dict[str, Entry]:
        normalized: dict[str, Paths.Entry] = {}
        for k, v in other.items():
            if isinstance(v, list):
                normalized[k] = Paths.Entry(self.environment, k, deque(v))
            else:
                normalized[k] = v
        return self.table | normalized

    def __ior__(self, other: dict[str, list[Path]] | dict[str, Entry]) -> Paths:  # type: ignore
        self.update(other)
        return self


class Flags(Table["Flags.Entry"]):
    """A table that represents the [flags] section of an env.toml file.  Entries in the
    table are joined with spaces and prepended to the corresponding environment variable
    when modified.
    """

    @staticmethod
    def _reset_flags(key: str, old: Iterable[str]) -> None:
        if key in os.environ:
            os.environ[key] = " ".join(
                f for f in os.environ[key].split(" ") if f not in old
            )

    @staticmethod
    def _extend_flags(key: str, value: Iterable[str]) -> None:
        if key in os.environ:
            os.environ[key] = " ".join([os.environ[key], *value])
        else:
            os.environ[key] = " ".join(value)

    class Entry(Array[str]):
        """Represents a single entry in the [flags] table."""

        # pylint: disable=protected-access

        def __init__(self, environment: Environment, key: str, array: deque[str]) -> None:
            super().__init__(environment, array)
            self.key = key

        def append(self, value: str) -> None:
            """Append a flag to the entry.

            Parameters
            ----------
            value : str
                The flag to append.
            """
            temp = self.array.copy()
            self.array.append(value)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def appendleft(self, value: str) -> None:
            """Prepend a flag to the entry.

            Parameters
            ----------
            value : str
                The flag to prepend.
            """
            temp = self.array.copy()
            self.array.appendleft(value)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def extend(self, other: Iterable[str]) -> None:
            """Extend the entry with the contents of another sequence.

            Parameters
            ----------
            other : Iterable[str]
                The sequence to extend the entry with.
            """
            temp = self.array.copy()
            self.array.extend(other)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def extendleft(self, other: Iterable[str]) -> None:
            """Extend the entry with the contents of another sequence, prepending each
            item.

            Note that this implicitly reverses the order of the items in the sequence.

            Parameters
            ----------
            other : Iterable[str]
                The sequence to extend the entry with.
            """
            temp = self.array.copy()
            self.array.extendleft(other)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def insert(self, index: int, value: str) -> None:
            """Insert a flag into the entry at a specific index.

            Parameters
            ----------
            index : int
                The index to insert the flag at.
            value : str
                The flag to insert into the entry.
            """
            temp = self.array.copy()
            self.array.insert(index, value)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

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
            temp = self.array.copy()
            self.array.remove(value)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def pop(self) -> str:
            """Remove and return the last flag in the entry.

            Returns
            -------
            str
                The flag that was removed.
            """
            temp = self.array.copy()
            result = self.array.pop()
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()
            return result

        def popleft(self) -> str:
            """Remove and return the first flag in the entry.

            Returns
            -------
            str
                The flag that was removed.
            """
            temp = self.array.copy()
            result = self.array.popleft()
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()
            return result

        def clear(self) -> None:
            """Remove all flags from the entry."""
            temp = self.array.copy()
            self.array.clear()
            Flags._reset_flags(self.key, temp)
            self.environment.save()

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
                A function to use as the key for sorting, by default None.
            reverse : bool, optional
                Whether to sort the entry in descending order, by default False.
            """
            temp = list(self.array)
            temp.sort(key=key, reverse=reverse)
            self.array = deque(temp)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def reverse(self) -> None:
            """Reverse the order of the flags in the entry."""
            temp = self.array.copy()
            self.array.reverse()
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def rotate(self, n: int = 1) -> None:
            """Rotate the entry n steps to the right.

            Parameters
            ----------
            n : int
                The number of steps to rotate the entry to the right.
            """
            temp = self.array.copy()
            self.array.rotate(n)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        @overload
        def __setitem__(self, index: SupportsIndex, value: str) -> None: ...
        @overload
        def __setitem__(self, index: slice, value: Iterable[str]) -> None: ...
        def __setitem__(
            self,
            index: SupportsIndex | slice,
            value: str | Iterable[str]
        ) -> None:
            temp = list(self.array)
            temp[index] = value  # type: ignore
            self.array = deque(temp)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

        def __delitem__(self, index: SupportsIndex | slice) -> None:
            temp = list(self.array)
            del temp[index]
            self.array = deque(temp)
            Flags._reset_flags(self.key, temp)
            Flags._extend_flags(self.key, self.array)
            self.environment.save()

    def clear(self) -> None:
        """Remove all items from the table and the environment."""
        temp = self.table.copy()
        self.table.clear()
        for key in temp:
            Flags._reset_flags(key, temp[key])
        self.environment.save()

    def get(self, key: str, default: list[str] | Entry | None = None) -> Entry | None:
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : list[str] | Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        if isinstance(default, list):
            default = Flags.Entry(self.environment, key, deque(default))
        return self.table.get(key, default)

    def pop(self, key: str, default: list[str] | Entry | None = None) -> Entry | None:
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : list[str] | Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        if isinstance(default, list):
            default = Flags.Entry(self.environment, key, deque(default))
        result = self.table.pop(key, default)
        if result is not None:
            Flags._reset_flags(key, result)
        self.environment.save()
        return result

    def setdefault(self, key: str, default: list[str] | Entry) -> Entry:
        """Set the value of a key if it is not already present in the table or the
        environment, and then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : list[str] | Entry
            The value to set if the key is not already present.

        Returns
        -------
        Entry
            The value associated with the key.
        """
        if isinstance(default, list):
            default = Flags.Entry(self.environment, key, deque(default))
        replace = key not in self.table
        result = self.table.setdefault(key, default)
        if replace:
            Flags._extend_flags(key, result)
        self.environment.save()
        return result

    def update(self, other: dict[str, list[str]] | dict[str, Entry]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, list[str]] | dict[str, Entry]
            The dictionary to update the table with.
        """
        # convert lists to Entries
        normalized: dict[str, Flags.Entry] = {}
        for key, value in other.items():
            if isinstance(value, list):
                normalized[key] = Flags.Entry(self.environment, key, deque(value))
            else:
                normalized[key] = value

        # do the update
        temp = self.table.copy()
        self.table.update(normalized)

        # update the environment
        for key, value in normalized.items():
            if key in temp:
                Flags._reset_flags(key, temp[key])
            Flags._extend_flags(key, value)

        # save the changes
        self.environment.save()

    def __setitem__(self, key: str, value: list[str] | Entry) -> None:
        if isinstance(value, list):
            value = Flags.Entry(self.environment, key, deque(value))

        if key in self.table:
            old = self.table[key]
            self.table[key] = value
            Flags._reset_flags(key, old)
        else:
            self.table[key] = value

        Flags._extend_flags(key, value)
        self.environment.save()

    def __delitem__(self, key: str) -> None:
        if key in self.table:
            old = self.table[key]
            del self.table[key]
            Flags._reset_flags(key, old)
        else:
            del self.table[key]
        self.environment.save()

    def __or__(self, other: dict[str, list[str]] | dict[str, Entry]) -> dict[str, Entry]:
        normalized: dict[str, Flags.Entry] = {}
        for k, v in other.items():
            if isinstance(v, list):
                normalized[k] = Flags.Entry(self.environment, k, deque(v))
            else:
                normalized[k] = v
        return self.table | normalized

    def __ior__(self, other: dict[str, list[str]] | dict[str, Entry]) -> Flags:  # type: ignore
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

        def __init__(self, environment: Environment, table: dict[str, str]) -> None:
            for key in self.VALID_KEYS:
                if key not in table:
                    raise ValueError(f"Package entry must have a '{key}' field.")

            for key in table:
                if key not in self.VALID_KEYS:
                    raise ValueError(f"Unexpected key '{key}' in package entry.")

            self.environment = environment
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
            self.environment.save()

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
            self.environment.save()

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
            self.environment.save()

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
            self.environment.save()

        def __eq__(self, other: Any) -> bool:
            if isinstance(other, Packages.Entry):
                return self.table == other.table
            return self.table == other

        def __ne__(self, other: Any) -> bool:
            return not self == other

    def append(self, value: dict[str, str] | Entry) -> None:
        """Append a package entry to the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to append.
        """
        if isinstance(value, dict):
            value = Packages.Entry(self.environment, value)
        super().append(value)

    def appendleft(self, value: dict[str, str] | Entry) -> None:
        """Prepend a package entry to the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to prepend.
        """
        if isinstance(value, dict):
            value = Packages.Entry(self.environment, value)
        super().appendleft(value)

    def extend(self, other: Iterable[dict[str, str] | Entry]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[dict[str, str] | Entry]
            The sequence to extend the array with.
        """
        super().extend(
            Packages.Entry(self.environment, value) if isinstance(value, dict) else value
            for value in other
        )

    def extendleft(self, other: Iterable[dict[str, str] | Entry]) -> None:
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[dict[str, str] | Entry]
            The sequence to extend the array with.
        """
        super().extendleft(
            Packages.Entry(self.environment, value) if isinstance(value, dict) else value
            for value in other
        )

    def insert(self, index: int, value: dict[str, str] | Entry) -> None:
        """Insert a package entry into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the package entry at.
        value : dict[str, str] | Entry
            The package entry to insert into the array.
        """
        if isinstance(value, dict):
            value = Packages.Entry(self.environment, value)
        super().insert(index, value)

    def remove(self, value: dict[str, str] | Entry) -> None:
        """Remove the first occurrence of a package entry from the array.

        Parameters
        ----------
        value : dict[str, str] | Entry
            The package entry to remove from the array.
        """
        if isinstance(value, dict):
            value = Packages.Entry(self.environment, value)
        super().remove(value)

    @overload
    def __setitem__(self, index: SupportsIndex, value: dict[str, str] | Entry) -> None: ...
    @overload
    def __setitem__(self, index: slice, value: Iterable[dict[str, str] | Entry]) -> None: ...
    def __setitem__(
        self,
        index: SupportsIndex | slice,
        value: dict[str, str] | Entry | Iterable[dict[str, str] | Entry]
    ) -> None:
        if isinstance(index, slice):
            super().__setitem__(
                Packages.Entry(self.environment, v) if isinstance(v, dict) else v  # type: ignore
                for v in value  # type: ignore
            )
        else:
            if isinstance(value, dict):
                value = Packages.Entry(self.environment, value)
            super().__setitem__(index, value)  # type: ignore

    def __add__(self, other: Iterable[dict[str, str] | Entry]) -> deque[Entry]:
        return self.array + deque(
            Packages.Entry(self.environment, v) if isinstance(v, dict) else v
            for v in other
        )

    def __iadd__(self, other: Iterable[dict[str, str] | Entry]) -> Packages:  # type: ignore
        self.extend(other)
        return self








class Environment:
    """A wrapper around an env.toml file that can be used to activate and deactivate
    the environment, as well as check or modify its state.
    """

    OLD_PREFIX = "_OLD_VIRTUAL_"

    def __init__(self) -> None:
        raise NotImplementedError(
            "Environment should be used as a global object, not instantiated directly."
        )

    @property
    def toml(self) -> Path:
        """Get the path to the env.toml file for the current virtual environment.

        Returns
        -------
        Path
            The path to the environment's configuration file.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        FileNotFoundError
            If the environment file does not exist.
        """
        if not self:
            raise RuntimeError("No environment is currently active.")
        path = Path(os.environ["BERTRAND_HOME"]) / "env.toml"
        if not path.exists():
            raise FileNotFoundError("Environment file not found.")
        return path

    @property
    def info(self) -> Table[Any]:
        """The [info] table from the env.toml file.

        Returns
        -------
        Table[Any]
            A dict-like object representing the [info] table in the env.toml file.
            Mutating the table will automatically update the env.toml file with the new
            values.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [info] table in the environment file.
        TypeError
            If the [info] table is not a table.
        """
        with self.toml.open("r") as f:
            content = tomlkit.load(f)
            if "info" not in content:
                raise KeyError(f"No [info] table in file: {self.toml}")
            info = content["info"]
            if not isinstance(info, tomlkit.items.AbstractTable):
                raise TypeError(f"[info] must be a table, not {type(info)}")
            return Table(self, dict(info))

    @property
    def vars(self) -> Vars:
        """The [vars] table from the env.toml file.

        Returns
        -------
        Vars
            A dict-like object representing the [vars] table in the env.toml file.
            Mutating the table will automatically update both the env.toml file and the
            system environment with the new values.

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
        with self.toml.open("r") as f:
            content = tomlkit.load(f)
            if "vars" not in content:
                raise KeyError(f"No [vars] table in file: {self.toml}")
            vars_ = content["vars"]
            if not isinstance(vars_, tomlkit.items.AbstractTable):
                raise TypeError(f"[vars] must be a table, not {type(vars)}")
            return Vars(self, dict(vars_))

    # TODO: paths, flags, and packages should all operate with ordinary data structures,
    # and convert them into the proper entry types, rather than requiring the user to
    # create the entry types themselves.

    @property
    def paths(self) -> Paths:
        """The [paths] table from the env.toml file.

        Returns
        -------
        Paths
            A dict-like object representing the [paths] table in the env.toml file.
            Mutating the table will automatically update both the env.toml file and the
            system environment with the new values.

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
                raise KeyError(f"No [paths] table in file: {self.toml}")
            paths = content["paths"]
            if not isinstance(paths, tomlkit.items.AbstractTable):
                raise TypeError(f"[paths] must be a table, not {type(paths)}")
            if not all(
                isinstance(x, list) and all(isinstance(y, str) for y in x)
                for x in paths.values()
            ):
                raise TypeError("values in [paths] table must all be lists of strings")
            return Paths(self, {k: deque(v) for k, v in paths.items()})

    @property
    def flags(self) -> Flags:
        """The [flags] table from the env.toml file.

        Returns
        -------
        Flags
            A dict-like object representing the [flags] table in the env.toml file.
            Mutating the table will automatically update both the env.toml file and the
            system environment with the new values.

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
            return Flags(self, {k: deque(v) for k, v in flags.items()})

    @property
    def packages(self) -> Packages:
        """The [packages] table from the env.toml file.

        Returns
        -------
        Packages
            A list-like object representing the [packages] table in the env.toml file.
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
            if not all(
                isinstance(p, tomlkit.items.Table) and "name" in p and "version" in p
                and "find" in p and "link" in p
                for p in packages
            ):
                raise TypeError(f"[packages] must be a list of tables")
            return Packages(self, [Packages.Entry(self, p) for p in packages])

    def save(self) -> None:
        """Write the current environment to the TOML file.

        Raises
        ------
        RuntimeError
            If no environment is active.
        """
        if "BERTRAND_HOME" not in os.environ:
            raise RuntimeError("environment file not found.")

        path = Path(os.environ["BERTRAND_HOME"]) / "env.toml"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as file:
            content: dict[str, Any] = {}
            content["info"] = self.info.table if self.info else {}
            content["vars"] = self.vars.table if self.vars else {}
            if self.paths:
                content["paths"] = {k: list(v) for k, v in self.paths.items()}
            else:
                content["paths"] = {
                    "PATH": [],
                    "CPATH": [],
                    "LIBRARY_PATH": [],
                    "LD_LIBRARY_PATH": [],
                }
            if self.flags:
                content["flags"] = {k: list(v) for k, v in self.flags.items()}
            else:
                content["flags"] = {
                    "CFLAGS": [],
                    "CXXFLAGS": [],
                    "LDFLAGS": [],
                }
            if self.packages:
                content["packages"] = [
                    {"name": p.name, "version": p.version, "find": p.find, "link": p.link}
                    for p in self.packages
                ]
            else:
                content["packages"] = []
            tomlkit.dump(content, file)

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

    def __bool__(self) -> bool:
        """Check if there is an active virtual environment."""
        return "BERTRAND_HOME" in os.environ

    def __contains__(self, key: str) -> bool:
        """Check if a key exists in the current environment."""
        return key in os.environ

    def __getitem__(self, key: str) -> Any:
        """Get the value of a key in the current environment."""
        return os.environ[key]

    def __setitem__(self, key: str, value: Any) -> None:
        """Set the value of a key in the current environment."""
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
        """Clear a key from the current environment."""
        self.vars.pop(key)
        self.paths.pop(key)
        self.flags.pop(key)

    def __truediv__(self, key: Path | str) -> Path:
        """Navigate from the root of the virtual environment's directory."""
        if not self:
            raise RuntimeError("No environment is currently active.")
        return Path(os.environ["BERTRAND_HOME"]) / key

    def __repr__(self) -> str:
        if not self:
            return "<Environment: headless>"
        return f"<Environment: {self.toml}>"



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



