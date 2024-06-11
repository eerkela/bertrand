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


# TODO: environment should actually be a global object, so there's no need for the
# current() or exists() methods.  Just check the truthiness of the object itself.

# TODO: this class could be imported into init.py and used to standardize everything
# and incrementally build the environment.


T = TypeVar("T")


class Table(Generic[T]):
    """A wrapper around a TOML table that automatically pushes changes to the env.toml
    file.  Subclasses can add behavior for modifying the environment as needed.
    """

    def __init__(self, env: Environment, table: dict[str, T]) -> None:
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

    def values(self) -> ValuesView[T]:
        """Return a view of the values in the table.

        Returns
        -------
        ValuesView[T]
            A view of the values in the table.
        """
        return self.table.values()

    def items(self) -> ItemsView[str, T]:
        """Return a view of the items in the table.

        Returns
        -------
        ItemsView[str, T]
            A view of the items in the table.
        """
        return self.table.items()

    def copy(self) -> dict[str, T]:
        """Return a shallow copy of the table.

        Returns
        -------
        dict[str, T]
            A shallow copy of the table.
        """
        return self.table.copy()

    def clear(self) -> None:
        """Remove all items from the table."""
        self.table.clear()
        self.env.save()

    def get(self, key: str, default: T | None = None) -> T | None:
        """Get an item from the table.

        Parameters
        ----------
        key : str
            The key to look up in the table.
        default : T | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        T | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        return self.table.get(key, default)

    def pop(self, key: str, default: T | None = None) -> T | None:
        """Remove an item from the table and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table.
        default : T, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        T
            The value associated with the key, or the default value if the key is not
            found.
        """
        result = self.table.pop(key, default)
        self.env.save()
        return result

    def setdefault(self, key: str, default: T) -> T:
        """Set the value of a key if it is not already present, and then return the
        value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : T, optional
            The value to set if the key is not already present, by default None.

        Returns
        -------
        T
            The value associated with the key.
        """
        result = self.table.setdefault(key, default)
        self.env.save()
        return result

    def update(self, other: dict[str, T]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, T]
            The dictionary to update the table with.
        """
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

    def __getitem__(self, key: str) -> T:
        return self.table[key]

    def __setitem__(self, key: str, value: T) -> None:
        self.table[key] = value
        self.env.save()

    def __delitem__(self, key: str) -> None:
        del self.table[key]
        self.env.save()

    def __getattr__(self, key: str) -> T:
        return self.table[key]

    def __setattr__(self, key: str, value: T) -> None:
        self.table[key] = value

    def __delattr__(self, key: str) -> None:
        del self.table[key]

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

    def __init__(self, env: Environment, array: deque[T]) -> None:
        self.env = env
        self.array = array

    def append(self, value: T) -> None:
        """Append a value to the array.

        Parameters
        ----------
        value : T
            The value to append to the array.
        """
        self.array.append(value)
        self.env.save()

    def appendleft(self, value: T) -> None:
        """Prepend a value to the array.

        Parameters
        ----------
        value : T
            The value to prepend to the array.
        """
        self.array.appendleft(value)
        self.env.save()

    def extend(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        self.array.extend(other)
        self.env.save()

    def extendleft(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence, prepending each item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        self.array.extendleft(other)
        self.env.save()

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
        self.env.save()

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
        self.env.save()

    def pop(self) -> T:
        """Remove and return the last value in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        result = self.array.pop()
        self.env.save()
        return result

    def popleft(self) -> T:
        """Remove and return the first item in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        result = self.array.popleft()
        self.env.save()
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
        self.env.save()

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
        self.env.save()

    def __delitem__(self, index: SupportsIndex | slice) -> None:
        if isinstance(index, slice):
            temp = list(self.array)
            del temp[index]
            self.array = deque(temp)
        else:
            del self.array[index]
        self.env.save()

    def __eq__(self, other: Any) -> bool:
        return self.array == other

    def __ne__(self, other: Any) -> bool:
        return self.array != other

    def __add__(self, other: deque[T]) -> deque[T]:
        return self.array + other

    def __iadd__(self, other: deque[T]) -> Array[T]:
        self.extend(other)
        return self


class SepArray(Array[T]):
    """A list-like object that stores a sequence of values separated by a delimiter.
    Changes to the list will be reflected in the corresponding environment variable by
    converting each value to a string, joining them with the delimiter, and then
    prepending the result to the environment variable.
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

    def __init__(
        self,
        env: Environment,
        key: str,
        array: deque[T],
        sep: str,
    ) -> None:
        super().__init__(env, array)
        self.key = key
        self.sep = sep

    def append(self, value: T) -> None:
        """Append a value to the array.

        Parameters
        ----------
        value : T
            The value to append to the array.
        """
        n = len(self.array)
        super().append(value)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*components[:n], str(value), *components[n:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(p) for p in self.array)

    def appendleft(self, value: T) -> None:
        """Prepend a value to the array.

        Parameters
        ----------
        value : T
            The value to prepend to the array.
        """
        super().appendleft(value)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join([str(value), *components])
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def extend(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        n = len(self.array)
        super().extend(other)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*components[:n], *[str(v) for v in other], *components[n:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def extendleft(self, other: Iterable[T]) -> None:
        """Extend the array with the contents of another sequence, prepending each
        item.

        Note that this implicitly reverses the order of the items in the sequence.

        Parameters
        ----------
        other : Iterable[T]
            The sequence to extend the array with.
        """
        super().extendleft(other)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in other], *components]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def insert(self, index: int, value: T) -> None:
        """Insert a value into the array at a specific index.

        Parameters
        ----------
        index : int
            The index to insert the value at.
        value : T
            The value to insert into the array.
        """
        index = self._normalize_index(index, truncate=True)
        super().insert(index, value)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*components[:index], str(value), *components[index:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

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
        index = self.array.index(value)
        super().remove(value)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*components[:index], *components[index + 1:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def pop(self) -> T:
        """Remove and return the last value in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        n = len(self.array)
        result = super().pop()
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*components[:n - 1], *components[n:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)
        return result

    def popleft(self) -> T:
        """Remove and return the first item in the array.

        Returns
        -------
        T
            The value of the value that was removed.
        """
        result = super().popleft()
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(components[1:])
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)
        return result

    def clear(self) -> None:
        """Remove all items from the array."""
        n = len(self.array)
        super().clear()
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(components[n:])

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
        super().sort(key=key, reverse=reverse)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in self.array], *components[len(self.array):]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def reverse(self) -> None:
        """Reverse the order of the items in the array."""
        super().reverse()
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in self.array], *components[len(self.array):]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def rotate(self, n: int) -> None:
        """Rotate the array n steps to the right.

        Parameters
        ----------
        n : int
            The number of steps to rotate the array to the right.
        """
        super().rotate(n)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in self.array], *components[len(self.array):]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    @overload
    def __setitem__(self, index: SupportsIndex, value: T) -> None: ...
    @overload
    def __setitem__(self, index: slice, value: Iterable[T]) -> None: ...
    def __setitem__(self, index: SupportsIndex | slice, value: T | Iterable[T]) -> None:
        n = len(self.array)
        super().__setitem__(index, value)  # type: ignore
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in self.array], *components[n:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)

    def __delitem__(self, index: SupportsIndex | slice) -> None:
        n = len(self.array)
        super().__delitem__(index)
        if self.key in os.environ:
            components = self.sep.split(os.environ[self.key])
            os.environ[self.key] = self.sep.join(
                [*[str(v) for v in self.array], *components[n:]]
            )
        else:
            os.environ[self.key] = self.sep.join(str(v) for v in self.array)


