"""Activate/deactivate the current environment and query its state."""
from __future__ import annotations

import os
from collections import deque
from collections.abc import KeysView, ValuesView, ItemsView
from pathlib import Path
from typing import (
    Any, Callable, Generic, Iterable, Iterator, SupportsIndex, TypeVar, overload
)

import tomlkit
from tomlkit.items import AbstractTable as tomlkit_AbstractTable
from tomlkit.items import AoT as tomlkit_AoT
from tomlkit.items import Array as tomlkit_Array

from .messages import YELLOW, CYAN, WHITE
from .package import Package


T = TypeVar("T")


class Array(Generic[T]):
    """A wrapper around a TOML array that is lazily loaded and written back to the
    env.toml file when modified.  Subclasses can add behavior for modifying the
    environment as needed.
    """

    def __init__(
        self,
        environment: Environment,
        getter: Callable[[tomlkit.TOMLDocument], list[T]],
        setter: Callable[[tomlkit.TOMLDocument, list[T]], None],
    ) -> None:
        self.environment = environment
        self.getter = getter
        self.setter = setter

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
        toml = self.environment.toml
        with toml.open("r") as f:
            return self.getter(tomlkit.load(f))

    @array.setter
    def array(self, value: list[T]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            self.setter(content, value)
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
        return self.array

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
        array.sort(key=key, reverse=reverse)  # type: ignore
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


class Table(Generic[T]):
    """A wrapper around the [info] table of an `env.toml` file.  This is lazily
    loaded and written back to the toml file when modified, so that everything
    stays in sync.
    """

    def __init__(
        self,
        environment: Environment,
        getter: Callable[[tomlkit.TOMLDocument], dict[str, T]],
        setter: Callable[[tomlkit.TOMLDocument, dict[str, T]], None],
    ) -> None:
        self.environment = environment
        self.getter = getter
        self.setter = setter

    @property
    def table(self) -> dict[str, T]:
        """Read the table from the env.toml file.

        Returns
        -------
        dict[str, T]
            An up-to-date copy of the table.

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
        toml = self.environment.toml
        with toml.open("r") as f:
            return self.getter(tomlkit.load(f))

    @table.setter
    def table(self, value: dict[str, T]) -> None:
        toml = self.environment.toml
        with toml.open("r+") as f:
            content = tomlkit.load(f)
            self.setter(content, value)
            f.seek(0)
            tomlkit.dump(content, f)
            f.truncate()

    @table.deleter
    def table(self) -> None:
        self.table = {}

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

    @overload
    def get(self, key: str, default: T) -> T: ...
    @overload
    def get(self, key: str, default: None = ...) -> T | None: ...
    def get(self, key: str, default: T | None = None) -> T | None:
        """Get an item from the table or a default value if the key is not found.

        Parameters
        ----------
        key : str
            The key to look up.
        default : T, optional
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
        return self.table.get(key, default)

    @overload
    def pop(self, key: str, default: T) -> T: ...
    @overload
    def pop(self, key: str, default: None = ...) -> T | None: ...
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


class Environment:
    """A wrapper around an env.toml file that can be used to activate and deactivate
    the environment, as well as check or modify its state.
    """

    OLD_PREFIX = "_OLD_VIRTUAL_"

    def __init__(self) -> None:
        self._info: Environment.Info
        self._vars: Environment.Vars
        self._paths: Environment.Paths
        self._flags: Environment.Flags
        self._packages: Environment.Packages
        raise NotImplementedError(
            "Environment should be used as a global object, not instantiated directly."
        )

    def __new__(cls) -> Environment:
        self = super().__new__(cls)
        self._info = self.Info(self)
        self._vars = self.Vars(self)
        self._paths = self.Paths(self)
        self._flags = self.Flags(self)
        self._packages = self.Packages(self)
        return self

    def activate(self, venv: Path) -> list[str]:
        """Enter the environment from a Python process and return the sequence of bash
        commands necessary to activate it from the command line.

        Parameters
        ----------
        venv : Path
            The path to the environment directory to activate.  Must contain an
            `env.toml` file that conforms to this specification.

        Returns
        -------
        list[str]
            A list of bash commands that will set the required environment variables when
            sourced from the command line.

        Raises
        ------
        FileNotFoundError
            If no env.toml file was found.
        TypeError
            If the env.toml file is not formatted correctly.

        Notes
        -----
        This method is called by the `$ bertrand activate venv` shell command, which is
        in turn called by the virtual environment's `activate` script to enter the
        environment.  Each command will be executed verbatim when the activation script
        is sourced, allowing the environment to be attached to the shell itself.
        """
        os.environ["BERTRAND_HOME"] = str(venv)
        commands = []

        # [vars] get exported directly
        for key, value in self.vars.items():
            if key in os.environ:
                k = f"{self.OLD_PREFIX}{key}"
                v = os.environ[key]
                os.environ[k] = v
                commands.append(f"export {k}=\"{v}\"")
            os.environ[key] = value
            commands.append(f"export {key}=\"{value}\"")

        # [paths] get prepended to existing paths
        for key, paths in self.paths.items():
            if key in os.environ:
                k = f"{self.OLD_PREFIX}{key}"
                v = os.environ[key]
                os.environ[k] = v
                commands.append(f"export {k}=\"{v}\"")
                fragment = os.pathsep.join([*(str(p) for p in paths), v])
            else:
                fragment = os.pathsep.join(str(p) for p in paths)
            if fragment:
                os.environ[key] = fragment
                commands.append(f'export {key}=\"{fragment}\"')

        # [flags] get appended to existing flags
        for key, flaglist in self.flags.items():
            if key in os.environ:
                k = f"{self.OLD_PREFIX}{key}"
                v = os.environ[key]
                os.environ[k] = v
                commands.append(f"export {k}=\"{v}\"")
                fragment = " ".join([v, *flaglist])
            else:
                fragment = " ".join(flaglist)
            if fragment:
                os.environ[key] = fragment
                commands.append(f'export {key}=\"{fragment}\"')

        return commands

    def deactivate(self) -> list[str]:
        """Exit the environment from a Python process and return the sequence of bash
        commands necessary to deactivate it from the command line.

        Returns
        -------
        list[str]
            A list of bash commands that will restore the previous environment
            variables when sourced.

        Notes
        -----
        When the `activate()` method is called, the environment variables are saved
        with a special prefix to prevent conflicts.  This method undoes that by
        transferring the value of the prefixed variable back to the original, and then
        clearing the temporary variable.

        If the variable did not exist before the environment was activated, this method
        will clear it without replacement.
        """
        commands = []

        for key in list(self.flags) + list(self.paths) + list(self.vars):
            old = f"{self.OLD_PREFIX}{key}"
            if old in os.environ:
                os.environ[key] = os.environ[old]
                commands.append(f"export {key}=\"{os.environ[old]}\"")
                os.environ.pop(old)
                commands.append(f"unset {old}")
            else:
                os.environ.pop(key, None)
                commands.append(f"unset {key}")

        os.environ.pop("BERTRAND_HOME", None)
        return commands

    # TODO: init()?

    @property
    def dir(self) -> Path:
        """The path to the directory containing the env.toml file for the current
        virtual environment.

        Returns
        -------
        Path
            A path to the environment's directory.

        Raises
        ------
        RuntimeError
            If no environment is currently active.
        """
        if "BERTRAND_HOME" not in os.environ:
            raise RuntimeError("no environment is currently active.")
        return Path(os.environ["BERTRAND_HOME"])

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
        """
        if "BERTRAND_HOME" not in os.environ:
            raise RuntimeError("no environment is currently active.")
        return Path(os.environ["BERTRAND_HOME"]) / "env.toml"

    class Info(Table[Any]):
        """The [info] table from the env.toml file."""

        def __init__(self, environment: Environment) -> None:
            super().__init__(environment, self._getter, self._setter)

        def _getter(self, content: tomlkit.TOMLDocument) -> dict[str, Any]:
            if "info" not in content:
                raise KeyError(
                    f"no {YELLOW}[info]{WHITE} table in file: "
                    f"{CYAN}{self.environment.toml}{WHITE}"
                )
            table = content["info"]
            if not isinstance(table, tomlkit_AbstractTable):
                raise TypeError(
                    f"{YELLOW}[info]{WHITE} must be a table, not "
                    f"{CYAN}{type(table)}{WHITE}"
                )
            return dict(table)

        def _setter(self, content: tomlkit.TOMLDocument, value: dict[str, Any]) -> None:
            content["info"] = value

    @property
    def info(self) -> Info:
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

    class Vars(Table[str]):
        """The [vars] table from the env.toml file."""

        def __init__(self, environment: Environment) -> None:
            super().__init__(environment, self._getter, self._setter)

        def _getter(self, content: tomlkit.TOMLDocument) -> dict[str, str]:
            if "vars" not in content:
                raise KeyError(
                    f"no {YELLOW}[vars]{WHITE} table in file: "
                    f"{CYAN}{self.environment.toml}{WHITE}"
                )
            table = content["vars"]
            if not isinstance(table, tomlkit_AbstractTable):
                raise TypeError(
                    f"{YELLOW}[vars]{WHITE} must be a table, not "
                    f"{CYAN}{type(table)}{WHITE}"
                )
            return dict(table)

        def _setter(self, content: tomlkit.TOMLDocument, value: dict[str, str]) -> None:
            for k, v in content.get("vars", {}).items():
                if k not in value:
                    os.environ.pop(k, None)
            for k, v in value.items():
                os.environ[k] = v
            content["vars"] = value

    @property
    def vars(self) -> Table[str]:
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

    class Paths(Table["Paths.Entry | list[Path]"]):  # type: ignore
        """A table that represents the [paths] section of an env.toml file.  Entries in
        the table are joined with the system's path separator and prepended to the
        corresponding environment variable when modified.
        """

        def __init__(self, environment: Environment) -> None:
            super().__init__(environment, self._getter, self._setter)

        def _getter(self, content: tomlkit.TOMLDocument) -> dict[str, Entry | list[Path]]:
            if "paths" not in content:
                raise KeyError(
                    f"no {YELLOW}[paths]{WHITE} table in file: "
                    f"{CYAN}{self.environment.toml}{WHITE}"
                )
            table = content["paths"]
            if not isinstance(table, tomlkit_AbstractTable):
                raise TypeError(
                    f"{YELLOW}[paths]{WHITE} must be a table, not "
                    f"{CYAN}{type(table)}{WHITE}"
                )
            return {k: Environment.Paths.Entry(self, k) for k in table}

        def _setter(
            self,
            content: tomlkit.TOMLDocument,
            value: dict[str, Entry | list[Path]]
        ) -> None:
            for k, v in content.get("paths", {}).items():
                if k in os.environ:
                    os.environ[k] = os.pathsep.join(
                        p for p in os.environ[k].split(os.pathsep) if Path(p) not in v
                    )

            for k, v in value.items():
                if k in os.environ:
                    os.environ[k] = os.pathsep.join([*(str(p) for p in v), os.environ[k]])
                else:
                    os.environ[k] = os.pathsep.join(str(p) for p in v)

            content["paths"] = {k: [str(p) for p in v] for k, v in value.items()}

        class Entry(Array[Path]):
            """Represents a single value in the [paths] table."""

            def __init__(self, paths: Environment.Paths, key: str) -> None:
                super().__init__(paths.environment, self._getter, self._setter)
                self.cache: list[Path] = []
                self.paths = paths
                self.key = key

            def _getter(self, content: tomlkit.TOMLDocument) -> list[Path]:
                if "paths" not in content:
                    raise KeyError(
                        f"no [paths] table in file: {self.paths.environment.toml}"
                    )
                table = content["paths"]
                if not isinstance(table, tomlkit_AbstractTable):
                    raise TypeError(f"[paths] must be a table, not {type(table)}")

                if self.key not in table:
                    raise KeyError(f"no '{self.key}' entry in [paths] table")
                array = table[self.key]
                if not isinstance(array, tomlkit_Array):
                    raise TypeError(
                        f"[paths.{self.key}] must be an array, not {type(array)}"
                    )
                if not all(isinstance(p, str) for p in array):
                    raise TypeError(
                        f"[paths.{self.key}] must be an array of pathlike strings"
                    )

                self.cache = [Path(p) for p in array]
                return [p for p in self.cache]

            def _setter(self, content: tomlkit.TOMLDocument, value: list[Path]) -> None:
                if self.key in os.environ:
                    os.environ[self.key] = os.pathsep.join(
                        p for p in os.environ[self.key].split(os.pathsep)
                        if Path(p) not in self.cache
                    )
                    os.environ[self.key] = os.pathsep.join(
                        [*(str(p) for p in value), os.environ[self.key]]
                    )
                else:
                    os.environ[self.key] = os.pathsep.join(str(p) for p in value)

                self.cache = value
                content.setdefault("paths", {})[self.key] = [str(p) for p in value]

        @overload  # type: ignore
        def get(self, key: str, default: list[Path]) -> list[Path]: ...
        @overload
        def get(self, key: str, default: None = ...) -> list[Path] | None: ...
        def get(self, key: str, default: list[Path] | None = None) -> list[Path] | None:
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
            table = self.table
            if key in table:
                return list(table[key])
            return default

        @overload  # type: ignore
        def pop(self, key: str, default: list[Path]) -> list[Path]: ...
        @overload
        def pop(self, key: str, default: None = ...) -> list[Path] | None: ...
        def pop(self, key: str, default: list[Path] | None = None) -> list[Path] | None:
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
                result = list(table[key])
                del table[key]
                self.table = table
                return result
            return default

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
            table.update(other)
            self.table = table

        def __getitem__(self, key: str) -> Entry:
            return self.Entry(self, key)  # lazily evaluated

        def __setitem__(self, key: str, value: list[Path]) -> None:  # type: ignore
            table = self.table
            table[key] = value
            self.table = table

        def __or__(self, other: dict[str, list[Path]]) -> dict[str, list[Path]]:  # type: ignore
            return {k: list(v) for k, v in self.table.items()} | other

        def __ior__(self, other: dict[str, list[Path]]) -> Environment.Paths:  # type: ignore
            self.update(other)  # type: ignore
            return self

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
        return self._paths

    @paths.setter
    def paths(self, value: dict[str, list[Path]]) -> None:
        self._paths.clear()
        self._paths.update(value)

    @paths.deleter
    def paths(self) -> None:
        self._paths.clear()

    class Flags(Table["Flags.Entry | list[str]"]):  # type: ignore
        """A table that represents the [flags] section of an env.toml file.  Entries in
        the table are joined with spaces and appended to the corresponding environment
        variable when modified.
        """

        def __init__(self, environment: Environment) -> None:
            super().__init__(environment, self._getter, self._setter)

        def _getter(self, content: tomlkit.TOMLDocument) -> dict[str, Entry | list[str]]:
            if "flags" not in content:
                raise KeyError(
                    f"no {YELLOW}[flags]{WHITE} table in file: "
                    f"{CYAN}{self.environment.toml}{WHITE}"
                )
            table = content["flags"]
            if not isinstance(table, tomlkit_AbstractTable):
                raise TypeError(
                    f"{YELLOW}[flags]{WHITE} must be a table, not "
                    f"{CYAN}{type(table)}{WHITE}"
                )
            return {k: Environment.Flags.Entry(self, k) for k in table}

        def _setter(
            self,
            content: tomlkit.TOMLDocument,
            value: dict[str, Entry | list[str]]
        ) -> None:
            for k, v in content.get("flags", {}).items():
                if k in os.environ:
                    os.environ[k] = " ".join(
                        f for f in os.environ[k].split(" ") if f not in v
                    )

            for k, v in value.items():
                if k in os.environ:
                    os.environ[k] = " ".join([os.environ[k], *v])
                else:
                    os.environ[k] = " ".join(v)

            content["flags"] = {k: list(v) for k, v in value.items()}

        class Entry(Array[str]):
            """Represents a single value in the [flags] table."""

            def __init__(self, flags: Environment.Flags, key: str) -> None:
                super().__init__(flags.environment, self._getter, self._setter)
                self.cache: list[str] = []
                self.flags = flags
                self.key = key

            def _getter(self, content: tomlkit.TOMLDocument) -> list[str]:
                if "flags" not in content:
                    raise KeyError(
                        f"no [flags] table in file: {self.flags.environment.toml}"
                    )
                table = content["flags"]
                if not isinstance(table, tomlkit_AbstractTable):
                    raise TypeError(f"[flags] must be a table, not {type(table)}")

                if self.key not in table:
                    raise KeyError(f"no '{self.key}' entry in [flags] table")
                array = table[self.key]
                if not isinstance(array, tomlkit_Array):
                    raise TypeError(
                        f"[flags.{self.key}] must be an array, not {type(array)}"
                    )
                if not all(isinstance(p, str) for p in array):
                    raise TypeError(
                        f"[flags.{self.key}] must be an array of strings"
                    )

                self.cache = list(array)
                return self.cache.copy()

            def _setter(self, content: tomlkit.TOMLDocument, value: list[str]) -> None:
                if self.key in os.environ:
                    os.environ[self.key] = " ".join(
                        f for f in os.environ[self.key].split(" ")
                        if f not in self.cache
                    )
                    os.environ[self.key] = " ".join(
                        [os.environ[self.key], *value]
                    )
                else:
                    os.environ[self.key] = " ".join(value)

                self.cache = value
                content.setdefault("flags", {})[self.key] = value

        @overload  # type: ignore
        def get(self, key: str, default: list[str]) -> list[str]: ...
        @overload
        def get(self, key: str, default: None = ...) -> list[str] | None: ...
        def get(self, key: str, default: list[str] | None = None) -> list[str] | None:
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
                return list(table[key])
            return default

        @overload  # type: ignore
        def pop(self, key: str, default: list[str]) -> list[str]: ...
        @overload
        def pop(self, key: str, default: None = ...) -> list[str] | None: ...
        def pop(self, key: str, default: list[str] | None = None) -> list[str] | None:
            """Remove an item from the table and return its value.

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
                result = list(table[key])
                del table[key]
                self.table = table
                return result
            return default

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
            table.update(other)
            self.table = table

        def __getitem__(self, key: str) -> Entry:
            return self.Entry(self, key)  # lazily evaluated

        def __setitem__(self, key: str, value: list[str]) -> None:  # type: ignore
            table = self.table
            table[key] = value
            self.table = table

        def __or__(self, other: dict[str, list[str]]) -> dict[str, list[str]]:  # type: ignore
            return {k: list(v) for k, v in self.table.items()} | other

        def __ior__(self, other: dict[str, list[str]]) -> Environment.Flags:  # type: ignore
            self.update(other)  # type: ignore
            return self

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
        return self._flags

    @flags.setter
    def flags(self, value: dict[str, list[str]]) -> None:
        self._flags.clear()
        self._flags.update(value)

    @flags.deleter
    def flags(self) -> None:
        self._flags.clear()

    class Packages(Array[Package]):
        """An array of immutable tables that represent the [packages] section of an
        env.toml file.  Each table has the following fields:

            - name: The name of the package.
            - version: The installed version.
            - find: The symbol to pass to CMake's `find_package()` function in order to
                locate the package's headers and libraries.
            - link: The symbol to pass to CMake's `target_link_libraries()` function in
                order to link against the package's libraries.

        There is no environment variable associated with this section, and it is
        automatically updated whenever a package is installed or removed from the
        virtual environment.
        """

        def __init__(self, environment: Environment) -> None:
            super().__init__(environment, self._getter, self._setter)

        def _getter(self, content: tomlkit.TOMLDocument) -> list[Package]:
            if "packages" not in content:
                return []

            array = content["packages"]
            if not isinstance(array, tomlkit_AoT):
                raise TypeError(
                    f"{YELLOW}[[packages]]{WHITE} must be an array of tables, not "
                    f"{CYAN}{type(array)}{WHITE}"
                )

            return [Package.from_dict(d) for d in array]

        def _setter(self, content: tomlkit.TOMLDocument, value: list[Package]) -> None:
            observed: set[Package] = set()
            duplicates: list[Package] = []
            for entry in value:
                if entry in observed:
                    duplicates.append(entry)
                elif entry.shorthand:
                    raise ValueError(
                        f"package must define `find` and `link` symbols: {entry}"
                    )
                else:
                    observed.add(entry)
            if duplicates:
                raise ValueError(f"package(s) already installed: {duplicates}")

            if value:
                content["packages"] = [p.to_dict() for p in value]
            else:
                content.pop("packages", None)

    @property
    def packages(self) -> Packages:
        """The [[packages]] array from the env.toml file.

        Returns
        -------
        Packages
            A list-like object representing the [[packages]] array in the env.toml file.
            Each item in the list is an immutable dict-like object with the fields
            'name', 'version', 'find', and 'link'.  Mutating the array will
            automatically update the env.toml file with the new values.

        Raises
        ------
        RuntimeError
            If no environment is active.
        FileNotFoundError
            If the environment file does not exist.
        KeyError
            If there is no [[packages]] array in the environment file.
        TypeError
            If the [[packages]] array is not an array of tables.
        """
        return self._packages

    @packages.setter
    def packages(self, value: list[Package]) -> None:
        self._packages.clear()
        self._packages.extend(value)

    @packages.deleter
    def packages(self) -> None:
        self._packages.clear()

    @property
    def table(self) -> dict[str, Any]:
        """A dictionary containing the current contents of the env.toml file.

        Returns
        -------
        dict[str, Any]
            An up-to-date dictionary representation of the env.toml file.
        """
        return {
            "info": self.info.table,
            "vars": self.vars.table,
            "paths": self.paths.table,
            "flags": self.flags.table,
            "packages": self.packages.array,
        }

    @table.setter
    def table(self, value: dict[str, Any]) -> None:
        """Update the env.toml file with the contents of a dictionary.

        Parameters
        ----------
        value : dict[str, Any]
            The dictionary to write to the env.toml file.

        Raises
        ------
        KeyError
            If the dictionary contains keys that are not valid for the env.toml file.
        """
        expected = {"info", "vars", "paths", "flags", "packages"}
        new: dict[str, Any] = {}
        for k, v in value.items():
            if k not in expected:
                raise KeyError(k)
            new[k] = v

        self.info = new.get("info", {})
        self.vars = new.get("vars", {})
        self.paths = new.get("paths", {})
        self.flags = new.get("flags", {})
        self.packages = new.get("packages", [])

    @table.deleter
    def table(self) -> None:
        self.info.clear()
        self.vars.clear()
        self.paths.clear()
        self.flags.clear()
        self.packages.clear()

    def keys(self) -> KeysView[str]:
        """
        Returns
        -------
        KeysView[str]
            The keys of the environment.
        """
        return self.table.keys()

    def values(self) -> ValuesView[Any]:
        """
        Returns
        -------
        ValuesView[str]
            The values of the environment.
        """
        return self.table.values()

    def items(self) -> ItemsView[str, Any]:
        """
        Returns
        -------
        ItemsView[str, str]
            The items of the environment.
        """
        return self.table.items()

    def clear(self) -> None:
        """Clear the environment."""
        self.info.clear()
        self.vars.clear()
        self.paths.clear()
        self.flags.clear()
        self.packages.clear()

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
        return self.table.get(key, default)

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
        if key == "info":
            value = self.info.table
            self.info.clear()
            return value
        if key == "vars":
            value = self.vars.table
            self.vars.clear()
            return value
        if key == "paths":
            value = self.paths.table
            self.paths.clear()
            return value
        if key == "flags":
            value = self.flags.table
            self.flags.clear()
            return value
        if key == "packages":
            value = self.packages.array  # type: ignore
            self.packages.clear()
            return value

        return default

    def update(self, other: dict[str, Any]) -> None:
        """Update the environment with the contents of another dictionary.

        Parameters
        ----------
        other : dict[str, Any]
            The dictionary to update the environment with.
        """
        self.info.update(other.get("info", {}))
        self.vars.update(other.get("vars", {}))
        self.paths.update(other.get("paths", {}))
        self.flags.update(other.get("flags", {}))
        self.packages.extend(other.get("packages", []))

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
        if key == "info":
            return self.info
        if key == "vars":
            return self.vars
        if key == "paths":
            return self.paths
        if key == "flags":
            return self.flags
        if key == "packages":
            return self.packages
        raise KeyError(key)

    def __setitem__(self, key: str, value: Any) -> None:
        if key == "info":
            self.info = value
        elif key == "vars":
            self.vars = value
        elif key == "paths":
            self.paths = value
        elif key == "flags":
            self.flags = value
        elif key == "packages":
            self.packages = value
        else:
            raise KeyError(key)

    def __delitem__(self, key: str) -> None:
        if key == "info":
            del self.info
        elif key == "vars":
            del self.vars
        elif key == "paths":
            del self.paths
        elif key == "flags":
            del self.flags
        elif key == "packages":
            del self.packages
        else:
            raise KeyError(key)

    def __contains__(self, key: str) -> bool:
        return key in {"info", "vars", "paths", "flags", "packages"}

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Environment):
            return self.table == other.table
        return False

    def __ne__(self, other: Any) -> bool:
        return not self == other

    def __or__(self, other: dict[str, Any]) -> dict[str, Any]:
        return self.table | other

    def __ior__(self, other: dict[str, Any]) -> Environment:
        self.update(other)
        return self

    def __str__(self) -> str:
        return repr(self)

    def __repr__(self) -> str:
        if not self:
            return "<Environment: headless>"
        if not self.toml.exists():
            return f"<(uninitialized) Environment: {self.toml}>"
        return f"<Environment: {self.toml}>"

    def __truediv__(self, key: Path | str) -> Path:
        """Navigate from the root of the virtual environment's directory."""
        if not self:
            raise RuntimeError("No environment is currently active.")
        return Path(os.environ["BERTRAND_HOME"]) / key


env = Environment.__new__(Environment)