class Vars(Table[Any]):
    """A table that represents the [vars] section of an env.toml file.  Pushes any
    changes to the current environment.
    """

    def clear(self) -> None:
        """Remove all items from the table."""
        for key in self.table:
            os.environ.pop(key, None)
        super().clear()

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
        return super().pop(key, default)

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
        return super().setdefault(key, default)

    def update(self, other: dict[str, Any]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the table with.
        """
        for key, value in other.items():
            os.environ[key] = value
        super().update(other)

    def __setitem__(self, key: str, value: Any) -> None:
        os.environ[key] = value
        super().__setitem__(key, value)

    def __delitem__(self, key: str) -> None:
        os.environ.pop(key, None)
        super().__delitem__(key)


class Paths(Table["Paths.Entry"]):
    """A table that represents the [paths] section of an env.toml file.  Entries in the
    table are joined with the system's path separator and prepended to the corresponding
    environment variable when modified.
    """

    class Entry(SepArray[Path]):
        """Represents a single entry in the [paths] table."""

        def __init__(self, env: Environment, key: str, array: deque[Path]) -> None:
            super().__init__(env, key, array, os.pathsep)

    def _reset_var(self, key: str) -> None:
        """Reset an environment variable to its original value.

        Parameters
        ----------
        key : str
            The key to reset.
        """
        if key in os.environ:
            components = os.pathsep.split(os.environ[key])
            value = {str(p) for p in self.table[key]}
            new = [c for c in components if c not in value]
            os.environ[key] = os.pathsep.join(new)

    def clear(self) -> None:
        """Remove all items from the table and the environment."""
        for key in self.table:
            self._reset_var(key)
        super().clear()

    def pop(self, key: str, default: Entry | None = None) -> Entry | None:
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        self._reset_var(key)
        return super().pop(key, default)

    def setdefault(self, key: str, default: Entry) -> Entry:
        """Set the value of a key if it is not already present in the table or the
        environment, and then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : Entry
            The value to set if the key is not already present.

        Returns
        -------
        Entry
            The value associated with the key.
        """
        if key not in self.table:
            if key in os.environ:
                os.environ[key] = os.pathsep.join(
                    [*[str(p) for p in default], os.environ[key]]
                )
            else:
                os.environ[key] = os.pathsep.join(str(p) for p in default)
        return super().setdefault(key, default)

    def update(self, other: dict[str, Entry]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Entry]
            The dictionary to update the table with.
        """
        for key, value in other.items():
            if key in self.table:
                self._reset_var(key)
            if key in os.environ:
                os.environ[key] = os.pathsep.join(
                    [*[str(p) for p in value], os.environ[key]]
                )
            else:
                os.environ[key] = os.pathsep.join(str(p) for p in value)
        super().update(other)

    def __setitem__(self, key: str, value: Entry) -> None:
        if key in self.table:
            self._reset_var(key)
        if key in os.environ:
            os.environ[key] = os.pathsep.join(
                [*[str(p) for p in value], os.environ[key]]
            )
        else:
            os.environ[key] = os.pathsep.join(str(p) for p in value)
        super().__setitem__(key, value)

    def __delitem__(self, key: str) -> None:
        self._reset_var(key)
        super().__delitem__(key)


class Flags(Table["Flags.Entry"]):
    """A table that represents the [flags] section of an env.toml file.  Entries in the
    table are joined with spaces and prepended to the corresponding environment variable
    when modified.
    """

    class Entry(SepArray[str]):
        """Represents a single entry in the [flags] table."""

        def __init__(self, env: Environment, key: str, array: deque[str]) -> None:
            super().__init__(env, key, array, " ")

    def _reset_var(self, key: str) -> None:
        """Reset an environment variable to its original value.

        Parameters
        ----------
        key : str
            The key to reset.
        """
        if key in os.environ:
            components = os.environ[key].split(" ")
            value = self.table[key]
            new = [c for c in components if c not in value]
            os.environ[key] = " ".join(new)

    def clear(self) -> None:
        """Remove all items from the table and the environment."""
        for key in self.table:
            self._reset_var(key)
        super().clear()

    def pop(self, key: str, default: Entry | None = None) -> Entry | None:
        """Remove an item from the table and the environment and return its value.

        Parameters
        ----------
        key : str
            The key to remove from the table and the environment.
        default : Entry | None, optional
            The value to return if the key is not found, by default None.

        Returns
        -------
        Entry | None
            The value associated with the key, or the default value if the key is not
            found.
        """
        self._reset_var(key)
        return super().pop(key, default)

    def setdefault(self, key: str, default: Entry) -> Entry:
        """Set the value of a key if it is not already present in the table or the
        environment, and then return the value.

        Parameters
        ----------
        key : str
            The key to set the value of.
        default : Entry
            The value to set if the key is not already present.

        Returns
        -------
        Entry
            The value associated with the key.
        """
        if key not in self.table:
            if key in os.environ:
                os.environ[key] = " ".join([*default, os.environ[key]])
            else:
                os.environ[key] = " ".join(default)
        return super().setdefault(key, default)

    def update(self, other: dict[str, Entry]) -> None:
        """Update the table with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Entry]
            The dictionary to update the table with.
        """
        for key, value in other.items():
            if key in self.table:
                self._reset_var(key)
            if key in os.environ:
                os.environ[key] = " ".join([*value, os.environ[key]])
            else:
                os.environ[key] = " ".join(value)
        super().update(other)

    def __setitem__(self, key: str, value: Entry) -> None:
        if key in self.table:
            self._reset_var(key)
        if key in os.environ:
            os.environ[key] = " ".join([*value, os.environ[key]])
        else:
            os.environ[key] = " ".join(value)
        super().__setitem__(key, value)

    def __delitem__(self, key: str) -> None:
        self._reset_var(key)
        super().__delitem__(key)


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




# TODO: indexing into an environment should check os.environ.
# -> the / operator will compute from the root of the virtual environment.


class Environment:
    """A wrapper around an env.toml file that can be used to activate and deactivate
    the environment, as well as check or modify its state.
    """

    OLD_PREFIX = "_OLD_VIRTUAL_"
    venv: Path | None
    toml: Path | None
    info: Table[Any] | None
    vars: Vars | None
    paths: Paths | None
    flags: Flags | None
    packages: Packages | None

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


environment = Environment.__new__(Environment)










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



