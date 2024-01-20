"""This module defines the core of the bertrand type system, including the metaclass
machinery, type primitives, and helper functions for detecting and resolving types.
"""
from __future__ import annotations
import collections
import inspect
from types import MappingProxyType, UnionType
from typing import (
    Any, Callable, ItemsView, Iterable, Iterator, KeysView, Mapping, NoReturn,
    TypeAlias, TypeVar, ValuesView
)
from typing import Union as PyUnion

import numpy as np
import pandas as pd
import regex as re  # alternate (PCRE-style) regex engine

from bertrand import LinkedSet, LinkedDict


# NOTE: this module is kind of huge, and would ordinarily be split into multiple files.
# However, due to the depth and complexity of metaprogramming, doing so would introduce
# a large number of circular dependencies, and would make the code much harder to
# read and maintain.  Additionally, the metaclass machinery is sensitive to the order
# in which classes are defined, and much of the length is due to inline documentation
# rather than actual code, so splitting it up would not necessarily make it any easier
# to navigate.  As such, I have opted to keep everything in one place for simplicity,
# and to copy docs wherever possible to compress the overall size.


# TODO: occasionally get a hangup when building registry.  This is probably due to
# some circular relationship in Linked structs.  Need to investigate, but hard to
# reproduce.


# pylint: disable=unused-argument, using-constant-test


class Empty:
    """Placeholder for required fields of an abstract type.

    A singleton instance of this class is exposed as a global ``EMPTY`` object.
    If an attribute of an abstract type is assigned to this object, then it will
    be marked as required in all concretions of that type.

    The global ``EMPTY`` object can be called to inject a validation function
    into the metaclass machinery, which will automatically be invoked on any
    value that is assigned to the attribute.  If no value is assigned for a
    particular concretion, the validator will be called with the global ``EMPTY``
    object as its argument, and can either return a default value or raise an
    error.  If no validator is supplied, the ``EMPTY`` object defaults to an
    identity function, which raises an error if the value is missing.
    """

    def __init__(
        self,
        func: Callable[[Any | Empty, dict[str, Any], dict[str, Any]], Any]
    ) -> None:
        self.func = func

    def __call__(
        self,
        func: Callable[[Any | Empty, dict[str, Any], dict[str, Any]], Any]
    ) -> Empty:
        return Empty(func)

    def __bool__(self) -> bool:
        return False

    def __str__(self) -> str:
        return "..."

    def __repr__(self) -> str:
        return "..."


def _identity(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> Any:
    """Simple identity function used to validate required arguments that do not
    have any type hints.
    """
    if value is EMPTY:
        raise TypeError("missing required field")
    return value


EMPTY: Empty = Empty(_identity)
POINTER_SIZE: int = np.dtype(np.intp).itemsize
META: TypeAlias = "TypeMeta | DecoratorMeta | UnionMeta | StructuredMeta"
DTYPE: TypeAlias = np.dtype[Any] | pd.api.extensions.ExtensionDtype
ALIAS: TypeAlias = str | type | DTYPE
TYPESPEC: TypeAlias = "META | ALIAS"
OBJECT_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[np.object_]]
RLE_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[tuple[np.object_, np.intp]]]


DEBUG: bool = False  # if True, print a debug message whenever a new type is created


########################
####    REGISTRY    ####
########################


class DecoratorPrecedence:
    """A linked set representing the priority given to each decorator in the type
    system.

    This interface is used to determine the order in which decorators are nested when
    applied to the same type.  For instance, if the precedence is set to `{A, B, C}`,
    and each is applied to a type `T`, then the resulting type will always be
    `A[B[C[T]]]`, no matter what order the decorators are applied in.  To demonstrate:

    .. code-block:: python

        >>> Type.registry.decorators
        DecoratorPrecedence({D1, D2, D3})
        >>> T
        T
        >>> D2[T]
        D2[T]
        >>> D3[D2[T]]
        D2[D3[T]]
        >>> D2[D3[D1[T]]]
        D1[D2[D3[T]]]

    At each step, the decorator that is furthest to the right in precedence is applied
    first, and the decorator furthest to the left is applied last.  We can modify this
    by changing the precedence order:

    .. code-block:: python

        >>> Type.registry.decorators.move_to_index(A, -1)
        >>> Type.registry.decorators
        DecoratorPrecedence({B, C, A})
        >>> A[B[C[T]]]
        B[C[A[T]]]

    As such, we can enforce a strict order for nested decorators, without requiring the
    user to specifically construct types in that order.  Whenever a decorator is
    applied to a previously-decorated type, we iterate through the decorator stack and
    check whether the new decorator is higher or lower in the precedence order.  We
    then insert it into the stack at the appropriate position, and re-apply any
    decorators that were higher in the stack.  The user does not need to account for
    this in practice, since it is handled automatically by the type system.
    """

    def __init__(self) -> None:
        self._set = LinkedSet()

    def index(self, decorator: DecoratorMeta) -> int:
        """Get the index of a decorator in the precedence order.

        Parameters
        ----------
        decorator : DecoratorMeta
            The decorator type to search for.

        Returns
        -------
        int
            The index of the decorator in the precedence order.

        Raises
        ------
        KeyError
            If the decorator is not in the precedence order.
        """
        return self._set.index(decorator)

    def distance(self, decorator1: DecoratorMeta, decorator2: DecoratorMeta) -> int:
        """Get the linear distance between two decorators in the precedence order.

        Parameters
        ----------
        decorator1 : DecoratorMeta
            The first decorator to search for.  This is used as the origin.
        decorator2 : DecoratorMeta
            The second decorator to search for.  This is the destination.

        Returns
        -------
        int
            The difference between the index of the first decorator and the index of
            the second decorator.  This will be positive if the first decorator is
            higher in the precedence order and negative if it is lower.  If the
            decorators are the same, then the distance will be zero.

        Raises
        ------
        KeyError
            If either decorator is not in the precedence order.
        """
        return self._set.distance(decorator1, decorator2)

    def swap(self, decorator1: DecoratorMeta, decorator2: DecoratorMeta) -> None:
        """Swap the positions of two decorators in the precedence order.

        Parameters
        ----------
        decorator1 : DecoratorMeta
            The first decorator to swap.
        decorator2 : DecoratorMeta
            The second decorator to swap.

        Raises
        ------
        KeyError
            If either decorator is not in the precedence order.
        """
        self._set.swap(decorator1, decorator2)

    def move(self, decorator: DecoratorMeta, shift: int) -> None:
        """Move a decorator in the precedence order relative to its current position.

        Parameters
        ----------
        decorator : DecoratorMeta
            The decorator to move.
        shift : int
            The number of positions to move the decorator.  Positive values move the
            decorator to the right, and negative values move it to the left, truncated
            to the bounds of the container.

        Raises
        ------
        KeyError
            If the decorator is not in the precedence order.
        """
        self._set.move(decorator, shift)

    def move_to_index(self, decorator: DecoratorMeta, index: int) -> None:
        """Move a decorator to the specified index in the precedence order.

        Parameters
        ----------
        decorator : DecoratorMeta
            The decorator to move.
        index : int
            The index to move the decorator to, truncated to the bounds of the
            container.  This supports negative indexing, which counts from the end of
            the precedence order.

        Raises
        ------
        KeyError
            If the decorator is not in the precedence order.
        """
        self._set.move_to_index(decorator, index)

    def __contains__(self, decorator: DecoratorMeta) -> bool:
        return decorator in self._set

    def __len__(self) -> int:
        return len(self._set)

    def __iter__(self) -> Iterator[DecoratorMeta]:
        return iter(self._set)

    def __reversed__(self) -> Iterator[DecoratorMeta]:
        return reversed(self._set)

    def __getitem__(self, index: int | slice) -> DecoratorMeta | LinkedSet[DecoratorMeta]:
        return self._set[index]

    def __str__(self) -> str:
        return f"{', '.join(repr(d) for d in self)}"

    def __repr__(self) -> str:
        return f"DecoratorPrecedence({', '.join(repr(d) for d in self)})"


class Edges:
    """A set of edges representing overrides for a type's less-than and greater-than
    operators.
    """

    def __init__(self) -> None:
        self._set: set[tuple[TypeMeta, TypeMeta]] = set()

    def add(self, typ1: TYPESPEC, typ2: TYPESPEC) -> None:
        """Add an edge to the set.

        Parameters
        ----------
        typ1 : TYPESPEC
            A specifier for the first type in the edge.  This will always be considered
            to be less than the second type.
        typ2 : TYPESPEC
            A specifier for the second type in the edge.  This will always be considered
            greater than the first type.

        Raises
        ------
        TypeError
            If either type is not an unparametrized scalar type.
        """
        t1 = resolve(typ1)
        t2 = resolve(typ2)

        if not (isinstance(t1, TypeMeta) and isinstance(t2, TypeMeta)):
            raise TypeError(
                f"edges can only be drawn between scalar types, not {repr(t1)} and "
                f"{repr(t2)}"
            )

        if t1.is_flyweight or t2.is_flyweight:
            raise TypeError(
                f"edges can only be drawn between unparametrized types, not "
                f"{repr(t1)} and {repr(t2)}"
            )

        self._set.add((t1, t2))

    def update(self, edges: Iterable[tuple[TYPESPEC, TYPESPEC]]) -> None:
        """Add multiple edges to the set.

        Parameters
        ----------
        edges : Iterable[tuple[TYPESPEC, TYPESPEC]]
            An iterable of edges to add to the set.

        Raises
        ------
        TypeError
            If any of the edges are not between unparametrized scalar types.
        """
        for typ1, typ2 in edges:
            self.add(typ1, typ2)

    def clear(self) -> None:
        """Remove all edges from the set, resetting comparisons to their defaults."""
        self._set.clear()

    def remove(self, typ1: TYPESPEC, typ2: TYPESPEC) -> None:
        """Remove an edge from the set.

        Parameters
        ----------
        typ1 : TYPESPEC
            A specifier for the first type in the edge.
        typ2 : TYPESPEC
            A specifier for the second type in the edge.

        Raises
        ------
        KeyError
            If the edge is not in the set.
        """
        t1 = resolve(typ1)
        t2 = resolve(typ2)
        self._set.remove((t1, t2))  # type: ignore

    def discard(self, typ1: TYPESPEC, typ2: TYPESPEC) -> None:
        """Remove an edge from the set if it is present.

        Parameters
        ----------
        typ1 : TYPESPEC
            A specifier for the first type in the edge.
        typ2 : TYPESPEC
            A specifier for the second type in the edge.
        """
        t1 = resolve(typ1)
        t2 = resolve(typ2)
        self._set.discard((t1, t2))  # type: ignore

    def pop(self) -> tuple[TYPESPEC, TYPESPEC]:
        """Pop an edge from the set.

        Returns
        -------
        tuple[TYPESPEC, TYPESPEC]
            The popped edge.

        Raises
        ------
        KeyError
            If the set is empty.
        """
        return self._set.pop()

    __hash__ = None  # type: ignore

    def __contains__(self, edge: tuple[TYPESPEC, TYPESPEC]) -> bool:
        if len(edge) != 2:
            return False  # type: ignore
        t1 = resolve(edge[0])
        t2 = resolve(edge[1])
        return (t1, t2) in self._set

    def __len__(self) -> int:
        return len(self._set)

    def __iter__(self) -> Iterator[tuple[TypeMeta, TypeMeta]]:
        return iter(self._set)

    def __reversed__(self) -> Iterator[tuple[TypeMeta, TypeMeta]]:
        # pylint: disable=bad-reversed-sequence
        return reversed(self._set)  # type: ignore

    def __str__(self) -> str:
        return f"{{{', '.join(f'{repr(t1)} < {repr(t2)}' for t1, t2 in self)}}}"

    def __repr__(self) -> str:
        return f"Edges({', '.join(f'{repr(t1)} < {repr(t2)}' for t1, t2 in self)})"


class TypeRegistry:
    """A registry containing the global state of the bertrand type system.
    
    This is automatically populated by the metaclass machinery, and is used by the
    detect() and resolve() helper functions to link aliases to their corresponding
    types.  It also provides a convenient way to interact with the type system as a
    whole, for instance to attach() or detach() it from pandas, or list all the
    types that are currently defined.
    """

    # types are recorded in the same order as they are defined
    _types: LinkedSet[TypeMeta | DecoratorMeta] = LinkedSet()
    _precedence: DecoratorPrecedence = DecoratorPrecedence()
    _edges: Edges = Edges()

    # aliases are stored in separate dictionaries for performance reasons
    types: dict[type, TypeMeta | DecoratorMeta] = {}
    dtypes: dict[type, TypeMeta | DecoratorMeta] = {}
    strings: dict[str, TypeMeta | DecoratorMeta] = {}

    def __init__(self) -> None:
        self._regex: None | re.Pattern[str] = None
        self._column: None | re.Pattern[str] = None
        self._resolvable: None | re.Pattern[str] = None
        self._na_strings: None | dict[str, Any] = None

    #######################
    ####    ALIASES    ####
    #######################

    @property
    def aliases(self) -> dict[type | str, TypeMeta | DecoratorMeta]:
        """Get a dictionary mapping aliases to their corresponding types.

        Returns
        -------
        dict[type | str, TypeMeta | DecoratorMeta]
            A dictionary with aliases as keys and unparametrized types as values.

        Notes
        -----
        Each category of alias is stored in its own separate dictionary for
        performance reasons.  This attribute unifies them into a single mapping,
        although changes to the resulting dictionary will not be reflected in the
        registry.
        """
        return self.types | self.dtypes | self.strings

    @property
    def regex(self) -> re.Pattern[str]:
        """Get a regular expression to match a registered string alias plus any
        optional arguments that follow it.

        Returns
        -------
        regex.Pattern[str]
            A regular expression that matches a single alias from the registry,
            followed by any number of comma-separated arguments enclosed in square
            brackets.  The alias is captured in a group named `alias`, and the
            arguments are captured in a group named `args`.
        """
        if self._regex is None:
            if not self.strings:
                pattern = r".^"  # matches nothing
            else:
                escaped = [re.escape(alias) for alias in self.strings]
                escaped.sort(key=len, reverse=True)  # avoids partial matches
                escaped.append(r"(?P<sized_unicode>U(?P<size>[0-9]*))$")  # U32, U...
                pattern = (
                    rf"(?P<alias>{'|'.join(escaped)})"  # alias
                    rf"(?P<params>\[(?P<args>([^\[\]]|(?&params))*)\])?"  # recursive args
                )

            self._regex = re.compile(pattern)

        return self._regex

    @property
    def column(self) -> re.Pattern[str]:
        """Get a regular expression to match an optional column name plus one or
        more type specifiers.

        Returns
        -------
        regex.Pattern[str]
            A regular expression that matches an optional column name in slice
            syntax, followed by any number of pipe-separated type specifiers
            (`column: type | ...`).  The column name is captured in a group
            named `column`, and the type specifiers are captured in a group
            named `type`.
        """
        if self._column is None:
            # match optional column name: any number of | separated types
            self._column = re.compile(
                rf"((?P<column>[^\(\)\[\]:,]+)\s*:\s*)?"  
                rf"(?P<type>{self.regex.pattern}(\s*[|]\s*(?&type))*)"
            )
        return self._column

    @property
    def resolvable(self) -> re.Pattern[str]:
        """Get a regular expression to match any string that can be resolved
        into a valid type.

        Returns
        -------
        regex.Pattern[str]
            A regular expression that matches any string that is composed
            entirely by comma-separated column/type expressions.  If this
            pattern matches, then the string is a valid target for
            :meth:`resolve`.
        """
        if self._resolvable is None:
            # match any number of , separated columns
            self._resolvable = re.compile(
                rf"(?P<atom>{self.column.pattern}(\s*[,]\s*(?&atom))*)"
            )
        return self._resolvable

    # TODO: maybe na_strings could just be aliases of the Missing type?

    @property
    def na_strings(self) -> dict[str, Any]:
        """TODO"""
        if self._na_strings is None:
            self._na_strings = {
                str(t.missing): t.missing for t in self
                if isinstance(t, TypeMeta) and t.is_nullable
            }
        return self._na_strings

    def refresh_regex(self) -> None:
        """Clear the regex cache, forcing it to be rebuilt the next time it is
        accessed.
        """
        self._regex = None
        self._column = None
        self._resolvable = None
        self._na_strings = None

    ################################
    ####    GLOBAL ACCESSORS    ####
    ################################

    @property
    def roots(self) -> UnionMeta:
        """Get a union containing all the root types that are stored in the
        registry.

        Returns
        -------
        UnionMeta
            A union containing the root types (those that inherit directly from
            :class:`Type`) of every hierarchy.
        """
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_root)
        return Union.from_types(result)

    @property
    def leaves(self) -> UnionMeta:
        """Get a union containing all the leaf types that are stored in the
        registry.

        Returns
        -------
        UnionMeta
            A union containing the concrete leaf types (those that have no children) of
            every hierarchy.
        """
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_leaf)
        return Union.from_types(result)

    @property
    def abstract(self) -> UnionMeta:
        """Get a union containing all the abstract types that are stored in the
        registry.

        Returns
        -------
        UnionMeta
            A union containing each abstract type as an unparametrized base
            type.
        """
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_abstract)
        return Union.from_types(result)

    @property
    def backends(self) -> dict[str, UnionMeta]:
        """Get a dictionary mapping backend specifiers to unions containing all
        the types that are registered under them.

        Returns
        -------
        dict[str, UnionMeta]
            A dictionary of backend strings to union types.  The contents of
            the union describe the family of types that are registered under
            the backend.
        """
        result: dict[str, LinkedSet[TypeMeta]] = {}
        for typ in self:
            if isinstance(typ, TypeMeta) and typ.backend:
                result.setdefault(typ.backend, LinkedSet()).add(typ)

        return {k: Union.from_types(v) for k, v in result.items()}

    @property
    def decorators(self) -> DecoratorPrecedence:
        """Get a simple precedence object containing all the unparametrized
        decorators that are currently registered.

        Returns
        -------
        DecoratorPrecedence
            A linked set containing each decorator as an unparametrized base
            type.  The order of the decorators in the set determines the order
            in which they are nested when applied to a single type.  The
            precedence object provides utilities for modifying this order.

        Examples
        --------
        .. doctest::

            >>> REGISTRY.decorators
            DecoratorPrecedence(Categorical, Sparse)
            >>> Sparse[Categorical[Int]]
            Categorical[Sparse[Int, <NA>], ...]
            >>> REGISTRY.decorators.swap(Sparse, Categorical)
            >>> Sparse[Categorical[Int]]
            Sparse[Categorical[Int, ...], <NA>]
        """
        return self._precedence

    @property
    def edges(self) -> Edges:
        """A set of edges (A, B) where A is considered to be less than B.

        Returns
        -------
        Edges
            A collection of edges representing overrides for the less-than and
            greater-than operators for scalar types.

        Examples
        --------
        This interface is used to customize comparisons between types in a symmetric
        manner, without breaking the transitive property of the operators.  For
        instance, if we want to make `Int32 < Int16` return `True`, then we can add an
        edge to the set:

        .. doctest::

            >>> Int32 < Int16
            False
            >>> Int16 < Int32
            True
            >>> Type.registry.edges.add(Int32, Int16)
            >>> Int32 < Int16
            True
            >>> Int16 < Int32
            False

        This also affects the behavior of the greater-than operator, as well as the
        order of the :attr:`larger <TypeMeta.larger>` and
        :attr:`smaller <TypeMeta.smaller>` attributes:

        .. doctest::

            >>> Int32 > Int16
            False
            >>> Int16 > Int32
            True
            >>> Int32.larger
            Union[NumpyInt16, NumpyInt64, PythonInt]
            >>> Int16.smaller
            Union[NumpyInt8, NumpyInt32]

        Note that edges that are applied to abstract parents will be propagated to all
        their children:

        .. doctest::

            >>> Int32["numpy"] < Int16["pandas"]
            True
            >>> Int16["numpy"] > Int32
            True
        """
        return self._edges

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __len__(self) -> int:
        return len(self._types)

    def __iter__(self) -> Iterator[TypeMeta | DecoratorMeta]:
        return iter(self._types)

    def __reversed__(self) -> Iterator[TypeMeta | DecoratorMeta]:
        return reversed(self._types)

    def __contains__(self, spec: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(spec)

        if isinstance(typ, TypeMeta):
            return (typ.parent if typ.is_flyweight else typ) in self._types

        if isinstance(typ, DecoratorMeta):
            return typ.base_type in self._types

        for t in typ:
            if isinstance(t, TypeMeta):
                if (t.parent if t.is_flyweight else t) not in self._types:
                    return False
            elif isinstance(t, DecoratorMeta):
                if t.base_type not in self._types:
                    return False

        return True

    def __str__(self) -> str:
        return f"{', '.join(str(t) for t in self)}"

    def __repr__(self) -> str:
        return f"TypeRegistry({{{', '.join(repr(t) for t in self)}}})"


class Aliases:
    """A mutable proxy for a type's aliases, which automatically pushes all changes to
    the global registry.

    This interface allows the user to remap the output of the detect() and resolve()
    helper functions at runtime.  This makes it possible to swap types out on the fly,
    or add custom syntax to the type specification mini-language to support the
    language of your choice.
    """

    def __init__(self, aliases: LinkedSet[str | type]):
        self.aliases = aliases
        self._parent: TypeMeta | DecoratorMeta | None = None  # deferred assignment

    @property
    def parent(self) -> TypeMeta | DecoratorMeta:
        """Get the type that this set of aliases points to.

        Returns
        -------
        TypeMeta | DecoratorMeta
            The type associated with these aliases.

        Raises
        ------
        TypeError
            If an attempt is made to reassign the type.
        """
        return self._parent  # type: ignore

    @parent.setter
    def parent(self, typ: TypeMeta | DecoratorMeta) -> None:
        if self._parent is not None:
            raise TypeError("cannot reassign type")

        # push all aliases to the global registry
        for alias in self.aliases:
            if alias in REGISTRY.aliases:
                raise TypeError(f"aliases must be unique: {repr(alias)}")

            if isinstance(alias, str):
                REGISTRY.refresh_regex()
                REGISTRY.strings[alias] = typ
            elif issubclass(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                REGISTRY.dtypes[alias] = typ
            else:
                REGISTRY.types[alias] = typ

        self._parent = typ

    def _register_alias(self, alias: ALIAS) -> None:
        if isinstance(alias, str):
            REGISTRY.strings[alias] = self.parent
            REGISTRY.refresh_regex()

        elif isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            REGISTRY.dtypes[type(alias)] = self.parent

        elif isinstance(alias, type):
            if issubclass(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                REGISTRY.dtypes[alias] = self.parent
            else:
                REGISTRY.types[alias] = self.parent

        else:
            raise TypeError(
                f"aliases must be strings, types, or dtypes: {repr(alias)}"
            )

    def _drop_alias(self, alias: ALIAS) -> None:
        if isinstance(alias, str):
            del REGISTRY.strings[alias]
            REGISTRY.refresh_regex()

        elif isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            del REGISTRY.dtypes[type(alias)]

        elif isinstance(alias, type):
            if issubclass(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                del REGISTRY.dtypes[alias]
            else:
                del REGISTRY.types[alias]

        else:
            raise TypeError(
                f"aliases must be strings, types, or dtypes: {repr(alias)}"
            )

    def add(self, alias: ALIAS) -> None:
        """Add an alias to a type, pushing it to the global registry.

        Parameters
        ----------
        alias : ALIAS
            The alias to add.  This can be a string, a type, or a numpy/pandas dtype
            object.

        Raises
        ------
        KeyError
            If the alias is already registered to another type
        TypeError
            If the alias is not a string, type, or dtype.

        Notes
        -----
        Adding aliases to a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.
        """
        if alias in REGISTRY.aliases:
            raise KeyError(f"aliases must be unique: {repr(alias)}")

        self._register_alias(alias)
        self.aliases.add(alias)

    def update(self, aliases: Iterable[ALIAS]) -> None:
        """Add multiple aliases to a type, pushing them to the global registry.

        Parameters
        ----------
        aliases : Iterable[ALIAS]
            The aliases to add.  These can be strings, types, or numpy/pandas dtype
            objects.

        Raises
        ------
        KeyError
            If any of the aliases are already registered to another type
        TypeError
            If any of the aliases are not strings, types, or dtypes.

        Notes
        -----
        Adding aliases to a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.
        """
        for alias in aliases:
            self.add(alias)

    def clear(self) -> None:
        """Remove all aliases from a type, dropping them from the global registry.

        Notes
        -----
        Removing aliases from a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.

        This effectively removes the type from consideration during type inference and
        resolution.
        """
        for alias in self.aliases:
            self._drop_alias(alias)
        self.aliases.clear()

    def remove(self, alias: ALIAS) -> None:
        """Remove an alias from a type, dropping it from the global registry.

        Parameters
        ----------
        alias : ALIAS
            The alias to remove.  This can be a string, a type, or a numpy/pandas dtype
            object.

        Raises
        ------
        KeyError
            If the alias is not registered to the type.

        Notes
        -----
        Removing aliases from a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.
        """
        if alias not in REGISTRY.aliases:
            raise KeyError(f"alias not found: {repr(alias)}")

        if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            typ = type(alias)
            self._drop_alias(typ)
            self.aliases.remove(typ)
        else:
            self._drop_alias(alias)
            self.aliases.remove(alias)

    def discard(self, alias: ALIAS) -> None:
        """Remove an alias from the set if it is present, reflecting changes in the
        global registry.

        Parameters
        ----------
        alias : ALIAS
            The alias to remove.  This can be a string, a type, or a numpy/pandas dtype
            object.

        Notes
        -----
        Removing aliases from a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.
        """
        if alias not in self.aliases:
            return

        if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            typ = type(alias)
            self._drop_alias(typ)
            self.aliases.remove(typ)
        else:
            self._drop_alias(alias)
            self.aliases.remove(alias)

    def pop(self, index: int = -1) -> ALIAS:
        """Pop an alias from the set, dropping it from the global registry.

        Parameters
        ----------
        index : int, default -1
            The index of the alias to pop.  This defaults to the last alias in the set.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Returns
        -------
        ALIAS
            The popped alias.

        Notes
        -----
        Removing aliases from a type modifies the behavior of the :func:`detect` and
        :func:`resolve` helper functions, allowing users to remap their output.  See
        the documentation of these methods for more information.
        """
        result = self.aliases.pop(index)
        self._drop_alias(result)
        return result

    __hash__ = None  # type: ignore

    def __contains__(self, alias: ALIAS) -> bool:
        if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            return type(alias) in self.aliases
        return alias in self.aliases

    def __len__(self) -> int:
        return len(self.aliases)

    def __iter__(self) -> Iterator[ALIAS]:
        return iter(self.aliases)

    def __reversed__(self) -> Iterator[ALIAS]:
        return reversed(self.aliases)

    def __str__(self) -> str:
        return f"{{{', '.join(repr(a) for a in self.aliases)}}}"

    def __repr__(self) -> str:
        return f"Aliases({', '.join(repr(a) for a in self.aliases)})"


REGISTRY = TypeRegistry()


# TODO: move get_from_caller() into object.py.  It is no longer referenced in this file


def get_from_calling_context(name: str, skip: int = 0) -> Any:
    """Get an object from the calling context given its fully-qualified (dotted) name
    as a string.

    Parameters
    ----------
    name: str
        The fully-qualified name of the object to retrieve.
    skip: int, default 0
        Skip a number of frames up the call stack before beginning the search.
        Defaults to 0, which starts at the caller's immediate context.

    Returns
    -------
    Any
        The object corresponding to the specified name.

    Raises
    ------
    AttributeError
        If the named object cannot be found.

    Notes
    -----
    This function avoids calling eval() and the associated security risks by traversing
    the call stack and searching for the object in each frame's local, global, and
    built-in namespaces.  More specifically, it searches for the first component of a
    dotted name, and, if found, progressively resolves the remaining components by
    calling getattr() on the previous result until the string is exhausted.  Otherwise,
    if no match is found, it moves up to the next frame and repeats the process.

    This is primarily used to extract named objects from stringified type hints when
    `from __future__ import annotations` is enabled, or for parsing object strings in
    the type specification mini-language.

    .. note::

        PEP 649 (https://peps.python.org/pep-0649/) could make this partially obsolete
        by avoiding stringification in the first place.  Still, it can be useful for
        backwards compatibility.
    """
    path = name.split(".")
    prefix = path[0]
    frame = inspect.currentframe()
    if frame is not None:
        for _ in range(skip + 1):
            frame = frame.f_back
            if frame is None:
                break

    while frame is not None:
        if prefix in frame.f_locals:
            obj = frame.f_locals[prefix]
        elif prefix in frame.f_globals:
            obj = frame.f_globals[prefix]
        elif prefix in frame.f_builtins:
            obj = frame.f_builtins[prefix]
        else:
            frame = frame.f_back
            continue

        for component in path[1:]:
            try:
                obj = getattr(obj, component)
            except AttributeError:
                continue  # back off to next frame

        return obj

    raise AttributeError(f"could not find object: {repr(name)}")


#########################
####    RESOLVE()    ####
#########################


# TODO: it makes sense to modify aliases to match python syntax.  This would make it
# trivial to parse python-style type hints.


# TODO: register NoneType to Missing, such that int | None resolves to
# Union[PythonInt, Missing]
# Same with None alias, which allows parsing of type hints with None as a type


# NOTE: string parsing currently does not support dictionaries or key-value mappings
# within Union[] operator (i.e. Union[{"foo": ...}], Union[[("foo", ...)]]).  This
# is technically possible to implement, but would add a lot of extra complexity.


def resolve(target: TYPESPEC | Iterable[TYPESPEC]) -> META:
    """Convert a type specifier into an equivalent bertrand type.

    Parameters
    ----------
    target : TYPESPEC | Iterable[TYPESPEC]
        A type specifier to convert.  This can be a python type, a numpy/pandas dtype,
        a string in the type specification mini-language, a previously-normalized
        bertrand type, or an iterable containing any of the above.  If a mapping is
        provided, then the keys will be used as column names and the values will be
        interpreted as type specifiers.

    Returns
    -------
    META
        The equivalent bertrand type.

    Raises
    ------
    TypeError
        If the specifier cannot be resolved into a valid type.

    See Also
    --------
    detect : Detect the type of a given object or container.

    Notes
    -----
    This function is called internally wherever a type is expected, allowing users
    to pass any of the supported type specifiers in place of a proper bertrand type.

    The types that are returned by this function can be customized by modifying their
    aliases or adding special constructor methods to return parametrized outputs.  For
    reference, the global mapping for each type can be accessed via the
    :attr:`TypeRegistry.aliases` attribute.

    Examples
    --------
    :func:`resolve` can accept other bertrand types, in which case it will return its
    argument unchanged.

    .. doctest::

        >>> resolve(Int)
        Int
        >>> resolve(Sparse[Int])
        Sparse[Int, <NA>]
        >>> resolve(Int | Float)
        Union[Int, Float]

    It can also accept python types in standard syntax, in which case it will search
    the aliases dictionary for a matching type (or types in the case of a union):

    .. doctest::

        >>> resolve(str)
        PythonString
        >>> resolve(str | bool)
        Union[PythonString, PythonBool]

    Numpy/pandas dtypes can be parsed similarly, with optional parametrization for
    each type:

    .. doctest::

        >>> resolve(np.dtype(np.int32))
        NumpyInt32
        >>> resolve(np.dtype([("foo", np.int32), ("bar", np.uint8)]))
        Union['foo': NumpyInt32, 'bar': NumpyUInt8]
        >>> resolve(pd.DatetimeTZDtype(tz="UTC"))
        PandasTimestamp['UTC']
        >>> resolve(pd.SparseDtype(np.int64))
        Sparse[NumpyInt64, 0]

    Numpy-style string specifiers are also supported, with similar semantics to numpy's
    :func:`dtype() <numpy.dtype>` function:

    .. doctest::
    
        >>> resolve("int32")
        Int32
        >>> resolve("?")
        Bool
        >>> resolve("U32")
        SizedUnicode[32]

    However, bertrand's string parsing capabilities are significantly more powerful
    than numpy's, and can be used to specify more complex types.  For instance, one can
    pass arguments to a type's constructor by enclosing them in square brackets,
    equivalent to normal bertrand syntax:

    .. doctest::

        >>> resolve("M8[ns]")
        NumpyDatetime64['ns']
        >>> resolve("Object[int]")
        Object[<class 'int'>]
        >>> resolve("Timedelta[numpy, 10ns]")
        NumpyTimedelta64['ns', 10]
        >>> resolve("sparse[bool, False]")
        Sparse[Bool, False]

    This syntax is fully generalized, and each type can define its own semantics for
    each argument, which they can parse however they like.  Just like their bertrand
    equivalents, abstract types will accept an optional backend specifier and forward
    any additional arguments to that type.  What's more, the arguments can themselves
    be nested type specifiers, which is particularly relevant for decorator types:

    .. doctest::

        >>> resolve("Sparse[Timedelta[numpy, 10ns]]")
        Sparse[NumpyTimedelta64['ns', 10], NaT]
        >>> resolve("Sparse[Categorical[String[python]]]")
        Sparse[Categorical[PythonString], <NA>]

    Similarly, multiple comma or pipe-separated types can be given to form a union:

    .. doctest::

        >>> resolve("char, unsigned long")  # platform-specific
        Union[Int8, UInt32]
        >>> resolve("int32 | float64")
        Union[Int32, Float64]
        >>> resolve("Union[Int64[pandas], Categorical[string, [a, b, c]]")
        Union[PandasInt64, Categorical[String, ['a', 'b', 'c']]]

    Optional column names can also be defined to create a structured union:

    .. doctest::

        >>> resolve("foo: int32")
        Union['foo': Int32]
        >>> resolve("foo: int32 | int64, bar: bool")
        Union['foo': Int32 | Int64, 'bar': Bool]
        >>> resolve("Union[foo: categorical[string], bar: decimal]")
        Union['foo': Categorical[String, <NA>], 'bar': Decimal]

    This level of string parsing allows the type system to interpret type hints in
    several different formats, both stringified and not, with identical semantics to
    the normal bertrand types.

    .. doctest::

        >>> from __future__ import annotations
        >>> def foo(x: int | None) -> "long long":
        ...     ...
        >>> foo.__annotations__
        {'x': 'int | None', 'return': "'long long'"}
        >>> resolve(foo.__annotations__["x"])
        Union[PythonInt, Missing]
        >>> resolve(foo.__annotations__["return"])
        Int64

    Lastly, :func:`resolve` can also accept an iterable containing any of the above
    type specifiers, in which case it will return a union containing each type.

    .. doctest::

        >>> resolve([Decimal, float, pd.BooleanDtype(), "int32"])
        Union[Decimal, PythonFloat, PandasBool, Int32]

    If the iterable is a mapping or only contains key-value pairs of length 2, then
    the keys will be interpreted as column names in a structured union:

    .. doctest::

        >>> resolve({"foo": complex, "bar": Unsigned["pandas"]})
        Union['foo': PythonComplex, 'bar': PandasUInt64]
        >>> resolve([("foo", complex), ("bar", Unsigned["pandas"])])
        Union['foo': PythonComplex, 'bar': PandasUInt64]

    These key-value pairs can also be represented as slices, which is used internally
    by the normal union constructor to create structured unions:

    .. doctest::

        >>> resolve(slice("foo", np.int16))
        Union['foo': NumpyInt16]
        >>> resolve([slice("foo", np.uint8), slice("bar", "datetime")])
        Union['foo': NumpyUInt8, 'bar': Datetime]

    Lastly, each alias (python type, numpy/pandas dtype, or string specifier) can be
    remapped dynamically at runtime, which will automatically change the behavior of
    :func:`resolve`:

    .. doctest::

        >>> Decimal.aliases
        Aliases({'decimal'})
        >>> Decimal.aliases.add("foo")
        >>> resolve("foo")
        Decimal
        >>> Decimal.aliases.remove("foo")
        >>> resolve("foo")
        Traceback (most recent call last):
            ...
        TypeError: invalid specifier -> 'foo'
    """
    if isinstance(target, (TypeMeta, DecoratorMeta, UnionMeta, StructuredMeta)):
        return target

    if isinstance(target, type):
        if target in REGISTRY.types:
            return REGISTRY.types[target]
        return Object[target]

    if isinstance(target, UnionType):  # python int | float | None syntax, not bertrand
        return Union.from_types(LinkedSet(resolve(t) for t in target.__args__))

    if isinstance(target, np.dtype):
        if target.fields:
            return StructuredUnion.from_columns({  # type: ignore
                col: _resolve_dtype(dtype) for col, (dtype, _) in target.fields.items()
            })
        return _resolve_dtype(target)

    if isinstance(target, pd.api.extensions.ExtensionDtype):
        # if isinstance(target, SyntheticDtype):
        #     return target._bertrand_type
        return _resolve_dtype(target)

    if isinstance(target, str):
        # strip leading/trailing whitespace and special syntax related to unions
        stripped = target.strip()
        if stripped.startswith("Union[") and stripped.endswith("]"):
            as_union = True
            stripped = stripped[6:-1].strip()
        elif stripped.startswith("[") and stripped.endswith("]"):
            as_union = True
            stripped = stripped[1:-1].strip()
        elif stripped.startswith("(") and stripped.endswith(")"):
            as_union = True
            stripped = stripped[1:-1].strip()
        elif stripped.startswith("{") and stripped.endswith("}"):
            as_union = True
            stripped = stripped[1:-1].strip()
        else:
            as_union = False

        # ensure string can be resolved into a valid type
        fullmatch = REGISTRY.resolvable.fullmatch(stripped)
        if not fullmatch:
            raise TypeError(f"invalid specifier -> {repr(target)}")

        return _resolve_string(fullmatch.group(), as_union)

    if isinstance(target, slice):
        return StructuredUnion.from_columns({target.start: resolve(target.stop)})

    if isinstance(target, dict):
        return StructuredUnion.from_columns({
            key: resolve(value) for key, value in target.items()
        })

    if hasattr(target, "__iter__"):
        it = iter(target)
        try:
            first = next(it)
        except StopIteration:
            return Union.from_types(LinkedSet())

        if isinstance(first, slice):
            slices = {first.start: resolve(first.stop)}
            for item in it:
                if not isinstance(item, slice):
                    raise TypeError(f"expected a slice -> {repr(item)}")
                slices[item.start] = resolve(item.stop)  # type: ignore
            return StructuredUnion.from_columns(slices)

        if isinstance(first, (list, tuple)):
            if len(first) != 2:
                raise TypeError(f"expected a tuple of length 2 -> {repr(first)}")

            pairs = {first[0]: resolve(first[1])}
            for item in it:
                if not isinstance(item, (list, tuple)) or len(item) != 2:  # type: ignore
                    raise TypeError(f"expected a tuple of length 2 -> {repr(item)}")
                pairs[item[0]] = resolve(item[1])  # type: ignore
            return StructuredUnion.from_columns(pairs)

        typ = resolve(first)
        if isinstance(typ, UnionMeta):
            flat = LinkedSet(typ)
        else:
            flat = LinkedSet([typ])
        for t in it:
            typ = resolve(t)
            if isinstance(typ, UnionMeta):
                flat.update(typ)
            else:
                flat.add(typ)

        return Union.from_types(flat)

    raise TypeError(f"invalid specifier -> {repr(target)}")


def _resolve_dtype(target: DTYPE) -> META:
    typ = REGISTRY.dtypes.get(type(target), None)
    if typ is None:
        raise TypeError(f"unrecognized dtype -> {repr(target)}")

    if typ._defines_from_dtype:  # pylint: disable=protected-access
        return typ.from_dtype(target)
    return typ


def _resolve_string(target: str, as_union: bool) -> META:
    # split into optional column specifiers -> 'column: type | type | ...'
    matches = list(REGISTRY.column.finditer(target))

    # resolve each specifier individually
    columns: dict[str, META] = {}
    flat = LinkedSet()
    for match in matches:
        col = match.group("column")
        specifiers = list(REGISTRY.regex.finditer(match.group("type")))

        # if column name is given, then add to columns dict
        if col:
            col = col.strip()
            if (
                (col.startswith("'") and col.endswith("'")) or
                (col.startswith('"') and col.endswith('"'))
            ):
                col = col[1:-1].strip()

            if col in columns:
                raise TypeError(f"duplicate column name -> {repr(col)}")

            if len(specifiers) > 1:
                columns[col] = Union.from_types(LinkedSet(
                    _process_string(s) for s in specifiers
                ))
            else:
                columns[col] = _process_string(specifiers[0])

        # otherwise, add to flat set
        else:
            flat.update(_process_string(s) for s in specifiers)

    # disallow mixed syntax
    if columns and flat:
        raise TypeError(
            f"cannot mix structured and unstructured syntax -> {repr(target)}"
        )

    if columns:
        return StructuredUnion.from_columns(columns)

    if len(flat) == 1 and not as_union:
        return flat.pop()

    return Union.from_types(flat)


def _process_string(match: re.Match[str]) -> META:
    groups = match.groupdict()

    # resolve alias to specified type
    if groups.get("sized_unicode"):  # special case for U32, U{xx} syntax
        typ = String
    else:
        typ = REGISTRY.strings[groups["alias"]]

    # check for optional arguments
    arguments = groups["args"]
    if not arguments:
        return typ

    # extract args
    args = []
    for m in TOKEN.finditer(arguments):
        substring = m.group().strip()
        if (
            (substring.startswith("'") and substring.endswith("'")) or
            (substring.startswith('"') and substring.endswith('"'))
        ):
            substring = substring[1:-1].strip()
        args.append(substring)

    # pass to type's from_string() method
    return typ.from_string(*args)


def _nested_expr(prefix: str, suffix: str, group_name: str) -> str:
    """Produce a regular expression to match nested sequences with the specified
    opening and closing tokens.  Relies on PCRE-style recursive patterns.
    """
    opener = re.escape(prefix)
    closer = re.escape(suffix)
    body = rf"(?P<body>([^{opener}{closer}]|(?&{group_name}))*)"
    return rf"(?P<{group_name}>{opener}{body}{closer})"


INVOCATION = re.compile(
    rf"(?P<invocation>[^\(\)\[\],]+)"
    rf"({_nested_expr('(', ')', 'signature')}|{_nested_expr('[', ']', 'options')})"
)
SEQUENCE = re.compile(
    rf"(?P<sequence>"
    rf"{_nested_expr('(', ')', 'parens')}|"
    rf"{_nested_expr('[', ']', 'brackets')})"
)
LITERAL = re.compile(r"[^,]+")
TOKEN = re.compile(rf"{INVOCATION.pattern}|{SEQUENCE.pattern}|{LITERAL.pattern}")


########################
####    DETECT()    ####
########################


def detect(data: Any, drop_na: bool = True) -> META:
    """Infer the type of a scalar, sequence, dataframe, or structured array.

    Parameters
    ----------
    data : Any
        The data to infer the type of.  This can either be a single object or a
        collection of objects organized into a vector.  If the data is a numpy array
        or pandas data structure with an appropriate dtype, then the dtype will be
        interpreted directly in constant time.  Otherwise, the type will be inferred
        elementwise in a fast, vectorized manner.
    drop_na : bool, default True
        Indicates whether to disregard missing values in the final type.  If set to
        True (the default), then dtype-based inference will make no extra effort to
        account for missing values, which gives optimal performance.  If False, missing
        values will be inserted post-hoc by checking :func:`pandas.isna`.  For
        elementwise inference, missing values are dropped either way.  This argument
        simply controls whether they are reintroduced to the final type or discarded
        before returning.

    Returns
    -------
    META
        The inferred bertrand type.

    See Also
    --------
    resolve : Convert a type specifier into an equivalent bertrand type.

    Notes
    -----
    This function is called internally by :func:`isinstance` to perform schema checks
    on arbitrary data.  It is also used during the :func:`dispatch` process to
    determine which overload(s) to call for a given set of arguments.

    The types that are returned by this function can be customized by modifying their
    aliases or adding special constructor methods to return parametrized outputs.  For
    reference, the global mapping for each type can be accessed via the
    :attr:`TypeRegistry.aliases` attribute.

    .. note::

        This function does not raise errors.  Instead, if it encounters an object that
        it cannot resolve into a specialized type, it will return a parametrized
        :class:`Object` type that wraps the Python class directly.  This is
        functionally identical to numpy's `object` dtype, but is more type safe, since
        it is labeled with an explicit Python type.

    Examples
    -------
    :func:`detect` can accept scalar values, which are interpreted directly.

    .. doctest::

        >>> detect(1)
        PythonInt
        >>> detect(np.int32(1))
        NumpyInt32
        >>> detect("foo")
        PythonString

    The way this works is by calling the :class:`type() <python.type>` function on the
    input and then searching the global registry for an identical type.  If one is
    found, then the matching bertrand type's :meth:`from_scalar() <Type.from_scalar>`
    method will be called to account for possible parametrization.  Otherwise, a
    generic :class:`Object` type will be returned:

    .. doctest::

        >>> class Foo:
        ...     pass
        >>> detect(Foo())
        Object[<class 'Foo'>]

    This process is repeated multiple times for unlabeled sequences, which are
    interpreted elementwise according to the same rules:

    .. doctest::

        >>> detect([1, 2, 3])
        Union[PythonInt]
        >>> detect([1, 2.0, "3", Foo()])
        Union[PythonInt, PythonFloat, PythonString, Object[<class 'Foo'>]]

    Note that these types are returned as unions even when they only contain a single
    element type.  This is because the :func:`detect` function also records the
    observed type at every index of the sequence, which can be used later for more
    complex analysis.  Accessing these types can be done via the :attr:`Union.index`
    attribute.

    .. doctest::

        >>> detect([1, 2, 3]).index
        array([PythonInt, PythonInt, PythonInt], dtype=object)
        >>> detect([1, 2.0, "3", Foo()]).index
        array([PythonInt, PythonFloat, PythonString, Object[<class 'Foo'>]],
              dtype=object)

    .. note::

        Internally, this array is stored using run-length encoding, which compresses
        runs of a single type into a single entry.  This improves both detection speed
        and memory efficiency.  When the array is accessed, it is automatically
        expanded back into its original form.

    If the input is a numpy array or pandas data structure with an appropriate dtype,
    then the dtype will be interpreted directly in constant time:

    .. doctest::

        >>> detect(np.array([1, 2, 3]))
        Union[NumpyInt64]
        >>> detect(pd.Series([1, 2, 3]))
        Union[NumpyInt64]
        >>> arr = np.arange(10**6)
        >>> from timeit import timeit
        >>> timeit(lambda: detect(arr), number=10**3)  # doctest: +SKIP
        0.005038505998527398

    This also works for dataframes and structured arrays, which will return a
    structured union containing the type of each column:

    .. doctest::

        >>> detect(pd.DataFrame({"foo": [1, 2, 3], "bar": [4., 5., 6.]}))
        Union['foo': NumpyInt64, 'bar': NumpyFloat64]
        >>> detect(np.array([(1, 4.), (2, 5.), (3, 6.)], dtype=[("foo", np.int64), ("bar", np.float64)]))
        Union['foo': NumpyInt64, 'bar': NumpyFloat64]

    Accessing the index of these objects yields a structured array containing the
    observed type of each row:

    .. doctest::

        >>> detect(pd.DataFrame({"foo": [1, 2, 3], "bar": [4., 5., 6.]})).index
        array([(NumpyInt64, NumpyFloat64), (NumpyInt64, NumpyFloat64),
               (NumpyInt64, NumpyFloat64)], dtype=[('foo', 'O'), ('bar', 'O')])

    If an array's dtype is ambiguous (i.e. ``dtype: object``), then the same
    elementwise process from before is used to disambiguate:

    .. doctest::

        >>> detect(np.array([1, 2, 3], dtype=object))
        Union[PythonInt]
        >>> pd.DataFrame({"foo": [1, 2., "3", Foo()]})
        Union['foo': PythonInt | PythonFloat | PythonString | Object[<class 'Foo'>]]

    .. warning::

        In the interest of speed, this function will not consider inheritance when
        performing scalar or elementwise detection.  Instead, the python class must
        exactly match one of the aliases in the registry.  If this is not the case,
        then a spurious :class:`Object` type can be returned, which may not be what
        the user intended.  In order to fix this, one should add an alias to the
        expected type that specifically matches the class in question.
    """
    data_type = type(data)

    if issubclass(data_type, (TypeMeta, DecoratorMeta, UnionMeta, StructuredMeta)):
        return data

    if issubclass(data_type, pd.DataFrame):
        return StructuredUnion.from_columns({
            col: _detect_dtype(data[col], drop_na=drop_na) for col in data.columns
        })

    if issubclass(data_type, np.ndarray) and data.dtype.fields:
        return StructuredUnion.from_columns({
            col: _detect_dtype(data[col], drop_na=drop_na) for col in data.dtype.names
        })

    if hasattr(data, "__iter__") and not isinstance(data, type):
        if data_type in REGISTRY.types:
            return _detect_scalar(data, data_type)
        elif hasattr(data, "dtype"):
            return _detect_dtype(data, drop_na)
        else:
            # convert to numpy array and strip missing values
            arr = np.asarray(data, dtype=object)
            missing = pd.isna(arr)
            hasnans = missing.any()
            if hasnans:
                arr = arr[~missing]  # pylint: disable=invalid-unary-operand-type

            union = _detect_elementwise(arr)

            # reinsert missing values
            if hasnans and not drop_na:
                index = np.full(missing.shape[0], Missing, dtype=object)
                index[~missing] = union.index  # pylint: disable=invalid-unary-operand-type
                return Union.from_types(union | Missing, _rle_encode(index))

            return union

    return _detect_scalar(data, data_type)


def _detect_scalar(data: Any, data_type: type) -> META:
    if pd.isna(data):
        return Missing

    typ = REGISTRY.types.get(data_type, None)
    if typ is None:
        return Object[data_type]

    if typ._defines_from_scalar:  # pylint: disable=protected-access
        return typ.from_scalar(data)
    return typ


def _detect_dtype(data: Any, drop_na: bool) -> META:
    dtype = data.dtype

    if dtype == np.dtype(object):  # ambiguous - loop through elementwise
        return _detect_elementwise(data)

    # if isinstance(dtype, SyntheticDtype):
    #     typ = dtype._bertrand_type
    # else:
    typ = REGISTRY.dtypes.get(type(dtype), None)
    if typ is None:
        raise TypeError(f"unrecognized dtype -> {repr(dtype)}")

    if typ._defines_from_dtype:  # pylint: disable=protected-access
        result = typ.from_dtype(dtype)
    else:
        result = typ

    if not drop_na:
        is_na = pd.isna(data)
        if is_na.any():
            index = np.full(is_na.shape[0], Missing, dtype=object)
            index[~is_na] = result  # pylint: disable=invalid-unary-operand-type
            return Union.from_types(LinkedSet([result, Missing]), _rle_encode(index))

    index = np.array(
        [(result, len(data))],
        dtype=[("value", object), ("count", np.intp)]
    )
    return Union.from_types(LinkedSet([result]), index)


def _detect_elementwise(data: Any) -> UnionMeta:
    observed = LinkedSet()
    counts = []
    count = 0
    prev: None | TypeMeta | DecoratorMeta = None
    lookup = REGISTRY.types

    for element in data:
        element_type = type(element)

        # search for a matching type in the registry
        typ = lookup.get(element_type, None)
        if typ is None:
            typ = Object[element_type]
        elif typ._defines_from_scalar:  # pylint: disable=protected-access
            typ = typ.from_scalar(element)

        # update counts
        if typ is prev:
            count += 1
        else:
            observed.add(typ)
            if prev is not None:
                counts.append((prev, count))
            prev = typ
            count = 1

    # add final count
    if prev is not None:
        observed.add(prev)
        counts.append((prev, count))

    # encode as structured array
    index = np.array(counts, dtype=[("value", object), ("count", np.intp)])
    return Union.from_types(observed, index)


def _rle_encode(arr: OBJECT_ARRAY) -> RLE_ARRAY:  # type: ignore
    """Apply run-length encoding to an array of objects, returning the result
    as a structured array with two columns: "value" and "count".
    """
    # get indices where transitions occur
    idx = np.flatnonzero(arr[:-1] != arr[1:])
    idx = np.concatenate([[0], idx + 1, [arr.shape[0]]])

    # get values at each transition
    values = arr[idx[:-1]]

    # compute run lengths as difference between transition indices
    counts = np.diff(idx)

    # encode as structured array
    return np.array(
        list(zip(values, counts)),
        dtype=[("value", arr.dtype), ("count", np.intp)]
    )


def _rle_decode(arr: RLE_ARRAY) -> OBJECT_ARRAY:
    """Undo run-length encoding on a structured array to recover the original
    object array.
    """
    return np.repeat(arr["value"], arr["count"])  # type: ignore


###########################
####    METACLASSES    ####
###########################


T = TypeVar("T")
UNION_RETURN = Mapping["TypeMeta | DecoratorMeta", T]
STRUCTURED_RETURN = Mapping[str, T | UNION_RETURN[T]]
META_RETURN = PyUnion[T, UNION_RETURN[T], STRUCTURED_RETURN[T]]


def _copy_docs(cls: type) -> type:
    """Copy docstrings from BaseMeta to the specified class."""
    ignore = {"mro"}
    for name in dir(BaseMeta):
        if not name.startswith("_") and name not in ignore:
            getattr(cls, name).__doc__ = getattr(BaseMeta, name).__doc__

    return cls


class BaseMeta(type):
    """Base class for all bertrand metaclasses.

    This is not meant to be used directly.  It exists only to document and provide
    shared methods and attributes to the other metaclasses.
    """

    # pylint: disable=no-value-for-parameter, redundant-returns-doc

    @property
    def registry(cls) -> TypeRegistry:
        """A reference to the global type registry.

        Returns
        -------
        TypeRegistry
            The global type registry.

        Notes
        -----
        This attribute is inherited by all bertrand type objects, and can be used to
        access the global type registry from anywhere in the type system.  It is
        identical to the global ``REGISTRY`` object.
        """
        return REGISTRY

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __getitem__(cls, params: Any | tuple[Any, ...]) -> META:
        """A wrapper around a type's `__class_getitem__` method that automatically
        unpacks tuples into a more traditional call syntax.
        """
        raise NotImplementedError()

    def from_scalar(cls, scalar: Any) -> TypeMeta | DecoratorMeta:
        """Construct a type from a scalar example of that type.

        Parameters
        ----------
        scalar: Any
            A scalar whose python type exactly matches one of the type's aliases.

        Returns
        -------
        TypeMeta | DecoratorMeta
            A type object corresponding to the given scalar.  Each type is free to
            interpret this scalar however it sees fit, and may return a parametrized
            type based on its value.

        See Also
        --------
        detect: infer the type of a scalar, sequence, dataframe, or structured array.

        Notes
        -----
        This method is a special case in the metaclass machinery, which automatically
        extracts and verifies its signature.  If it is overridden on a descendant type,
        then the metaclass will ensure that it is a classmethod that accepts only a
        single positional argument, and will raise an error if this is not the case.
        Otherwise, it will fall back to a simple identity function.
        
        If the fallback implementation is used, then it will be optimized away during
        type detection, giving optimal performance in the usual case of
        non-parametrized types.  Individual types can override this method to add
        parametrization support via scalar examples, which will be applied
        automatically during :func:`detect` calls.
        """
        raise NotImplementedError()

    def from_dtype(cls, dtype: DTYPE) -> TypeMeta | DecoratorMeta:
        """Construct a type from an equivalent numpy/pandas dtype object.

        Parameters
        ----------
        dtype: DTYPE
            A numpy or pandas dtype object whose type matches one of the type's
            aliases.

        Returns
        -------
        TypeMeta | DecoratorMeta
            A type object corresponding to the given dtype.  Each type is free to
            interpret the dtype however it sees fit, and may return a parametrized
            type based on its value.

        See Also
        --------
        detect: infer the type of a scalar, sequence, dataframe, or structured array.
        resolve: convert a type specifier into an equivalent bertrand type.

        Notes
        -----
        This method is a special case in the metaclass machinery, which automatically
        extracts and verifies its signature.  If it is overridden on a descendant type,
        then the metaclass will ensure that it is a classmethod that accepts only a
        single positional argument, and will raise an error if this is not the case.
        Otherwise, it will fall back to a simple identity function.

        If the fallback implementation is used, then it will be optimized away during
        :func:`detect` and :func:`resolve` calls, giving optimal performance in the
        usual case of non-parametrized types.  Individual types can override this
        method to translate parameters from existing numpy/pandas dtypes as needed.
        If given, this method will be called automatically during :func:`detect` and
        :func:`resolve` calls whenever they encounter a matching dtype.
        """
        raise NotImplementedError()

    def from_string(cls, *args: str) -> TypeMeta | DecoratorMeta:
        """Construct a type from a string in the type-specification mini-language.

        Parameters
        ----------
        *args: str
            Any number of positional arguments given as strings.  These represent
            tokenized arguments given to a type or one of its aliases using normal
            square bracket syntax.  For example, ``resolve("Datetime[pandas, UTC]")``
            would be parsed as ``Datetime.from_string("pandas", "UTC")``.

        Returns
        -------
        TypeMeta | DecoratorMeta
            A type object corresponding to the given arguments.  Each type is free to
            interpret these arguments however it sees fit, and may return a
            parametrized type based on their values.

        Notes
        -----
        This method is a special case in the metaclass machinery, which automatically
        extracts and verifies its signature.  If it is overridden on a descendant type,
        then the metaclass will ensure that it is a classmethod whose signature is
        compatible with ``__class_getitem__()``, and will raise an error if this is not
        the case.  Otherwise, it will inject a default implementation that differs for
        abstract and concrete types.

        For abstract types, the metaclass will inject a definition that extracts the
        first argument (if any) and interprets it as a backend specifier.  It will then
        look up the backend in its implementations table and forward any remaining
        arguments to the resulting type's ``from_string()`` method.  For consistency,
        abstract types are not allowed to change this behavior, and any attempt to
        override this method will raise an error.

        For concrete types, the metaclass will fall back to a simple identity function,
        which accepts no arguments.  This will be optimized away during type
        resolution, giving optimal performance in the usual case of non-parametrized
        types.  Individual types can override this method to add parametrization
        support via string arguments, similar to ``__class_getitem__()``.  Users should
        take care to ensure that both of these methods have identical semantics, as
        they will be used interchangeably to resolve type hints when
        ``from __future__ import annotations`` is enabled.
        """
        raise NotImplementedError()

    ##########################
    ####    FLYWEIGHTS    ####
    ##########################

    @property
    def is_flyweight(cls) -> META_RETURN[bool]:
        """Check whether the type is a parametrized flyweight.

        Returns
        -------
        bool
            True if the type was produced by one of the following parametrization
            methods.  False otherwise.

        See Also
        --------
        __class_getitem__: defines parametrization behavior for bertrand types.
        from_scalar: parses a scalar example of this type.
        from_dtype: parses a numpy dtype object registered to this type.
        from_string: parses a string representation of this type.

        Examples
        --------
        .. doctest::

            >>> Object.is_flyweight
            False
            >>> Object[int].is_flyweight
            True
        """
        raise NotImplementedError()

    @property
    def flyweights(cls) -> META_RETURN[Mapping[str, TypeMeta | DecoratorMeta]]:
        """A read-only proxy for this type's flyweight cache.

        Returns
        -------
        Mapping[str, TypeMeta | DecoratorMeta]
            A mapping of string identifiers to flyweight types.  This is a read-only
            proxy for a :class:`LinkedDict`, which is a custom C++ data structure
            that combines a hash table with a doubly-linked list.  The order of the
            keys always reflects current LRU order, with the most recently used
            flyweight at the beginning of the dictionary.  Note that this order is
            not necessarily stable, and can change without invalidating the proxy.

        See Also
        --------
        cache_size: the maximum size of this type's flyweight cache.
        flyweight: create a flyweight with the specified parameters.

        Examples
        --------
        .. doctest::

            >>> Object.flyweights
            mappingproxy(LinkedDict({}))
            >>> Object[int]
            Object[<class 'int'>]
            >>> Object.flyweights
            mappingproxy(LinkedDict({"Object[<class 'int'>]": Object[<class 'int'>]}))
        """
        raise NotImplementedError()

    @property
    def base_type(cls) -> META:
        """Return an unparametrized version of this type.

        Returns
        -------
        META
            A type (possibly a union) with all parameters removed.  If the type is not
            parametrized, then this will be a self-reference.

        See Also
        --------
        flyweight: create a flyweight with the specified parameters.
        flyweights: a read-only proxy for this type's flyweight cache.
        is_flyweight: indicates whether this type is a parametrized flyweight.

        Examples
        --------
        .. doctest::

            >>> Object.base_type
            Object
            >>> Object[int].base_type
            Object
            >>> Sparse[Object[int]].base_type
            Sparse
        """
        raise NotImplementedError()

    @property
    def cache_size(cls) -> META_RETURN[int | None]:
        """The maximum size of this type's flyweight cache, if it is limited to
        a finite size.

        Returns
        -------
        int | None
            The maximum number of flyweights that can be stored in this type's
            LRU cache.  If None, then the cache is unlimited.

        See Also
        --------
        flyweight: create a flyweight with the specified parameters.
        flyweights: a read-only proxy for this type's flyweight cache.

        Examples
        --------
        .. code-block:: python

            class Foo(Type, cache_size=10):
                ...

            assert Foo.cache_size == 10
        """
        raise NotImplementedError()

    @property
    def params(cls) -> META_RETURN[Mapping[str, Any]]:
        """A read-only mapping of parameters to their configured values.

        Returns
        -------
        Mapping[str, Any]
            A mapping of parameter names to their values.  These are determined by
            to :meth:`Type.__class_getitem__()` when the type is parametrized.

        Notes
        -----
        The parameter names and default values are inferred from the signature of
        :meth:`__class_getitem__() <Type.__class_getitem__>`, and are available as
        dotted attributes on the type itself.

        Examples
        --------
        .. doctest::

            >>> PandasTimestamp.params
            mappingproxy({'tz': None})
            >>> PandasTimestamp["UTC"].params
            mappingproxy({'tz': datetime.timezone.utc})
            >>> PandasTimestamp["UTC"].tz
            datetime.timezone.utc
        """
        raise NotImplementedError()

    def flyweight(cls, *args: Any, **kwargs: Any) -> TypeMeta | DecoratorMeta:
        """Get a flyweight type with the specified parameters.

        Parameters
        ----------
        *args: Any
            Positional arguments used to parametrize the type.  These should always
            match the signature of `__class_getitem__()` in both number and order, as
            they will be inserted into the resulting type's namespace under that
            assumption.  The values will also be used to construct a unique string
            identifier for the type, which is used to look up the corresponding
            flyweight in the type's LRU cache.
        **kwargs: Any
            Keyword arguments representing overrides for a parametrized type's fields.
            These are injected directly into the type's namespace, and will override
            any existing values with the same key(s).

        Returns
        -------
        TypeMeta | DecoratorMeta
            A flyweight type with the specified parameters.

        Raises
        ------
        TypeError
            If the number of positional arguments does not match the signature of
            `__class_getitem__()`.  Note that order is not checked, so users should
            take care to ensure that the arguments are passed in the same order.

        See Also
        --------
        Type.__class_getitem__: defines parametrization behavior for bertrand types.
        Type.from_scalar: parses a scalar example of this type.
        Type.from_dtype: parses a numpy dtype object registered to this type.
        Type.from_string: parses a string representation of this type.

        Notes
        -----
        Flyweights are generated only once when they are first requested, and are
        reused for any subsequent calls with the same parameters.  This is accomplished
        by storing them in an LRU cache, which is unique to every type.  The cache
        size can be controlled via the `cache_size` metaclass argument, which defaults
        to None, signalling an infinite cache size.  If a finite cache is specified,
        then the number of flyweights will be capped at that value, and the least
        recently used flyweight will be evicted whenever the cache is full.

        This method is typically called by the `__class_getitem__()` method of a
        parametrized type, which is responsible for generating a new type with the
        specified parameters.  Invoking this method handles all the caching logic and
        instantiation details, and returns the resulting type to the caller.
        """
        raise NotImplementedError()

    def replace(cls, *args: Any, **kwargs: Any) -> META:
        """Replace certain parameters of a parametrized type.

        Parameters
        ----------
        *args: Any
            Positional arguments to supply to the type's constructor.  These are
            interpreted in the same order as :attr:`params <TypeMeta.params>`, and
            any missing arguments will be filled in with overrides from ``kwargs`` or
            their current values for this type.
        **kwargs: Any
            Keyword arguments to supply to the type's constructor.  These must be
            contained in :attr:`params <TypeMeta.params>`, and must not conflict with
            any positional arguments.  If a parameter is not specified and no
            positional override is given, then the current value for this type will be
            used instead.

        Returns
        -------
        META
            A newly-parametrized type with the specified configuration.

        Raises
        ------
        TypeError
            If the number of positional arguments does not match the signature of
            :meth:`__class_getitem__() <Type.__class_getitem__>`, or if any of the
            keyword arguments are not recognized.

        Notes
        -----
        This is a convenience method that allows users to modify a type without
        directly mutating it.

        If necessary, this method can be overridden on a descendant type to modify
        its behavior, though this is generally discouraged.  If this is done, then the
        descendant type should always call this method via ``super()`` to ensure that
        the default behavior is preserved.  Any extra logic should be limited to
        filtering the inputs to or output from this method before or after the call to
        ``super()``.

        Examples
        --------
        .. doctest::

            >>> PandasTimestamp["US/Pacific"]
            PandasTimestamp[US/Pacific]
            >>> PandasTimestamp["US/Pacific"].replace(None)
            PandasTimestamp[None]
            >>> PandasTimestamp["US/Pacific"].replace(tz=datetime.timezone.utc)
            PandasTimestamp[UTC]
        """
        raise NotImplementedError()

    ################################
    ####    CLASS DECORATORS    ####
    ################################

    @property
    def is_default(cls) -> META_RETURN[bool]:
        """Check whether the type is its own default implementation.

        Returns
        -------
        bool
            True if this type is concrete or abstract with no default implementation.
            False otherwise, signifying that it delegates attribute access to another
            type.

        See Also
        --------
        default: registers a default implementation for an abstract type.
        as_default: converts an abstract type to its default implementation.

        Examples
        --------
        .. doctest::

            >>> Int.is_default
            False
            >>> Signed.is_default
            False
            >>> Int64.is_default
            False
            >>> NumpyInt64.is_default
            True
        """
        raise NotImplementedError()

    @property
    def as_default(cls) -> META:
        """Convert an abstract type to its default implementation, if it has one.

        Returns
        -------
        META
            The default implementation of the type, if it has one.  Otherwise,
            returns the type itself.

        See Also
        --------
        default: registers a default implementation for an abstract type.

        Examples
        --------
        .. doctest::

            >>> Int.as_default
            Signed
            >>> Signed.as_default
            Int64
            >>> Int64.as_default
            NumpyInt64
            >>> NumpyInt64.as_default
            NumpyInt64
        """
        raise NotImplementedError()

    @property
    def as_nullable(cls) -> META:
        """Convert a concrete type to its nullable implementation, if it has one.

        Returns
        -------
        META
            The type itself if it is already nullable.  Otherwise, the nullable
            implementation that is registered to this type, if it has one.

        Raises
        ------
        TypeError
            If the type is marked as non-nullable and does not have a nullable
            implementation.

        See Also
        --------
        nullable: registers a nullable implementation for a concrete type.

        Examples
        --------
        .. doctest::

            >>> Int64["numpy"].as_nullable
            PandasInt64
            >>> PandasInt64.as_nullable
            PandasInt64
        """
        raise NotImplementedError()

    def default(cls, other: TypeMeta) -> TypeMeta:
        """A class decorator that registers a default implementation for an abstract
        type.

        Parameters
        ----------
        other: TypeMeta
            The type to delegate to when attribute lookups fail on the abstract
            type.  This type must be a subclass of the abstract type, and will
            be validated before being registered.  It can be either abstract or
            concrete, and may itself delegate to another type.

        Returns
        -------
        TypeMeta
            The type that was passed in, unmodified.

        Raises
        ------
        TypeError
            If the parent type is concrete, or if the decorated type is not a
            subclass of the parent.

        See Also
        --------
        as_default: converts an abstract type to its default implementation.

        Examples
        --------
        .. code-block:: python

            class Foo(Type):
                pass

            assert not hasattr(Foo, "x")

            @Foo.default
            class Bar(Foo):
                x = 1

            @Bar.default
            class Baz(Bar):
                y = 2

            assert Foo.x is Bar.x
            assert Foo.y is Baz.y
        """
        raise NotImplementedError()

    def nullable(cls, other: TypeMeta) -> TypeMeta:
        """A class decorator that registers an alternate, nullable implementation for
        a concrete type.

        Parameters
        ----------
        other: TypeMeta
            The type to delegate to when missing values are encountered for this
            type.  This will implicitly replace the original type during
            conversions when missing values are present.  It will be validated
            before being registered, and must be concrete.

        Returns
        -------
        TypeMeta
            The type that was passed in, unmodified.

        Raises
        ------
        TypeError
            If the parent type is abstract or already nullable, or if the decorated
            type is not concrete.

        See Also
        --------
        as_nullable: converts a concrete type to its nullable implementation.

        Examples
        --------
        In general, the structure of a nullable type is as follows:

        .. code-block:: python

            class Foo(Type):  # abstract parent type
                ...

            @Foo.default
            class Bar(Foo, backend="non_nullable"):  # non-nullable implementation
                ...

            @Bar.nullable
            class Baz(Foo, backend="nullable"):  # nullable equivalent
                ...

        With this structure in place, any conversion to ``Foo`` or ``Bar`` will 
        be automatically translated to ``Baz`` when missing values are present.

        A practical example of this can be seen with numpy integer types, which
        cannot store missing values by default.  Pandas implements a set of
        functionally identical extension types to cover this case, which are
        identical in every way except for their nullability.  As such, we can
        implicitly convert to the pandas type whenever missing values are
        encountered.

        .. doctest::

            >>> Int64["numpy"]([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: int64
            >>> Int64["numpy"]([1, 2, 3, None])
            0       1
            1       2
            2       3
            3    <NA>
            dtype: Int64
            >>> detect(_)
            Union[PandasInt64]
        """
        raise NotImplementedError()

    ####################
    ####    CORE    ####
    ####################

    @property
    def aliases(cls) -> META_RETURN[Aliases]:
        """A setlike collection of aliases for this type.

        Returns
        -------
        Aliases
            A mutable container that holds aliases for this type.  These implement a
            simplified set interface, and any changes are automatically reflected in
            the global registry.

        See Also
        --------
        TypeRegistry.aliases: the global registry of type aliases.

        Examples
        --------
        .. doctest::

            >>> Int.aliases
            Aliases('Int', 'int', 'integer')
            >>> Int.aliases.add("foo")
            >>> Int.aliases
            Aliases('Int', 'int', 'integer', 'foo')
            >>> resolve("foo")
            Int
            >>> Int.aliases.remove("foo")
            >>> Int.aliases
            Aliases('Int', 'int', 'integer')
            >>> resolve("foo")
            Traceback (most recent call last):
                ...
            TypeError: invalid specifier -> 'foo'
        """
        raise NotImplementedError()

    @property
    def scalar(cls) -> META_RETURN[type]:
        """The python type associated with a scalar value of this type.

        Returns
        -------
        type
            The python type to use for scalar values of this type.  This is identical
            to the result of the :func:`type` function for a single element of this
            type.

        Raises
        ------
        AttributeError
            If the type does not define a scalar type.  This can only occur for
            abstract types that do not default to a concrete implementation.

        See Also
        --------
        dtype: the numpy/pandas dtype associated with this type.

        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is defined in the absence of an explicit :attr:`dtype`, then
        a synthetic dtype will be produced automatically.  This is a special dtype that
        wraps around the scalar type and represents the contents of a vector as
        standard python objects.  It is functionally equivalent to numpy's ``object``
        dtype, but is more type safe, since it is labeled with an explicit python type.

        Examples
        --------
        .. doctest::

            >>> Int64.scalar
            <class 'numpy.int64'>
            >>> Int32.scalar
            <class 'numpy.int32'>
            >>> Int16.scalar
            <class 'numpy.int16'>
            >>> Int8.scalar
            <class 'numpy.int8'>
        """
        raise NotImplementedError()

    @property
    def dtype(cls) -> META_RETURN[DTYPE]:
        """The numpy/pandas dtype associated with vectors of this type.

        Returns
        -------
        DTYPE
            The numpy/pandas dtype to use for vectors of this type.

        Raises
        ------
        AttributeError
            If the type does not define a dtype.  This can only occur for abstract
            types that do not default to a concrete implementation.

        See Also
        --------
        scalar: the python type associated with a scalar value of this type.

        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is defined in the absence of an explicit :attr:`scalar` type,
        then the scalar type will be inferred automatically from ``dtype.type``.

        Examples
        --------
        .. doctest::

            >>> Int64.dtype
            dtype('int64')
            >>> Int32.dtype
            dtype('int32')
            >>> Int16.dtype
            dtype('int16')
            >>> Int8.dtype
            dtype('int8')
        """
        raise NotImplementedError()

    @property
    def itemsize(cls) -> META_RETURN[int]:
        """The size of a single element of this type, in bytes.

        Returns
        -------
        int
            The width of this type in memory.
    
        Raises
        ------
        AttributeError
            If the type does not define an itemsize.  This can only occur for abstract
            types that do not default to a concrete implementation.
    
        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is not provided, it will be inferred from ``dtype.itemsize``.

        Examples
        --------
        .. doctest::

            >>> Int8.itemsize
            1
            >>> Int16.itemsize
            2
            >>> Int32.itemsize
            4
            >>> Int64.itemsize
            8
        """
        raise NotImplementedError()

    @property
    def max(cls) -> META_RETURN[int | float]:
        """The maximum value that can be accurately represented by this type.

        Returns
        -------
        int | float
            An integer representing the maximum value that this type can store, or
            positive infinity if the type is unbounded.

        Raises
        ------
        AttributeError
            If the type does not define a maximum value.  This can only occur for
            abstract types that do not default to a concrete implementation.

        See Also
        --------
        min: the minimum value that can be accurately represented by this type.

        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is not provided, it defaults to positive infinity.

        .. note::

            For floating point and other real numeric types, this refers to the maximum
            value that can be stored with integer precision at exponent 1.  This
            typically reflects the size of the mantissa in the IEEE 754 specification,
            which is used to determine overflow when performing integer/float
            conversions.

            .. doctest::

                >>> Float64.max  # equal to 2**53, for a 53-bit mantissa
                9007199254740992

        Examples
        --------
        .. doctest::

            >>> Int64.max
            9223372036854775807
            >>> Int32.max
            2147483647
            >>> Int16.max
            32767
            >>> Int8.max
            127
        """
        raise NotImplementedError()

    @property
    def min(cls) -> META_RETURN[int | float]:
        """The minimum value that can be accurately represented by this type.

        Returns
        -------
        int | float
            An integer representing the minimum value that this type can store, or
            negative infinity if the type is unbounded.

        Raises
        ------
        AttributeError
            If the type does not define a minimum value.  This can only occur for
            abstract types that do not default to a concrete implementation.

        See Also
        --------
        max: the maximum value that can be accurately represented by this type.

        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is not provided, it defaults to negative infinity.

        .. note::

            For floating point and other real numeric types, this refers to the minimum
            value that can be stored with integer precision at exponent 1.  This
            typically reflects the size of the mantissa in the IEEE 754 specification,
            which is used to determine overflow when performing integer/float
            conversions.

            .. doctest::

                >>> Float64.min  # equal to -2**53, for a 53-bit mantissa
                -9007199254740992

        Examples
        --------
        .. doctest::

            >>> Int64.min
            -9223372036854775808
            >>> Int32.min
            -2147483648
            >>> Int16.min
            -32768
            >>> Int8.min
            -128
        """
        raise NotImplementedError()

    @property
    def is_nullable(cls) -> META_RETURN[bool]:
        """Indicates whether this type supports missing values.

        Returns
        -------
        bool
            True if the type can represent missing values.  False otherwise.

        Raises
        ------
        AttributeError
            If the type does not define a ``is_nullable`` attribute.

        See Also
        --------
        nullable: registers a nullable implementation for a concrete type.
        as_nullable: converts a concrete type to its nullable implementation.

        Examples
        --------
        .. doctest::

            >>> Int64["numpy"].is_nullable
            False
            >>> Int64["pandas"].is_nullable
            True
        """
        raise NotImplementedError()

    @property
    def missing(cls) -> META_RETURN[Any]:
        """The missing value to use for this type.

        Returns
        -------
        Any
            A special value to store in place of missing values.  This can be any
            scalar value that passes a :func:`pandas.isna` check.

        Raises
        ------
        AttributeError
            If the type does not define a missing value.  This can only occur for
            abstract types that do not default to a concrete implementation.

        See Also
        --------
        is_nullable: a boolean flag indicating whether this type is nullable.
        nullable: registers a nullable implementation for a concrete type.
        as_nullable: converts a concrete type to its nullable implementation.

        Notes
        -----
        This is a required field for all concrete types, as enforced by the metaclass
        machinery.  It is not required for abstract types, since they do not have a
        concrete implementation by default.

        If this attribute is not provided, it defaults to :attr:`pandas.NA`.

        Examples
        --------
        .. doctest::

            >>> Int64.missing
            <NA>
            >>> Float64.missing
            nan
            >>> Complex128.missing
            (nan+nanj)
        """
        raise NotImplementedError()

    @property
    def slug(cls) -> META_RETURN[str]:
        """Get a string identifier for this type, as used to look up flyweights.

        Returns
        -------
        str
            A string concatenating the type's class name with its parameter
            configuration, if any.

        Notes
        -----
        This is identical to the output of the :func:`str` and :func:`repr` built-in
        functions on the type.

        Examples
        --------
        .. doctest::

            >>> Int.slug
            'Int'
            >>> PandasTimestamp["UTC"].slug
            'PandasTimestamp[UTC]'
            >>> Sparse[Int].slug
            'Sparse[Int, <NA>]'
        """
        raise NotImplementedError()

    @property
    def hash(cls) -> META_RETURN[int]:
        """Get a precomputed hash for this type.

        Returns
        -------
        int
            A unique hash based on the type's string identifier.

        Notes
        -----
        This is identical to the output of the :func:`hash` built-in function.
        """
        raise NotImplementedError()

    @property
    def backend(cls) -> META_RETURN[str]:
        """Get a concrete type's backend specifier.

        Returns
        -------
        str
            A string identifying the backend that this type is registered to.
            This is used to search an abstract parent's implementation
            dictionary, allowing the concrete type to be resolved via
            parametrization.

        Examples
        --------
        .. code-block:: python

            class Foo(Type):
                pass

            class Bar(Foo, backend="bar"):
                # required fields, etc.
                ...

            assert Foo["bar"] is Bar
            assert Bar.backend == "bar"
        """
        raise NotImplementedError()

    @property
    def is_abstract(cls) -> META_RETURN[bool]:
        """Check whether the type is abstract.

        Returns
        -------
        bool
            True if the type is abstract (i.e. :attr:`backend` is set to the empty
            string).  False otherwise.

        See Also
        --------
        backend: the type's backend specifier.

        Examples
        --------
        .. doctest::

            >>> Int.is_abstract
            True
            >>> Signed.is_abstract
            True
            >>> Int64.is_abstract
            True
            >>> NumpyInt64.is_abstract
            False
        """
        raise NotImplementedError()

    #########################
    ####    TRAVERSAL    ####
    #########################

    @property
    def is_root(cls) -> META_RETURN[bool]:
        """Check whether the type is the root of its type hierarchy.

        Returns
        -------
        bool
            True if the type has no parent.  False otherwise.

        Notes
        -----
        A root is any type that directly inherits from the generic :class:`Type`
        class.

        Examples
        --------
        .. doctest::

            >>> Int.is_root
            True
            >>> Signed.is_root
            False
            >>> Int64.is_root
            False
            >>> NumpyInt64.is_root
            False
        """
        raise NotImplementedError()

    @property
    def root(cls) -> META:
        """Get the root of this type's hierarchy.

        Returns
        -------
        META
            The root type that this type ultimately inherits from.  This can be
            a self-reference if :attr:`is_root` is True.

        See Also
        --------
        parent: the immediate parent of this type.

        Examples
        --------
        .. doctest::

            >>> Int.root
            Int
            >>> Signed.root
            Int
            >>> Int64.root
            Int
            >>> NumpyInt64.root
            Int
        """
        raise NotImplementedError()

    @property
    def parent(cls) -> META:
        """Get the immediate parent of this type within the hierarchy.

        Returns
        -------
        META
            The type that this type directly inherits from or a self reference if
            :attr:`is_root` is True.

        See Also
        --------
        root: the root of this type's hierarchy.

        Examples
        --------
        .. doctest::

            >>> Int.parent
            None
            >>> Signed.parent
            Int
            >>> Int64.parent
            Signed
            >>> NumpyInt64.parent
            Int64
        """
        raise NotImplementedError()

    @property
    def subtypes(cls) -> UnionMeta:
        """A union containing the abstract subtypes of this type.

        Returns
        -------
        UnionMeta
            A union containing all of the abstract children of this type.  This
            does not include concrete implementations of this type, which are
            stored in a separate attribute.

        See Also
        --------
        implementations: a union containing the concrete implementations of this type.
        children: a union containing the immediate children of this type.

        Examples
        --------
        .. doctest::

            >>> Int.subtypes
            Union[Signed, Unsigned]
            >>> Signed.subtypes
            Union[Int64, Int32, Int16, Int8]
            >>> Int64.subtypes
            Union[]
            >>> NumpyInt64.subtypes
            Union[]
        """
        raise NotImplementedError()

    @property
    def implementations(cls) -> UnionMeta:
        """A union containing the concrete implementations of this type.

        Returns
        -------
        UnionMeta
            A union containing all of the concrete implementations of this type.
            This does not include abstract subtypes of this type, which are stored
            in a separate attribute.

        See Also
        --------
        subtypes: a union containing the abstract subtypes of this type.
        children: a union containing the immediate children of this type.

        Examples
        --------
        .. doctest::

            >>> Int.implementations
            Union[]
            >>> Signed.implementations
            Union[PythonInt]
            >>> Int64.implementations
            Union[NumpyInt64, PandasInt64]
            >>> NumpyInt64.implementations
            Union[]
        """
        raise NotImplementedError()

    @property
    def children(cls) -> UnionMeta:
        """A union containing all the children of this type.

        Returns
        -------
        UnionMeta
            A union containing all descendants of this type in the hierarchy,
            including grandchildren, great-grandchildren, etc.

        See Also
        --------
        leaves: a union containing all the leaves of this type.

        Notes
        -----
        Children are always returned in definition order (i.e. the order in which they
        were registered).

        Examples
        --------
        .. doctest::

            >>> Int.children
            Union[Int, Signed, Int64, NumpyInt64, PandasInt64, Int32, NumpyInt32, PandasInt32, Int16, NumpyInt16, PandasInt16, Int8, NumpyInt8, PandasInt8, PythonInt]
            >>> Signed.children
            Union[Signed, Int64, NumpyInt64, PandasInt64, Int32, NumpyInt32, PandasInt32, Int16, NumpyInt16, PandasInt16, Int8, NumpyInt8, PandasInt8, PythonInt]
            >>> Int64.children
            Union[Int64, NumpyInt64, PandasInt64]
            >>> NumpyInt64.children
            Union[NumpyInt64]
        """
        raise NotImplementedError()

    @property
    def is_leaf(cls) -> META_RETURN[bool]:
        """Check whether the type is a leaf of its type hierarchy.

        Returns
        -------
        bool
            True if the type has no immediate children.  False otherwise.

        Examples
        --------
        .. doctest::

            >>> Int.is_leaf
            False
            >>> Signed.is_leaf
            False
            >>> Int64.is_leaf
            False
            >>> NumpyInt64.is_leaf
            True
        """
        raise NotImplementedError()

    @property
    def leaves(cls) -> UnionMeta:
        """A union containing all the leaves contained in this type.

        Returns
        -------
        UnionMeta
            A union containing all descendants of this type that are leaves in
            the hierarchy, including grandchildren, great-grandchildren, etc.
            This is identical to :attr:`children`, except that it excludes any
            abstract types.

        See Also
        --------
        children: a union containing all the children of this type.

        Note
        ----
        Leaves are always returned in definition order (i.e. the order in which they
        were registered).

        Examples
        --------
        .. doctest::

            >>> Int.leaves
            Union[NumpyInt64, PandasInt64, NumpyInt32, PandasInt32, NumpyInt16, PandasInt16, NumpyInt8, PandasInt8, PythonInt]
            >>> Signed.leaves
            Union[NumpyInt64, PandasInt64, NumpyInt32, PandasInt32, NumpyInt16, PandasInt16, NumpyInt8, PandasInt8, PythonInt]
            >>> Int64.leaves
            Union[NumpyInt64, PandasInt64]
            >>> NumpyInt64.leaves
            Union[NumpyInt64]
        """
        raise NotImplementedError()

    @property
    def larger(cls) -> UnionMeta:
        """A union containing all the sibling types that have a larger representable
        range than this type.

        Returns
        -------
        UnionMeta
            A union containing all sibling types (leaves within the same hierarchy)
            that compare greater than this type.  This is always returned in sorted
            order, with the smallest type first.

        See Also
        --------
        smaller: a union containing all the sibling types that are less than this type.

        Examples
        --------
        .. doctest::

            >>> Int32.larger
            Union[NumpyUInt32, NumpyInt64, NumpyUInt64, PythonInt]
            >>> Int64.larger
            Union[NumpyUInt64, PythonInt]

        This can also be used to dynamically upcast input data to the smallest type
        that can fully represent it.  For instance, by forming a union with the type
        itself, we can ensure that the input data is always converted to the smallest
        option.

        .. doctest::

            >>> Int64 | Int64.larger
            Union[Int32, NumpyUInt32, NumpyInt64, NumpyUInt64, PythonInt]
            >>> (Int64 | Int64.larger)([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: int64
            >>> (Int64 | Int64.larger)([1, 2, 3, 2**64 - 1])
            0                      1
            1                      2
            2                      3
            3    9223372036854775807
            dtype: uint64
            >>> (Int64 | Int64.larger)([1, 2, 3, 2**80 - 1])
            0                            1
            1                            2
            2                            3
            3    1208925819614629174706175
            dtype: PythonInt
        """
        raise NotImplementedError()

    @property
    def smaller(cls) -> UnionMeta:
        """A union containing all the sibling types that have a smaller representable
        range than this type.

        Returns
        -------
        UnionMeta
            A union containing all sibling types (leaves within the same hierarchy)
            that compare less than this type.  This is always returned in sorted
            order, with the smallest type first.

        See Also
        --------
        larger: a union containing all the sibling types that are greater than this type.

        Examples
        --------
        .. doctest::

            >>> Int32.smaller
            Union[NumpyInt8, NumpyUInt8, NumpyInt16, NumpyUInt16]
            >>> Int64.smaller
            Union[NumpyInt8, NumpyUInt8, NumpyInt16, NumpyUInt16, NumpyInt32, NumpyUInt32]

        This can also be used to dynamically downcast input data to the smallest type
        that can fully represent it.  For instance, by forming a union with the type
        itself, we can ensure that the input data is always converted to the smallest
        option.

        .. doctest::

            >>> Int32.smaller | Int32
            Union[NumpyInt8, NumpyUInt8, NumpyInt16, NumpyUInt16, Int32]
            >>> (Int32.smaller | Int32)([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: int8
            >>> (Int32.smaller | Int32)([1, 2, 3, 2**15 - 1])
            0        1
            1        2
            2    32767
            dtype: int16
            >>> (Int32.smaller | Int32)([1, 2, 3, 2**31 - 1])
            0             1
            1             2
            2    2147483647
            dtype: int32
        """
        raise NotImplementedError()

    ###########################
    ####    DECORATORS     ####
    ###########################

    @property
    def is_decorator(cls) -> META_RETURN[bool]:
        """Check whether the type is a decorator type.

        Returns
        -------
        bool
            True if the type is a decorator type.  False otherwise.

        Examples
        --------
        .. doctest::

            >>> Int.is_decorated
            False
            >>> Sparse[Int].is_decorated
            True
        """
        raise NotImplementedError()

    @property
    def decorators(cls) -> UnionMeta:
        """A union containing all the decorators applied to this type.

        Returns
        -------
        UnionMeta
            A union containing all the decorators that are attached to this type.

        See Also
        --------
        wrap: convert the outermost decorator into its unparametrized base type.
        wrapped: the type that this type is decorating.
        unwrapped: the innermost non-decorator type.

        Notes
        -----
        This is always empty in the case of scalar (undecorated) types.

        Examples
        --------
        .. doctest::

            >>> Int.decorators
            Union[]
            >>> Sparse[Int].decorators
            Union[Sparse[Int, <NA>]]
            >>> Categorical[Sparse[Int]].decorators
            Union[Categorical[Sparse[Int, <NA>], ...], Sparse[Int, <NA>]]
        """
        raise NotImplementedError()

    @property
    def wrapped(cls) -> META:  # pylint: disable=redundant-returns-doc
        """Return the type that this type is decorating.

        Returns
        -------
        META
            If the type is a parametrized decorator, then the type that it wraps.
            Otherwise, a self reference to the current type.

        See Also
        --------
        decorators: a union containing all the decorators that are attached to this type.
        wrap: convert the outermost decorator into its unparametrized base type.
        unwrapped: the innermost non-decorator type.

        Examples
        --------
        .. doctest::

            >>> Int.wrapped
            None
            >>> Sparse[Int].wrapped
            Int
            >>> Categorical[Sparse[Int]].wrapped
            Sparse[Int]
        """
        raise NotImplementedError()

    @property
    def unwrapped(cls) -> META:
        """Return the innermost non-decorator type in the stack.

        Returns
        -------
        META
            The innermost type in the stack that is not a parametrized decorator, or a
            self-reference if the type is not decorated.

        See Also
        --------
        decorators: a union containing all the decorators that are attached to this type.
        wrap: convert the outermost decorator into its unparametrized base type.
        wrapped: the type that this type is decorating.

        Examples
        --------
        .. doctest::

            >>> Int.unwrapped
            Int
            >>> Sparse[Int].unwrapped
            Int
            >>> Categorical[Sparse[Int]].unwrapped
            Int
        """
        raise NotImplementedError()

    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Apply the decorator to a series of the wrapped type.

        Parameters
        ----------
        series : pd.Series[Any]
            The series to transform.  This will always be of the type that the
            decorator wraps.

        Returns
        -------
        pd.Series
            The transformed series, which should reflect the decorated type.

        See Also
        --------
        inverse_transform : Remove the decorator from a series of the wrapped type.

        Notes
        -----
        Conversions involving decorators are always handled from the inside out,
        meaning that the input will first be converted to the wrapped type and then
        transformed into the final result via this method.  This allows the user to
        ignore the multivariate nature of the decorator and its corresponding
        complexity in the conversion logic.

        The default implementation of this method simply casts the series to the
        decorator's :attr:`dtype <TypeMeta.dtype>`.  If the series is already of that
        type, then this method is a no-op.  This is the simplest (and most efficient)
        way to apply a decorator, but users can override this method to provide more
        sophisticated logic if necessary.

        Examples
        --------
        .. doctest::

            >>> Int([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: int64
            >>> Sparse[Int].transform(_)
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]
            >>> Sparse[Int]([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]
        """
        raise NotImplementedError()

    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Remove the decorator from a series of the wrapped type.

        Parameters
        ----------
        series : pd.Series[Any]
            The series to transform.  This will always be of the decorator's type.

        Returns
        -------
        pd.Series
            The transformed series, which should reflect the type that the decorator
            wraps.

        See Also
        --------
        transform : Apply the decorator to a series of the wrapped type.

        Notes
        -----
        Conversions involving decorators are always handled from the inside out,
        meaning that input decorators will be unwrapped using this method before
        converting to the final result.  This allows the user to ignore the
        multivariate nature of the decorator and its corresponding complexity in the
        conversion logic.

        The default implementation of this method simply casts the series to the
        wrapped :attr:`dtype <TypeMeta.dtype>`.  If the series is already of that type,
        then this method is a no-op.  This is the simplest (and most efficient) way to
        remove a decorator, but users can override this method to provide more
        sophisticated logic if necessary.

        Examples
        --------
        .. doctest::

            >>> Sparse[Int]([1, 2, 3])
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]
            >>> Sparse[Int].inverse_transform(_)
            0    1
            1    2
            2    3
            dtype: int64
            >>> Int(Sparse[Int]([1, 2, 3]))
            0    1
            1    2
            2    3
            dtype: int64
        """
        raise NotImplementedError()

    #####################
    ####    UNION    ####
    #####################

    @property
    def is_union(cls) -> bool:
        """Indicates whether this type is a union.

        Returns
        -------
        bool
            True if the type is a union.  Always False for scalar types.

        See Also
        --------
        is_structured: indicates whether this type is a structured union.

        Examples
        --------
        .. doctest::

            >>> Int.is_union
            False
            >>> Union[Int, Float].is_union
            True
            >>> Union["foo": Int, "bar": Float].is_union
            True
        """
        return False

    @property
    def is_structured(cls) -> bool:
        """Indicates whether this type is a structured union.

        Returns
        -------
        bool
            True if the type is a structured union.  Always False for scalar types.

        See Also
        --------
        is_union: indicates whether this type is a union.

        Examples
        --------
        .. doctest::

            >>> Int.is_structured
            False
            >>> Union[Int, Float].is_structured
            False
            >>> Union["foo": Int, "bar": Float].is_structured
            True
        """
        return False

    @property
    def index(cls) -> OBJECT_ARRAY | None:
        """A 1D numpy array containing the observed type at every index of a sequence
        passed to :func:`detect`.

        Returns
        -------
        OBJECT_ARRAY | None
            A numpy array with the same shape as the input sequence, where each element
            is a bertrand type corresponding to the inferred type at that index.  If
            the type was generated by any other means, then this will be None.

        See Also
        --------
        detect: infer the type of a given object or container.

        Notes
        -----
        This attribute is stored internally as a run-length encoded array, which
        compresses runs of a single type into a single entry.  When the array is
        accessed, it is automatically expanded back into its original form.

        Examples
        --------
        .. doctest::

            >>> detect([1, 2, 3]).index
            array([PythonInt, PythonInt, PythonInt], dtype=object)
            >>> detect(pd.DataFrame({"foo": [1, 2, 3], "bar": [4., 5., 6.]})).index
            array([(NumpyInt64, NumpyFloat64), (NumpyInt64, NumpyFloat64),
                   (NumpyInt64, NumpyFloat64)], dtype=[('foo', 'O'), ('bar', 'O')])
        """
        return None

    def collapse(cls) -> UnionMeta:
        """Remove any redundant types from the union.

        Returns
        -------
        UnionMeta
            A new union containing only those types that are not contained by any other
            type in the union.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int8["numpy"], Int8["pandas"]]
            Union[Int8, NumpyInt8, PandasInt8]
            >>> _.collapse()
            Union[Int8]
        """
        return Union.from_types(LinkedSet([cls]))

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> UnionMeta:
        """Return a new union with types sorted according to an optional key function.

        Parameters
        ----------
        key: Callable[[TypeMeta], Any], optional
            A function that takes a type and returns a value to use for sorting.
            Defaults to normal ``<`` comparisons if not specified.
        reverse: bool, default False
            If True, sort the types in descending order.

        Returns
        -------
        UnionMeta
            A new union with the types sorted according to the specified key function.

        See Also
        --------
        TypeRegistry.edges: overrides for the default comparison operators.

        Examples
        --------
        .. doctest::

            >>> Union[Int16, Int32, Int8].sorted()
            Union[Int8, Int16, Int32]
            >>> Union[Int16, Int32, Int8].sorted(reverse=True)
            Union[Int32, Int16, Int8]
            >>> Union[Int16, Int32, Int8].sorted(key=lambda t: t.itemsize)
            Union[Int8, Int16, Int32]
        """
        return Union.from_types(LinkedSet([cls]))

    def issubset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        """Check whether all types in this union are also in another type.

        Parameters
        ----------
        other: TYPESPEC | Iterable[TYPESPEC]
            The type(s) to check against.  These will be processed according to the
            :func:`resolve` function.
        strict: bool, default False
            If True, then the comparison type must contain at least one type that is
            not in the union.

        Returns
        -------
        bool
            True if all types in this union are also in the comparison type, accounting
            for strictness.

        See Also
        --------
        issuperset: check whether all types in another type are also in this union.
        isdisjoint: check whether this union has no types in common with another type.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].issubset(Int)
            True
            >>> Union[Int8, Int16].issubset(Int8 | Int16)
            True
            >>> Union[Int8, Int16].issubset(Int8 | Int16, strict=True)
            False
        """
        typ = resolve(other)
        if cls is typ:
            return not strict

        return (
            issubclass(cls, typ) and
            not strict or len(cls.children) < len(typ.children)
        )

    def issuperset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        """Check whether all types in another type are also in this union.

        Parameters
        ----------
        other: TYPESPEC | Iterable[TYPESPEC]
            The type(s) to check against.  These will be processed according to the
            :func:`resolve` function.
        strict: bool, default False
            If True, then this union must contain at least one type that is not in the
            comparison type.

        Returns
        -------
        bool
            True if all types in the comparison type are also in this union, accounting
            for strictness.

        See Also
        --------
        issubset: check whether all types in this union are also in another type.
        isdisjoint: check whether this union has no types in common with another type.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].issuperset(Int8)
            True
            >>> Union[Int8, Int16].issuperset(Int8 | Int16)
            True
            >>> Union[Int8, Int16].issuperset(Int8 | Int16, strict=True)
            False
        """
        typ = resolve(other)
        if cls is typ:
            return not strict

        return (
            issubclass(typ, cls) and
            not strict or len(cls.children) > len(typ.children)
        )

    def isdisjoint(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        """Check whether this union has no types in common with another type.

        Parameters
        ----------
        other: TYPESPEC | Iterable[TYPESPEC]
            The type(s) to check against.  These will be processed according to the
            :func:`resolve` function.

        Returns
        -------
        bool
            True if this union has no types in common with the comparison type.

        See Also
        --------
        issubset: check whether all types in this union are also in another type.
        issuperset: check whether all types in another type are also in this union.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].isdisjoint(Int32 | Int64)
            True
            >>> Union[Int8, Int16].isdisjoint(Int16 | Int32)
            False
        """
        typ = resolve(other)
        if cls is typ:
            return False

        return not (issubclass(typ, cls) or issubclass(cls, typ))

    def keys(cls) -> KeysView[str]:
        """Return the column names of a structured union.

        Returns
        -------
        KeysView[str]
            The name of each column in a structured union.  If the union is not
            structured, then an empty view is returned.

        See Also
        --------
        values: return the types of each column in a structured union.
        items: return the column names and types of a structured union.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].keys()
            dict_keys([])
            >>> Union["foo": Int8 | Int16].keys()
            dict_keys(['foo'])
        """
        return {}.keys()

    def values(cls) -> ValuesView[META]:
        """Return the type of each column in a structured union.

        Returns
        -------
        ValuesView[META]
            The type of each column in a structured union.  These can include any
            bertrand type object besides another structured union.  If the original
            union is not structured, then an empty view is returned.

        See Also
        --------
        keys: return the column names of a structured union.
        items: return the column names and types of a structured union.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].values()
            dict_values([])
            >>> Union["foo": Int8 | Int16].values()
            dict_values([Union[Int8, Int16]])
        """
        return {}.values()

    def items(cls) -> ItemsView[str, META]:
        """Return the column name and type of each column in a structured union.

        Returns
        -------
        ItemsView[str, META]
            A sequence containing tuples of length 2, where the first element is the
            column name and the second element is the type of that column.  If the
            original union is not structured, then an empty view is returned.

        See Also
        --------
        keys: return the column names of a structured union.
        values: return the types of each column in a structured union.

        Examples
        --------
        .. doctest::

            >>> Union[Int8, Int16].items()
            dict_items([])
            >>> Union["foo": Int8 | Int16].items()
            dict_items([('foo', Union[Int8, Int16])])
        """
        return {}.items()

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any] | pd.DataFrame:
        raise NotImplementedError()

    def __setattr__(cls, name: str, val: Any) -> None:
        if name in {"_parent", "_base_type", "_default", "_nullable"}:
            super().__setattr__(name, val)
        else:
            raise TypeError("bertrand types are immutable")

    def __hash__(cls) -> int:
        raise NotImplementedError()

    def __instancecheck__(cls, other: Any | Iterable[Any]) -> bool:
        return cls.__subclasscheck__(detect(other))

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        raise NotImplementedError()

    def __contains__(cls, other: Any | TYPESPEC | Iterable[Any | TYPESPEC]) -> bool:
        try:
            return cls.__subclasscheck__(other)
        except TypeError:
            return cls.__instancecheck__(other)

    def __len__(cls) -> int:
        raise NotImplementedError()

    def __bool__(cls) -> bool:
        raise NotImplementedError()

    def __iter__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        raise NotImplementedError()

    def __reversed__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        raise NotImplementedError()

    def __or__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, UnionMeta):
            result = LinkedSet((cls,))
            result.update(typ)
            return Union.from_types(result)

        return Union.from_types(LinkedSet((cls, typ)))

    def __ror__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return typ | cls

    def __and__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) & typ

    def __rand__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return typ & cls

    def __sub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) - typ

    def __rsub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return typ - cls

    def __xor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) ^ typ

    def __rxor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return typ ^ cls

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        raise NotImplementedError()

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        raise NotImplementedError()

    def __eq__(cls, other: Any) -> bool:
        raise NotImplementedError()

    def __ne__(cls, other: Any) -> bool:
        raise NotImplementedError()

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        raise NotImplementedError()

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        raise NotImplementedError()

    def __str__(cls) -> str:
        raise NotImplementedError()

    def __repr__(cls) -> str:
        raise NotImplementedError()


@_copy_docs
class TypeMeta(BaseMeta):
    """Metaclass for all scalar bertrand types (those that inherit from bertrand.Type).

    This metaclass is responsible for parsing the namespace of a new type, validating
    its configuration, and adding it to the global type registry.  It also defines the
    basic behavior of all bertrand types, including the establishment of hierarchical
    relationships, parametrization, and operator overloading.

    See the documentation for the `Type` class for more information on how these work.
    """
    # pylint: disable=missing-return-doc, no-value-for-parameter

    _slug: str
    _hash: int
    _required: dict[str, Callable[..., Any]]
    _fields: dict[str, Any]
    _parent: TypeMeta
    _base_type: TypeMeta
    _children: LinkedSet[TypeMeta]
    _subtypes: LinkedSet[TypeMeta]
    _implementations: LinkedDict[str, TypeMeta]
    _default: TypeMeta
    _nullable: TypeMeta | None
    _cache_size: int | None
    _flyweights: LinkedDict[str, TypeMeta]
    _params: tuple[str]
    _parametrized: bool
    _defines_from_scalar: bool
    _defines_from_dtype: bool

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(
        cls: TypeMeta,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        **kwargs: Any
    ):
        if DEBUG:
            fields = {k: "..." for k in cls._required} | cls._fields
            if not (len(bases) == 0 or bases[0] is Base):
                print("-" * 80)
                print(f"slug: {cls.slug}")
                print(f"parent: {cls.parent}")
                print(f"aliases: {cls.aliases}")
                print(f"fields: {_format_dict(fields)}")
                print(f"is_root: {cls.is_root}")
                print(f"is_abstract: {cls.is_abstract}")
                print(f"is_leaf: {cls.is_leaf}")
                print(f"cache_size: {cls.cache_size}")
                print(f"itemsize: {getattr(cls, 'itemsize', '...')}")
                print()

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str = "",
        cache_size: int | None = None,
    ) -> TypeMeta:
        if not backend:
            abstract = AbstractBuilder(name, bases, namespace, cache_size)
            return abstract.parse().fill().register(BaseMeta.__new__(
                mcs,
                abstract.name,
                (abstract.parent,),
                abstract.namespace
            ))

        concrete = ConcreteBuilder(name, bases, namespace, backend, cache_size)
        return concrete.parse().fill().register(BaseMeta.__new__(
            mcs,
            concrete.name,
            (concrete.parent,),
            concrete.namespace
        ))

    def __getitem__(cls, params: Any | tuple[Any, ...]) -> TypeMeta:
        if isinstance(params, tuple):
            return cls.__class_getitem__(*params)
        return cls.__class_getitem__(params)

    # NOTE: builders always override these methods during type instantiation, so the
    # versions provided here will never actually be called in practice.

    def from_scalar(cls, scalar: Any) -> TypeMeta:
        raise NotImplementedError()

    def from_dtype(cls, dtype: DTYPE) -> TypeMeta:
        raise NotImplementedError()

    def from_string(cls, *args: str) -> TypeMeta:
        raise NotImplementedError()

    ##########################
    ####    FLYWEIGHTS    ####
    ##########################

    @property
    def is_flyweight(cls) -> bool:
        return cls._parametrized

    @property
    def flyweights(cls) -> Mapping[str, TypeMeta]:
        return MappingProxyType(cls._flyweights)

    @property
    def base_type(cls) -> TypeMeta:
        return cls._base_type

    @property
    def cache_size(cls) -> int | None:
        return cls._cache_size

    @property
    def params(cls) -> Mapping[str, Any]:
        return MappingProxyType({k: getattr(cls, k) for k in cls._params})

    def flyweight(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls._flyweights.lru_get(slug, None)

        if typ is None:
            if len(args) != len(cls._params):
                raise TypeError(
                    f"expected {len(cls._params)} positional arguments, got "
                    f"{len(args)}"
                )

            typ = BaseMeta.__new__(
                TypeMeta,
                cls.__name__,
                (cls,),
                {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,
                    "_base_type": cls,
                    "_parametrized": True,
                    "__class_getitem__": _parametrized_getitem,
                } | kwargs  # override non-required class variables
            )
            cls._flyweights.lru_add(slug, typ)

        return typ

    def replace(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        raise NotImplementedError()  # provided in Type

    ################################
    ####    CLASS DECORATORS    ####
    ################################

    @property
    def is_default(cls) -> bool:
        return cls is cls._default

    @property
    def as_default(cls) -> TypeMeta:
        return cls._default

    @property
    def as_nullable(cls) -> TypeMeta:
        if cls._nullable is None:
            if not cls.is_default:
                return cls.as_default.as_nullable

            raise TypeError(f"type is not nullable -> {cls.slug}")

        return cls._nullable

    def default(cls, other: TypeMeta) -> TypeMeta:
        if not cls.is_abstract:
            raise TypeError("concrete types cannot have default implementations")

        if not issubclass(other, cls):
            raise TypeError(
                f"default implementation must be a subclass of {cls.__name__}"
            )

        cls._default = other
        return other

    def nullable(cls, other: TypeMeta) -> TypeMeta:
        if cls.is_abstract:  # pylint: disable=using-constant-test
            raise TypeError(
                f"abstract types cannot have nullable implementations -> {cls.slug}"
            )
        if cls.is_nullable:
            raise TypeError(f"type is already nullable -> {cls.slug}")

        if other.is_abstract:
            raise TypeError(
                f"nullable implementations must be concrete -> {other.slug}"
            )

        cls._nullable = other
        return other

    ####################
    ####    CORE    ####
    ####################

    @property
    def aliases(cls) -> Aliases:
        return cls._aliases

    @property
    def scalar(cls) -> type:
        return cls.__getattr__("scalar")

    @property
    def dtype(cls) -> DTYPE:
        return cls.__getattr__("dtype")

    @property
    def itemsize(cls) -> int:
        return cls.__getattr__("itemsize")

    @property
    def max(cls) -> int | float:
        return cls.__getattr__("max")

    @property
    def min(cls) -> int | float:
        return cls.__getattr__("min")

    @property
    def is_nullable(cls) -> bool:
        return cls.__getattr__("is_nullable")

    @property
    def missing(cls) -> Any:
        return cls.__getattr__("missing")

    @property
    def slug(cls) -> str:
        return cls._slug

    @property
    def hash(cls) -> int:
        return cls._hash

    @property
    def backend(cls) -> str:
        return cls._fields["backend"]

    @property
    def is_abstract(cls) -> bool:
        return not cls.backend

    #########################
    ####    TRAVERSAL    ####
    #########################

    @property
    def is_root(cls) -> bool:
        return cls is cls._parent

    @property
    def root(cls) -> TypeMeta:
        result = cls
        while not result.is_root:
            result = result.parent
        return result

    @property
    def parent(cls) -> TypeMeta:
        return cls._parent

    @property
    def subtypes(cls) -> UnionMeta:
        return Union.from_types(cls._subtypes)

    @property
    def implementations(cls) -> UnionMeta:
        return Union.from_types(LinkedSet(cls._implementations.values()))

    @property
    def children(cls) -> UnionMeta:
        return Union.from_types(cls._children)

    @property
    def is_leaf(cls) -> bool:
        return not cls._subtypes and not cls._implementations

    @property
    def leaves(cls) -> UnionMeta:
        return Union.from_types(LinkedSet(t for t in cls._children if t.is_leaf))

    @property
    def larger(cls) -> UnionMeta:
        features = lambda t: (t.max - t.min, t.itemsize, abs(t.max + t.min))
        fts = features(cls)
        types = sorted(t for t in cls.root.leaves if t > cls and features(t) != fts)
        result = LinkedSet()
        for t in types:
            if result:
                tail = result[-1]
                if features(t) == features(tail):
                    if t.parent is tail.parent and t is t.parent.as_default:
                        result.pop()
                    else:
                        continue
            result.add(t)

        return Union.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        features = lambda t: (t.max - t.min, t.itemsize, abs(t.max + t.min))
        feats = features(cls)
        types = sorted(t for t in cls.root.leaves if t < cls and features(t) != feats)
        result = LinkedSet()
        for t in types:
            if result:
                tail = result[-1]
                if features(t) == features(tail):
                    if t.parent is tail.parent and t is t.parent.as_default:
                        result.pop()
                    else:
                        continue
            result.add(t)

        return Union.from_types(result)

    ##########################
    ####    DECORATORS    ####
    ##########################

    @property
    def is_decorator(cls) -> bool:
        return False

    @property
    def decorators(cls) -> UnionMeta:
        return Union.from_types(LinkedSet())

    @property
    def wrapped(cls) -> TypeMeta:
        return cls

    @property
    def unwrapped(cls) -> TypeMeta:
        return cls

    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        return series

    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        return series

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls is cls._default:
            return cls.as_default(*args, **kwargs)
        return pd.Series(*args, dtype=cls.dtype, **kwargs)  # type: ignore

    def __getattr__(cls, name: str) -> Any:
        if cls is not cls._default:
            return getattr(cls.as_default, name)

        if name in cls._fields:
            return cls._fields[name]

        raise _no_attribute(cls, name)

    def __dir__(cls) -> set[str]:
        result = set(dir(TypeMeta))
        result.update(cls._fields)
        end = cls
        while not end.is_default:
            end = end.as_default
            result.update(end._fields)
        return result

    def __hash__(cls) -> int:
        return cls._hash

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        check = super(BaseMeta, cls).__subclasscheck__
        if isinstance(typ, UnionMeta):
            return all(check(t) for t in typ)
        return check(typ)

    def __len__(cls) -> int:
        return len(cls.children)

    def __bool__(cls) -> bool:
        return True

    def __iter__(cls) -> Iterator[TypeMeta]:
        return iter(cls.children)  # type: ignore

    def __reversed__(cls) -> Iterator[TypeMeta]:
        # pylint: disable=bad-reversed-sequence
        return reversed(cls.children)  # type: ignore

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls < t for t in typ)

        edge = _has_edge(cls.base_type, typ.unwrapped.base_type)  # type: ignore
        if edge is not None:
            return edge

        return _features(cls) < _features(typ)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls <= t for t in typ)

        edge = _has_edge(cls.base_type, typ.unwrapped.base_type)  # type: ignore
        if edge is not None:
            return edge

        return _features(cls) <= _features(typ)

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls is t for t in typ)

        return cls is typ

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls >= t for t in typ)

        edge = _has_edge(cls.base_type, typ.unwrapped.base_type)  # type: ignore
        if edge is not None:
            return not edge

        return _features(cls) >= _features(typ)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls > t for t in typ)

        edge = _has_edge(cls.base_type, typ.unwrapped.base_type)  # type: ignore
        if edge is not None:
            return not edge

        return _features(cls) > _features(typ)

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


@_copy_docs
class DecoratorMeta(BaseMeta):
    """Metaclass for all bertrand type decorators (those that inherit from
    bertrand.DecoratorType).

    This metaclass is responsible for parsing the namespace of a new decorator,
    validating its configuration, and registering it in the global type registry.  It
    also defines the basic behavior of all bertrand type decorators, including a
    mechanism for applying/removing the decorator, traversing a type's decorator stack,
    and allowing pass-through access to the wrapped type's attributes and methods.  It
    is an example of the Gang of Four's
    `Decorator Pattern <https://en.wikipedia.org/wiki/Decorator_pattern>`_.

    See the documentation for the `DecoratorType` class for more information on how
    these work.
    """
    # pylint: disable=missing-return-doc, no-value-for-parameter

    _slug: str
    _hash: int
    _fields: dict[str, Any]
    _base_type: DecoratorMeta
    _cache_size: int | None
    _flyweights: LinkedDict[str, DecoratorMeta]
    _params: tuple[str]
    _parametrized: bool
    _defines_from_scalar: bool
    _defines_from_dtype: bool

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(
        cls: DecoratorMeta,
        name: str,
        bases: tuple[type, ...],
        namespace: dict[str, Any],
        **kwargs: Any
    ):
        if DEBUG and not (len(bases) == 0 or bases[0] is Base):
            print("-" * 80)
            print(f"slug: {cls.slug}")
            print(f"aliases: {cls.aliases}")
            print(f"fields: {_format_dict(cls._fields)}")
            print(f"cache_size: {cls.cache_size}")
            print()

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[DecoratorMeta],
        namespace: dict[str, Any],
        cache_size: int | None = None,
    ) -> DecoratorMeta:
        build = DecoratorBuilder(name, bases, namespace, cache_size)
        return build.parse().fill().register(BaseMeta.__new__(
            mcs,
            build.name,
            (build.parent,),
            build.namespace
        ))

    def __getitem__(cls, params: Any | tuple[Any, ...]) -> DecoratorMeta:
        if isinstance(params, tuple):
            return cls.__class_getitem__(*params)
        return cls.__class_getitem__(params)

    # NOTE: builders always override these methods during type instantiation, so the
    # versions provided here will never actually be called in practice.

    def from_scalar(cls, scalar: Any) -> DecoratorMeta:
        raise NotImplementedError()

    def from_dtype(cls, dtype: DTYPE) -> DecoratorMeta:
        raise NotImplementedError()

    def from_string(cls, *args: str) -> DecoratorMeta:
        raise NotImplementedError()

    ##########################
    ####    FLYWEIGHTS    ####
    ##########################

    @property
    def is_flyweight(cls) -> bool:
        return cls._parametrized

    @property
    def flyweights(cls) -> Mapping[str, DecoratorMeta]:
        return MappingProxyType(cls._flyweights)

    @property
    def base_type(cls) -> DecoratorMeta:
        return cls._base_type

    @property
    def cache_size(cls) -> int | None:
        return cls._cache_size

    @property
    def params(cls) -> Mapping[str, Any]:
        return MappingProxyType({k: getattr(cls, k) for k in cls._params})

    def flyweight(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls._flyweights.lru_get(slug, None)

        if typ is None:
            if len(args) != len(cls._params):
                raise TypeError(
                    f"expected {len(cls._params)} positional arguments, got "
                    f"{len(args)}"
                )

            typ = BaseMeta.__new__(
                DecoratorMeta,
                cls.__name__,
                (cls,),
                {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,
                    "_base_type": cls,
                    "_parametrized": True,
                    "__class_getitem__": _parametrized_getitem,
                } | kwargs  # override non-required class variables
            )
            cls._flyweights.lru_add(slug, typ)

        return typ

    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        raise NotImplementedError()  # provided in DecoratorType

    ################################
    ####    CLASS DECORATORS    ####
    ################################

    @property
    def is_default(cls) -> bool:
        if cls.is_flyweight:
            return cls.wrapped.is_default
        raise _no_attribute(cls, "is_default")

    @property
    def as_default(cls) -> DecoratorMeta:
        if cls.is_flyweight:
            return cls.replace(cls.wrapped.as_default)
        raise _no_attribute(cls, "as_default")

    @property
    def as_nullable(cls) -> DecoratorMeta:
        if cls.is_flyweight:
            return cls.replace(cls.wrapped.as_nullable)
        raise _no_attribute(cls, "as_nullable")

    def default(cls, other: TypeMeta) -> NoReturn:
        raise _no_attribute(cls, "default")

    def nullable(cls, other: TypeMeta) -> NoReturn:
        raise _no_attribute(cls, "nullable")

    ####################
    ####    CORE    ####
    ####################

    @property
    def aliases(cls) -> Aliases:
        return cls._aliases

    @property
    def scalar(cls) -> type:
        return cls.__getattr__("scalar")

    @property
    def dtype(cls) -> DTYPE:
        return cls.__getattr__("dtype")

    @property
    def itemsize(cls) -> int:
        return cls.__getattr__("itemsize")

    @property
    def max(cls) -> int | float:
        return cls.__getattr__("max")

    @property
    def min(cls) -> int | float:
        return cls.__getattr__("min")

    @property
    def is_nullable(cls) -> bool:
        return cls.__getattr__("is_nullable")

    @property
    def missing(cls) -> Any:
        return cls.__getattr__("missing")

    @property
    def slug(cls) -> str:
        return cls._slug

    @property
    def hash(cls) -> int:
        return cls._hash

    @property
    def backend(cls) -> str:
        return cls.__getattr__("backend")

    @property
    def is_abstract(cls) -> bool:
        return cls.__getattr__("is_abstract")

    #########################
    ####    TRAVERSAL    ####
    #########################

    @property
    def is_root(cls) -> bool:
        return cls.__getattr__("is_root")

    @property
    def root(cls) -> DecoratorMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "root")
        return cls.replace(cls.wrapped.root)

    @property
    def parent(cls) -> DecoratorMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "parent")
        return cls.replace(cls.wrapped.parent)

    @property
    def subtypes(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "subtypes")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.subtypes
        ))

    @property
    def implementations(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "implementations")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.implementations
        ))

    @property
    def children(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "children")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.children
        ))

    @property
    def is_leaf(cls) -> bool:
        return cls.__getattr__("is_leaf")

    @property
    def leaves(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "leaves")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.leaves
        ))

    @property
    def larger(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "larger")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.larger
        ))

    @property
    def smaller(cls) -> UnionMeta:
        if not cls.is_flyweight:
            raise _no_attribute(cls, "smaller")
        return Union.from_types(LinkedSet(
            cls.replace(t) for t in cls.wrapped.smaller
        ))

    ##########################
    ####    DECORATORS    ####
    ##########################

    @property
    def is_decorator(cls) -> bool:
        return True

    @property
    def decorators(cls) -> UnionMeta:
        result = LinkedSet((cls,))
        curr = cls
        while curr is not curr.wrapped:
            curr = curr.wrapped  # type: ignore
            result.add(curr)
        return Union.from_types(result)

    @property
    def wrapped(cls) -> TypeMeta | DecoratorMeta:
        return cls._fields["wrapped"]

    @property
    def unwrapped(cls) -> TypeMeta | DecoratorMeta:
        result: TypeMeta | DecoratorMeta = cls
        while result is not result.wrapped:
            result = result.wrapped
        return result

    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        raise NotImplementedError()  # provided in DecoratorType

    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        raise NotImplementedError()  # provided in DecoratorType

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getattr__(cls, name: str) -> Any:
        if name in cls._fields:
            return cls._fields[name]

        wrapped = cls._fields["wrapped"]
        if cls is not wrapped:
            return getattr(wrapped, name)

        raise _no_attribute(cls, name)

    def __dir__(cls) -> set[str]:
        result = set(dir(DecoratorMeta))
        result.update(cls._fields)
        curr: TypeMeta | DecoratorMeta = cls
        while curr is not curr.wrapped:
            curr = curr.wrapped
            result.update(dir(curr))
        return result

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        wrapped = cls._fields["wrapped"]
        if cls is wrapped:
            raise TypeError(f"decorator has no wrapped type -> {cls.slug}")

        # pylint: disable=no-value-for-parameter
        return cls.transform(wrapped(*args, **kwargs))

    # TODO:
    # >>> Sparse[Int16, 1] in Sparse[Int]
    # False  (should be True?)

    # TODO: probably need some kind of supplemental is_empty field that checks whether
    # parameters were supplied beyond wrapped.

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)

        # scalar types cannot be subclasses of decorators
        if isinstance(typ, TypeMeta):
            return False

        # broadcast across unions
        if isinstance(typ, UnionMeta):
            # pylint: disable=no-value-for-parameter
            return all(cls.__subclasscheck__(t) for t in typ)

        # DecoratorType acts as wildcard
        if cls is DecoratorType:
            return True

        # base types must be compatible
        if cls.base_type is not typ.base_type:
            return False

        # unparametrized types act as wildcards
        t1 = cls.wrapped
        if t1 is cls:
            return True

        t2 = typ.wrapped
        if t2 is typ:
            return False

        # parameters must be compatible
        params = list(cls.params.values())[1:]
        forwarded_params = list(typ.params.values())[1:]
        for p1, p2 in zip(params, forwarded_params):
            na1 = pd.isna(p1)
            na2 = pd.isna(p2)
            if na1 ^ na2 or not (na1 and na2 or p1 is p2 or p1 == p2):
                return False

        # delegate to wrapped types
        return t1.__subclasscheck__(t2)

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        wrapped = cls._fields["wrapped"]
        if cls is wrapped:
            raise TypeError(f"decorator has no wrapped type -> {cls.slug}")
        return len(wrapped)

    def __bool__(cls) -> bool:
        return True

    def __iter__(cls) -> Iterator[DecoratorMeta]:
        wrapped = cls._fields["wrapped"]
        if cls is wrapped:
            raise TypeError(f"decorator has no wrapped type -> {cls.slug}")
        return (cls.replace(t) for t in wrapped)

    def __reversed__(cls) -> Iterator[DecoratorMeta]:
        wrapped = cls._fields["wrapped"]
        if cls is wrapped:
            raise TypeError(f"decorator has no wrapped type -> {cls.slug}")
        return (cls.replace(t) for t in reversed(wrapped))

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        # pylint: disable=comparison-with-callable
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls < t for t in typ)

        t1 = cls.wrapped
        if t1 is cls:
            return False

        if isinstance(typ, DecoratorMeta):
            t2 = typ.wrapped
            if t2 is typ:
                return False
            return t1 < t2

        return t1 < typ

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        # pylint: disable=comparison-with-callable
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls <= t for t in typ)

        t1 = cls.wrapped
        if t1 is cls:
            return False

        if isinstance(typ, DecoratorMeta):
            t2 = typ.wrapped
            if t2 is typ:
                return False
            return t1 <= t2

        return t1 <= typ

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls is t for t in typ)

        return cls is typ

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        # pylint: disable=comparison-with-callable
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls >= t for t in typ)

        t1 = cls.wrapped
        if t1 is cls:
            return False

        if isinstance(typ, DecoratorMeta):
            t2 = typ.wrapped
            if t2 is typ:
                return False
            return t1 >= t2

        return t1 >= typ

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        # pylint: disable=comparison-with-callable
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls > t for t in typ)

        t1 = cls.wrapped
        if t1 is cls:
            return False

        if isinstance(typ, DecoratorMeta):
            t2 = typ.wrapped
            if t2 is typ:
                return False
            return t1 > t2

        return t1 > typ

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


@_copy_docs
class UnionMeta(BaseMeta):
    """Metaclass for all union types (those produced by Union[] or setlike operators).

    This metaclass is responsible for delegating operations to the union's members, and
    is an example of the Gang of Four's `Composite Pattern
    <https://en.wikipedia.org/wiki/Composite_pattern>`.  The union can thus be treated
    similarly to an individual type, with operations producing new unions as output.

    See the documentation for the `Union` class for more information on how these work.
    """

    # pylint: disable=missing-return-doc, no-value-for-parameter, not-an-iterable

    _types: LinkedSet[TypeMeta | DecoratorMeta] = LinkedSet()
    _hash: int = 42
    _index: RLE_ARRAY | None = None

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __getitem__(cls, types: TYPESPEC | tuple[TYPESPEC, ...]) -> UnionMeta:
        return cls.__class_getitem__(types)  # type: ignore

    def from_types(
        cls,
        types: LinkedSet[TypeMeta | DecoratorMeta],
        index: RLE_ARRAY | None = None
    ) -> UnionMeta:
        """Construct a union from a set of types.

        Parameters
        ----------
        types: LinkedSet[TypeMeta | DecoratorMeta]
            A set of types to include in the union.  The order is preserved.
        index: RLE_ARRAY, optional
            A run-length encoded array containing the observed type at every index of
            an input iterable.  This is injected by the :func:`detect` function, and
            should not be used in any other case.

        Returns
        -------
        UnionMeta
            A new union type containing the specified types.

        Raises
        ------
        TypeError
            If any of the types are not bertrand types.

        Notes
        -----
        This is only meant for internal use, and should not be called directly.  The
        `Union[]` operator accomplishes the same thing, but allows for a more
        expressive syntax and automatically resolves each specifier.
        """
        hash_val = 42  # to avoid potential collisions at hash=0
        for t in types:
            if not isinstance(t, (TypeMeta, DecoratorMeta)):
                raise TypeError("union members must be bertrand types")
            hash_val = (hash_val * 31 + hash(t)) % (2**63 - 1)

        return BaseMeta.__new__(UnionMeta, cls.__name__, (cls,), {
            "_types": types,
            "_hash": hash_val,
            "_index": index,
            "__class_getitem__": _union_getitem
        })

    def from_scalar(cls, scalar: Any) -> NoReturn:
        raise _no_attribute(cls, "from_scalar")

    def from_dtype(cls, dtype: DTYPE) -> NoReturn:
        raise _no_attribute(cls, "from_dtype")

    def from_string(cls, *args: str) -> NoReturn:
        raise _no_attribute(cls, "from_string")

    ##########################
    ####    FLYWEIGHTS    ####
    ##########################

    @property
    def is_flyweight(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_flyweight")

    @property
    def flyweights(
        cls
    ) -> Mapping[TypeMeta | DecoratorMeta, Mapping[str, TypeMeta | DecoratorMeta]]:
        return cls.__getattr__("flyweights")

    @property
    def base_type(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.base_type for t in cls))

    @property
    def cache_size(cls) -> UNION_RETURN[int | None]:
        return cls.__getattr__("cache_size")

    @property
    def params(cls) -> UNION_RETURN[Mapping[str, Any]]:
        return cls.__getattr__("params")

    def flyweight(cls, *args: Any, **kwargs: Any) -> NoReturn:
        raise _no_attribute(cls, "flyweight")

    def replace(cls, *args: Any, **kwargs: Any) -> UnionMeta:
        return cls.from_types(LinkedSet(t.replace(*args, **kwargs) for t in cls))

    ################################
    ####    CLASS DECORATORS    ####
    ################################

    @property
    def is_default(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_default")

    @property
    def as_default(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.as_default for t in cls))

    @property
    def as_nullable(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.as_nullable for t in cls))

    def default(cls, other: TypeMeta) -> NoReturn:
        raise _no_attribute(cls, "default")

    def nullable(cls, other: TypeMeta) -> NoReturn:
        raise _no_attribute(cls, "nullable")

    ####################
    ####    CORE    ####
    ####################

    @property
    def aliases(cls) -> UNION_RETURN[Aliases]:
        return cls.__getattr__("aliases")

    @property
    def scalar(cls) -> UNION_RETURN[type]:
        return cls.__getattr__("scalar")

    @property
    def dtype(cls) -> UNION_RETURN[DTYPE]:
        return cls.__getattr__("dtype")

    @property
    def itemsize(cls) -> UNION_RETURN[int]:
        return cls.__getattr__("itemsize")

    @property
    def max(cls) -> UNION_RETURN[int | float]:
        return cls.__getattr__("max")

    @property
    def min(cls) -> UNION_RETURN[int | float]:
        return cls.__getattr__("min")

    @property
    def is_nullable(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_nullable")

    @property
    def missing(cls) -> UNION_RETURN[Any]:
        return cls.__getattr__("missing")

    @property
    def slug(cls) -> UNION_RETURN[str]:
        return cls.__getattr__("slug")

    @property
    def hash(cls) -> UNION_RETURN[int]:
        return cls.__getattr__("hash")

    @property
    def backend(cls) -> UNION_RETURN[str]:
        return cls.__getattr__("backend")

    @property
    def is_abstract(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_abstract")

    #########################
    ####    HIERARCHY    ####
    #########################

    @property
    def is_root(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_root")

    @property
    def root(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.root for t in cls))

    @property
    def parent(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.parent for t in cls))

    @property
    def subtypes(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.subtypes)
        return cls.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.implementations)
        return cls.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.children)
        return cls.from_types(result)

    @property
    def is_leaf(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_leaf")

    @property
    def leaves(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.leaves)
        return cls.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.larger)
        return cls.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.smaller)
        return cls.from_types(result)

    ##########################
    ####    DECORATORS    ####
    ##########################

    @property
    def is_decorator(cls) -> UNION_RETURN[bool]:
        return cls.__getattr__("is_decorator")

    @property
    def decorators(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            result.update(t.decorators)
        return cls.from_types(result)

    @property
    def wrapped(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.wrapped for t in cls))

    @property
    def unwrapped(cls) -> UnionMeta:
        return cls.from_types(LinkedSet(t.unwrapped for t in cls))

    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        for t in cls:
            try:
                return t.transform(series)
            except Exception:  # pylint: disable=broad-exception-caught
                continue

        raise TypeError(f"invalid transform: {repr(cls)}")

    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        for t in cls:
            try:
                return t.inverse_transform(series)
            except Exception:  # pylint: disable=broad-exception-caught
                continue

        raise TypeError(f"invalid inverse transform: {repr(cls)}")

    #####################
    ####    UNION    ####
    #####################

    @property
    def is_union(cls) -> bool:
        return True

    @property
    def index(cls) -> OBJECT_ARRAY | None:
        if cls._index is None:
            return None
        return _rle_decode(cls._index)

    def collapse(cls) -> UnionMeta:
        result = LinkedSet()
        for t in cls:
            if not any(t is not u and issubclass(t, u) for u in cls):
                result.add(t)
        return cls.from_types(result)

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> UnionMeta:
        result = cls._types.copy()
        result.sort(key=key, reverse=reverse)
        return cls.from_types(result)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        for t in cls:
            try:
                return t(*args, **kwargs)
            except Exception:  # pylint: disable=broad-exception-caught
                continue

        raise TypeError(f"cannot convert to union type: {repr(cls)}")

    def __getattr__(cls, name: str) -> UNION_RETURN[Any]:
        return {t: getattr(t, name) for t in cls._types}

    def __dir__(cls) -> set[str]:
        result = set(dir(UnionMeta))
        for t in cls:
            result.update(dir(t))
        return result

    def __hash__(cls) -> int:
        return cls._hash

    def __instancecheck__(cls, other: Any) -> bool:
        observed = detect(other)
        return any(issubclass(observed, t) for t in cls)

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if isinstance(typ, UnionMeta):
            return all(any(issubclass(o, t) for t in cls) for o in typ)
        return any(issubclass(typ, t) for t in cls)

    def __len__(cls) -> int:
        return len(cls._types)

    def __bool__(cls) -> bool:
        return len(cls._types) > 0

    def __iter__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        return iter(cls._types)

    def __reversed__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        return reversed(cls._types)

    def __or__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, UnionMeta):
            return cls.from_types(cls._types | typ)
        return cls.from_types(cls._types | (typ,))

    def __and__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, UnionMeta):
            result = LinkedSet(
                t for t in cls.children if any(issubclass(t, u) for u in typ)
            )
        else:
            result = LinkedSet(t for t in cls.children if issubclass(t, typ))

        return cls.from_types(result).collapse()  # pylint: disable=no-member

    def __sub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, UnionMeta):
            result = LinkedSet(
                t for t in cls.children
                if not any(issubclass(t, u) or issubclass(u, t) for u in typ)
            )
        else:
            result = LinkedSet(
                t for t in cls.children
                if not (issubclass(t, typ) or issubclass(typ, t))
            )

        return cls.from_types(result).collapse()  # pylint: disable=no-member

    def __xor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        result = LinkedSet()
        if isinstance(typ, UnionMeta):
            for t in cls.children:
                if not any(issubclass(t, u) or issubclass(u, t) for u in typ):
                    result.add(t)
            for t in typ.children:
                if not any(issubclass(t, u) or issubclass(u, t) for u in cls):
                    result.add(t)

        else:
            for t in cls.children:
                if not (issubclass(t, typ) or issubclass(typ, t)):
                    result.add(t)
            for t in typ.children:
                if not (issubclass(t, cls) or issubclass(cls, t)):
                    result.add(t)

        return cls.from_types(result).collapse()  # pylint: disable=no-member

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(all(t < u for u in typ) for t in cls)

        return all(t < typ for t in cls)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t <= u for u in typ) for t in cls)

        return all(t <= typ for t in cls)

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if isinstance(typ, UnionMeta):
            return len(cls) == len(typ) and all(t is u for t, u in zip(cls, other))

        return all(t is typ for t in cls)

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t >= u for u in typ) for t in cls)

        return all(t >= typ for t in cls)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(all(t > u for u in typ) for t in cls)

        return all(t > typ for t in cls)

    def __str__(cls) -> str:
        return f"{' | '.join(t.slug for t in cls._types) or '{}'}"

    def __repr__(cls) -> str:
        return f"Union[{', '.join(t.slug for t in cls._types)}]"


# TODO: StructuredMeta should use STRUCTURED_RETURN hints where possible.  That way,
# mypy can infer these return types


@_copy_docs
class StructuredMeta(UnionMeta):
    """Metaclass for structured unions, which are produced by the `Union[]` operator
    when called with a mapping of column names to type specifiers.

    Structured unions can be treated similarly to normal unions, except that they also
    keep track of column names, and can be called to produce an entire dataframe rather
    than a single column.

    See the documentation for the `Union` class for more information on how these work.
    """

    # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc
    # pylint: disable=no-value-for-parameter, not-an-iterable

    _columns: dict[str, META] = {}

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    # TODO: copy from_types docs from UnionMeta

    def from_types(
        cls,
        types: LinkedSet[TypeMeta | DecoratorMeta],
        index: RLE_ARRAY | None = None
    ) -> NoReturn:
        raise TypeError("cannot create structured union from a flat set of types")

    def from_columns(cls, columns: Mapping[str, META]) -> StructuredMeta:
        """Construct a structured union from a mapping of column names to type objects.

        Parameters
        ----------
        columns: dict[str, META]
            A dictionary with strings as keys and type objects as values.  The keys
            represent column names with values representing the type of each column.

        Returns
        -------
        StructuredMeta
            A structured union type.

        Raises
        ------
        TypeError
            If any of the column names are not strings, or any of values are not
            bertrand types.  Also raised if an attempt is made to nest structured
            unions within each other.

        Notes
        -----
        This is only meant for internal use, and should not be called directly.  The
        `Union[]` operator accomplishes the same thing, but allows for a more
        expressive syntax and automatically resolves each specifier.
        """
        hash_val = 42  # to avoid potential collisions at hash=0
        flattened = LinkedSet()
        for k, v in columns.items():
            if not isinstance(k, str):
                raise TypeError("column names must be strings")
            if isinstance(v, StructuredMeta):
                raise TypeError("structured unions cannot be nested")
            if isinstance(v, UnionMeta):
                flattened.update(v)
            elif isinstance(v, (TypeMeta, DecoratorMeta)):
                flattened.add(v)
            else:
                raise TypeError("column values must be bertrand types")
            hash_val = (hash_val * 31 + hash(k) + hash(v)) % (2**63 - 1)

        return BaseMeta.__new__(StructuredMeta, cls.__name__, (cls,), {
            "_columns": columns,
            "_types": flattened,
            "_hash": hash_val,
            "_index": None,
            "__class_getitem__": _structured_getitem
        })

    ##########################
    ####    FLYWEIGHTS    ####
    ##########################

    @property
    def base_type(cls) -> StructuredMeta:
        return cls.from_columns({k: v.base_type for k, v in cls.items()})

    def replace(cls, *args: Any, **kwargs: Any) -> StructuredMeta:
        return cls.from_columns({k: v.replace(*args, **kwargs) for k, v in cls.items()})

    ################################
    ####    CLASS DECORATORS    ####
    ################################

    @property
    def as_default(cls) -> StructuredMeta:
        return cls.from_columns({k: v.as_default for k, v in cls.items()})

    @property
    def as_nullable(cls) -> StructuredMeta:
        return cls.from_columns({k: v.as_nullable for k, v in cls.items()})

    #########################
    ####    HIERARCHY    ####
    #########################

    @property
    def root(cls) -> StructuredMeta:
        return cls.from_columns({k: v.root for k, v in cls.items()})

    @property
    def parent(cls) -> StructuredMeta:
        return cls.from_columns({k: v.parent for k, v in cls.items()})

    @property
    def subtypes(cls) -> StructuredMeta:
        return cls.from_columns({k: v.subtypes for k, v in cls.items()})

    @property
    def implementations(cls) -> StructuredMeta:
        return cls.from_columns({k: v.implementations for k, v in cls.items()})

    @property
    def children(cls) -> StructuredMeta:
        return cls.from_columns({k: v.children for k, v in cls.items()})

    @property
    def leaves(cls) -> StructuredMeta:
        return cls.from_columns({k: v.leaves for k, v in cls.items()})

    @property
    def larger(cls) -> StructuredMeta:
        return cls.from_columns({k: v.larger for k, v in cls.items()})

    ##########################
    ####    DECORATORS    ####
    ##########################

    @property
    def decorators(cls) -> StructuredMeta:
        return cls.from_columns({k: v.decorators for k, v in cls.items()})

    @property
    def wrapped(cls) -> StructuredMeta:
        return cls.from_columns({k: v.wrapped for k, v in cls.items()})

    @property
    def unwrapped(cls) -> StructuredMeta:
        return cls.from_columns({k: v.unwrapped for k, v in cls.items()})

    #####################
    ####    UNION    ####
    #####################

    @property
    def is_structured(cls) -> bool:
        return True

    @property
    def index(cls) -> OBJECT_ARRAY | None:
        # pylint: disable=protected-access
        indices = {
            col: _rle_decode(t._index) for col, t in cls.items()
            if isinstance(t, UnionMeta) and t._index is not None
        }
        if not indices:
            return None

        shape = next(iter(indices.values())).shape
        arr = np.empty(shape, dtype=np.dtype([(k, object) for k in cls._columns]))
        for col, t in cls.items():
            if col in indices:
                arr[col] = indices[col]
            else:
                arr[col] = np.full(shape, t, dtype=object)

        return arr

    def collapse(cls) -> StructuredMeta:
        return cls.from_columns({
            k: v.collapse() if isinstance(v, UnionMeta) else v for k, v in cls.items()
        })

    def issubset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        typ = resolve(other)
        if cls is typ:
            return not strict

        if isinstance(typ, StructuredMeta):
            k1 = cls.keys()
            k2 = typ.keys()
            return (
                k1 <= k2 and all(issubclass(t, typ[k]) for k, t in cls.items()) and
                (
                    not strict or
                    len(cls.children) < len(typ.children) or len(k1) < len(k2)
                )
            )

        return (
            issubclass(cls, typ) and
            not strict or len(cls.children) < len(typ.children)
        )

    def issuperset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        typ = resolve(other)
        if cls is typ:
            return not strict

        if isinstance(typ, StructuredMeta):
            k1 = cls.keys()
            k2 = typ.keys()
            return (
                k1 >= k2 and all(issubclass(typ[k], t) for k, t in cls.items()) and
                (
                    not strict or
                    len(cls.children) > len(typ.children) or len(k1) > len(k2)
                )
            )

        return (
            issubclass(typ, cls) and
            not strict or len(cls.children) > len(typ.children)
        )

    def isdisjoint(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, StructuredMeta):
            keys = typ.keys()
            for k, v in cls.items():
                if k in keys:
                    t = typ[k]
                    if issubclass(v, t) or issubclass(t, v):
                        return False
            return True

        return all(not (issubclass(t, typ) or issubclass(typ, t)) for t in cls)

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> StructuredMeta:
        return cls.from_columns({
            k: v.sorted(key=key, reverse=reverse) if isinstance(v, UnionMeta) else v
            for k, v in cls.items()
        })

    def keys(cls) -> KeysView[str]:
        return cls._columns.keys()

    def values(cls) -> ValuesView[META]:
        return cls._columns.values()

    def items(cls) -> ItemsView[str, META]:
        return cls._columns.items()

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getattr__(cls, name: str) -> Mapping[str, Any]:  # type: ignore
        return {k: getattr(v, name) for k, v in cls.items()}

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.DataFrame:  # type: ignore
        if len(args) != len(cls._columns):
            raise TypeError(
                f"structured union expects {len(cls._columns)} positional "
                f"arguments, not {len(args)}"
            )

        return pd.DataFrame({
            k: v(args[i], **kwargs) for i, (k, v) in enumerate(cls.items())
        })

    def __instancecheck__(cls, other: Any) -> bool:
        if isinstance(other, pd.DataFrame):
            return (
                len(cls._columns) == len(other.columns) and
                all(x == y for x, y in zip(cls._columns, other.columns)) and
                all(isinstance(other[k], v) for k, v in cls.items())
            )

        if isinstance(other, np.ndarray) and other.dtype.fields:
            return (
                len(cls._columns) == len(other.dtype.names) and
                all(x == y for x, y in zip(cls._columns, other.dtype.names)) and
                all(isinstance(other[k], v) for k, v in cls.items())
            )

        return False

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)

        if isinstance(typ, StructuredMeta):
            k1 = cls.keys()
            k2 = typ.keys()
            return (
                len(k1) == len(k2) and all(x == y for x, y in zip(k1, k2)) and
                all(issubclass(typ[k], v) for k, v in cls.items())
            )

        if isinstance(typ, UnionMeta):
            return all(any(issubclass(o, t) for t in cls) for o in typ)

        return any(issubclass(typ, t) for t in cls)

    def __or__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> StructuredMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        columns = cls._columns.copy()
        if isinstance(typ, StructuredMeta):
            for k, v in typ.items():
                if k in columns:
                    columns[k] |= v
                else:
                    columns[k] = v
        else:
            for k, v in columns.items():
                columns[k] |= typ

        return cls.from_columns(columns)

    def __and__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> StructuredMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, StructuredMeta):
            keys = typ.keys()
            columns = {k: v & typ[k] for k, v in cls.items() if k in keys}
        else:
            columns = {k: v & typ for k, v in cls.items()}

        return cls.from_columns(columns)

    def __sub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> StructuredMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, StructuredMeta):
            columns = cls._columns.copy()
            for k, v in typ.items():
                if k in columns:
                    columns[k] -= v
        else:
            columns = {k: v - typ for k, v in cls.items()}

        return cls.from_columns(columns)

    def __xor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> StructuredMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, StructuredMeta):
            columns = cls._columns.copy()
            for k, v in typ.items():
                if k in columns:
                    columns[k] ^= v
                else:
                    columns[k] = v
        else:
            columns = {k: v ^ typ for k, v in cls.items()}

        return cls.from_columns(columns)

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, StructuredMeta):
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v < t:
                    return False
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t < u for u in typ) for t in cls)

        return all(t < typ for t in cls)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, StructuredMeta):
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v <= t:
                    return False
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t <= u for u in typ) for t in cls)

        return all(t <= typ for t in cls)

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if isinstance(typ, StructuredMeta):
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if v is not t:
                    return False
            return True

        return False

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, StructuredMeta):
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v >= t:
                    return False
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t >= u for u in typ) for t in cls)

        return all(t >= typ for t in cls)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, StructuredMeta):
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v > t:
                    return False
            return True

        if isinstance(typ, UnionMeta):
            return all(all(t > u for u in typ) for t in cls)

        return all(t > typ for t in cls)

    def __str__(cls) -> str:
        items = [f"'{k}': {str(v) if v else '{}'}" for k, v in cls._columns.items()]
        return f"{{{', '.join(items)}}}"

    def __repr__(cls) -> str:
        items = [f"'{k}': {str(v) if v else 'Union[]'}" for k, v in cls._columns.items()]
        return f"Union[{', '.join(items)}]"


# TODO:
# >>> Sparse[Sparse[Int, 1]]
# Sparse[Int, <NA>]
# -> should preserve the innermost 1?


def _insort_decorator(
    invoke: Callable[..., DecoratorMeta],
    cls: DecoratorMeta,
    other: TypeMeta | DecoratorMeta,
    *args: Any,
) -> DecoratorMeta:
    """Insert a decorator into a type's decorator stack, ensuring that it obeys the
    registry's current precedence.
    """
    visited: list[DecoratorMeta] = []
    curr: DecoratorMeta | TypeMeta = other
    wrapped = curr.wrapped

    while wrapped is not curr:
        delta = REGISTRY.decorators.distance(cls, curr.base_type)  # type: ignore
        if delta > 0:  # cls is higher priority or identical to current decorator
            break
        if delta == 0:  # cls is identical to current decorator
            curr = wrapped
            break

        visited.append(curr)  # type: ignore
        curr = wrapped
        wrapped = curr.wrapped

    result = invoke(cls, curr, *args)
    for curr in reversed(visited):
        result = curr.replace(result)
    return result


def _no_attribute(cls: type, name: str) -> AttributeError:
    """Raise an AttributeError for a missing attribute."""
    return AttributeError(f"type '{cls.__name__}' has no attribute '{name}'")


def _has_edge(t1: TypeMeta, t2: TypeMeta) -> bool | None:
    """Check whether an edge exists between two types in a comparison."""
    edges = REGISTRY.edges
    reset = t2

    while not t1.is_root:
        while not t2.is_root:
            if (t1, t2) in edges:
                return True
            if (t2, t1) in edges:
                return False
            t2 = t2.parent

        t1 = t1.parent
        t2 = reset

    return None


def _features(t: TypeMeta | DecoratorMeta) -> tuple[int | float, int, bool, int | float]:
    """Extract a tuple of features representing the allowable range of a type, used for
    range-based sorting and comparison.
    """
    return (
        t.max - t.min,  # total range
        t.itemsize,  # memory footprint
        t.is_nullable,  # nullability
        abs(t.max + t.min),  # bias away from zero
    )


def _format_dict(d: dict[str, Any]) -> str:
    """Convert a dictionary into a string with standard indentation."""
    if not d:
        return "{}"

    prefix = "{\n    "
    sep = ",\n    "
    suffix = "\n}"
    return prefix + sep.join(f"{k}: {repr(v)}" for k, v in d.items()) + suffix


def _parametrized_getitem(cls: TypeMeta, *args: Any) -> NoReturn:
    """A parametrized replacement for a scalar or decorator's __class_getitem__()
    method that disables further parametrization.
    """
    raise TypeError(f"{cls.slug} cannot be re-parametrized")


def _union_getitem(cls: UnionMeta, key: int | slice) -> META:
    """A parametrized replacement for the base Union's __class_getitem__() method that
    allows for integer-based indexing/slicing of a union's types.
    """
    # pylint: disable=protected-access
    if isinstance(key, slice):
        return cls.from_types(cls._types[key])
    return cls._types[key]


def _structured_getitem(cls: StructuredMeta, key: int | slice | str) -> META:
    """A parametrized replacement for the base Union's __class_getitem__() method that
    allows for string-based lookup as well as integer indexing into the union's
    columns
    """
    # pylint: disable=protected-access
    if isinstance(key, str):
        return cls._columns[key]
    if isinstance(key, slice):
        return cls.from_columns({k: v for k, v in list(cls.items())[key]})
    return list(cls.values())[key]


########################
####    BUILDERS    ####
########################


class TypeBuilder:
    """Common base for all namespace builders.

    The builders that inherit from this class are responsible for transforming a type's
    simplified namespace into a fully-featured bertrand type.  They do this by looping
    through all the attributes that are defined in the class and executing any relevant
    helper functions to validate their configuration.  The builders also mark all
    required fields (those assigned to ``EMPTY``) and reserved attributes that they
    encounter, and fill in missing slots with default values where possible.  If the
    type is improperly configured, the builder will throw a series of informative error
    messages guiding the user towards a solution.

    The end result is a fully processed namespace that conforms to the expectations of
    each metaclass, which can then be used to instantiate the type.  This means that
    the output from the builder is directly converted into dotted names on the type, as
    reflected by the `dir()` built-in function.  Since each builder is executed before
    the type is instantiated, they have free reign to modify the namespace however they
    see fit, without violating the type's read-only semantics.

    These builders are what allow us to write new types without regard to the internal
    mechanics of the type system.  Instead, the relevant information will be
    automatically extracted from the namespace and used to construct the type, without
    any additional work on the user's part.
    """

    RESERVED: set[str] = set(dir(TypeMeta)) - {
        "__module__",
        "__qualname__",
        "__annotations__",
        "__doc__",
        "aliases",
        "scalar",
        "dtype",
        "itemsize",
        "max",
        "min",
        "missing",
        "is_nullable",
    }

    def __init__(
        self,
        name: str,
        parent: TypeMeta | DecoratorMeta,
        namespace: dict[str, Any],
        cache_size: int | None,
    ):
        self.name = name
        self.parent = parent
        self.namespace = namespace
        self.cache_size = cache_size
        if parent is Base:
            self.required: dict[str, Callable[[Any, dict[str, Any], dict[str, Any]], Any]] = {}
            self.fields = {}
        else:
            self.required = parent._required.copy()
            self.fields = parent._fields.copy()

    def fill(self) -> TypeBuilder:
        """Fill in any reserved slots in the namespace with their default values.

        This method should be called after parsing the namespace to avoid unnecessary
        work.

        Returns
        -------
        TypeBuilder
            The builder instance, for chaining.
        """
        self.namespace["_required"] = self.required
        self.namespace["_fields"] = self.fields
        self.namespace["_slug"] = self.name
        self.namespace["_hash"] = hash(self.name)
        if self.parent is Base or self.parent is Type or self.parent is DecoratorType:
            self.namespace["_parent"] = None  # replaced with self-ref in register()
        else:
            self.namespace["_parent"] = self.parent
        self.namespace["_base_type"] = None  # replaced with self-ref in register()
        self.namespace["_parametrized"] = False
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict(max_size=self.cache_size)

        aliases = LinkedSet[str | type]((self.name,))
        if "aliases" in self.namespace:
            for alias in self.namespace.pop("aliases"):
                if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                    aliases.add(type(alias))
                else:
                    aliases.add(alias)

        self.namespace["_aliases"] = Aliases(aliases)
        return self

    def _validate(self, arg: str, value: Any) -> None:
        """Validate a required argument (one assigned to `EMPTY`) by invoking a bound
        function with the specified value and current namespace.
        """
        try:
            self.fields[arg] = self.required.pop(arg)(
                value, self.namespace, self.fields
            )
        except Exception as err:
            raise type(err)(f"{self.name}.{arg} -> {str(err)}")


class AbstractBuilder(TypeBuilder):
    """A builder-style parser for an abstract type's namespace (backend == None).

    Abstract types support the creation of required fields through the ``EMPTY``
    syntax, which must be inherited or defined by each of their concrete
    implementations.  This rule is enforced by ConcreteBuilder, which is automatically
    chosen when the backend is not None.  Note that in contrast to concrete types,
    abstract types do not support parametrization, and attempting defining a
    corresponding constructor will throw an error.
    """

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        cache_size: int | None
    ):
        if len(bases) != 1 or not (bases[0] is Base or isinstance(bases[0], TypeMeta)):
            raise TypeError(
                f"{name} -> abstract types must inherit from bertrand.Type or one of "
                f"its subclasses, not {bases}"
            )

        parent = bases[0]
        if parent is not Base and not parent.is_abstract:
            raise TypeError(
                f"{name} -> abstract types must not inherit from concrete types"
            )

        super().__init__(name, parent, namespace, cache_size)

    def parse(self) -> AbstractBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.

        Returns
        -------
        AbstractBuilder
            A reference to the current builder instance, for chaining.

        Raises
        ------
        TypeError
            If the type implements a reserved attribute.
        """
        namespace = self.namespace.copy()

        for name, value in namespace.items():
            if isinstance(value, Empty):
                self.required[name] = value.func
                del self.namespace[name]
            elif name in self.required:
                self._validate(name, self.namespace.pop(name))
            elif name in self.RESERVED and self.parent is not Base:
                raise TypeError(
                    f"{self.name} -> must not implement reserved attribute: "
                    f"{repr(name)}"
                )

        return self

    def fill(self) -> AbstractBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.

        Returns
        -------
        AbstractBuilder
            A reference to the current builder instance, for chaining.
        """
        super().fill()
        self.namespace["_children"] = LinkedSet()
        self.namespace["_subtypes"] = LinkedSet()
        self.namespace["_implementations"] = LinkedDict()
        self.namespace["_default"] = None  # replaced with self-ref in register()
        self.namespace["_nullable"] = None

        self.fields["backend"] = ""

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """Register a newly-created type, pushing changes to the parent type's
        namespace if necessary.

        Parameters
        ----------
        typ : TypeMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        TypeMeta
            The registered type.
        """
        # pylint: disable=protected-access
        if typ._parent is None:
            typ._parent = typ  # type: ignore
        if typ._base_type is None:
            typ._base_type = typ  # type: ignore
        if typ._default is None:
            typ._default = typ  # type: ignore
        if typ._nullable is None and self.fields.get("is_nullable", False):
            typ._nullable = typ

        parent = self.parent
        if parent is not Base:
            typ.aliases.parent = typ  # pushes aliases to global registry
            typ._children.add(typ)
            REGISTRY._types.add(typ)

            parent._subtypes.add(typ)
            parent._children.add(typ)
            while not parent.is_root:
                parent = parent.parent
                parent._children.add(typ)

        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "__class_getitem__" in self.namespace and self.parent is not Base:
            raise TypeError(
                f"{self.name} -> abstract types must not implement __class_getitem__"
            )

        def default(cls: TypeMeta, backend: str, *args: Any) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            # pylint: disable=protected-access
            if backend in cls._implementations:
                if args:
                    return cls._implementations[backend][*args]
                return cls._implementations[backend]

            if cls is not cls._default:
                default = cls.as_default
                if default.is_abstract:
                    return default.__class_getitem__(backend, *args)

            raise TypeError(f"{self.name} -> invalid backend: {repr(backend)}")

        self.namespace["__class_getitem__"] = classmethod(default)  # type: ignore
        self.namespace["_params"] = ("backend",)

    def _from_scalar(self) -> None:
        """Parse a type's from_scalar() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_scalar" in self.namespace and self.parent is not Base:
            raise TypeError(
                f"{self.name} -> abstract types must not implement from_scalar"
            )

        def default(cls: TypeMeta, scalar: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError(
                f"{self.name} -> abstract types cannot be constructed from scalar "
                f"examples"
            )

        self.namespace["_defines_from_scalar"] = False
        self.namespace["from_scalar"] = classmethod(default)  # type: ignore

    def _from_dtype(self) -> None:
        """Parse a type's from_dtype() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_dtype" in self.namespace and self.parent is not Base:
            raise TypeError(
                f"{self.name} -> abstract types must not implement from_dtype"
            )

        def default(cls: TypeMeta, dtype: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError(
                f"{self.name} -> abstract types cannot be constructed from "
                f"numpy/pandas dtypes"
            )

        self.namespace["_defines_from_scalar"] = False
        self.namespace["from_dtype"] = classmethod(default)  # type: ignore

    def _from_string(self) -> None:
        """Parse a type's from_string() method (if it has one), ensuring that it is
        callable with the same number of positional arguments as __class_getitem__().
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_string" in self.namespace and self.parent is not Base:
            raise TypeError(
                f"{self.name} -> abstract types must not implement from_string"
            )

        def default(cls: TypeMeta, backend: str, *args: str) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            # pylint: disable=protected-access
            if not backend:
                return cls

            if backend in cls._implementations:
                typ = cls._implementations[backend]
                if not args:
                    return typ
                return typ.from_string(*args)

            if cls._default:
                return cls.as_default.from_string(backend, *args)

            raise TypeError(f"{self.name} -> invalid backend: {repr(backend)}")

        self.namespace["from_string"] = classmethod(default)  # type: ignore


class ConcreteBuilder(TypeBuilder):
    """A builder-style parser for a concrete type's namespace (backend != None).

    Concrete types must implement all required fields, and may optionally define
    additional fields that are not required.  They can also be parametrized by
    specifying a __class_getitem__ method that accepts any number of positional
    arguments.  These arguments must have default values, and the signature will be
    used to populate the base type's `params` dict.  Parametrized types are then
    instantiated using a flyweight caching strategy, which involves an LRU dict of a
    specific size attached to the type itself.
    """

    RESERVED = TypeBuilder.RESERVED - {
        "__class_getitem__",
        "from_scalar",
        "from_dtype",
        "from_string",
        "replace",
    }

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str,
        cache_size: int | None,
    ):
        if len(bases) != 1 or not (bases[0] is Base or isinstance(bases[0], TypeMeta)):
            raise TypeError(
                f"{name} -> concrete types must inherit from bertrand.Type or one of "
                f"its subclasses, not {bases}"
            )

        if not isinstance(backend, str):
            raise TypeError(
                f"{name} -> backend id must be a string, not {repr(backend)}"
            )

        parent = bases[0]
        if parent is not Base:
            if not parent.is_abstract:
                raise TypeError(
                    f"{name} -> concrete types must not inherit from other concrete "
                    f"types"
                )
            if backend in parent._implementations:
                raise TypeError(
                    f"{name} -> backend id must be unique: {repr(backend)}"
                )

        super().__init__(name, parent, namespace, cache_size)
        self.backend = backend
        self.class_getitem_signature: inspect.Signature | None = None

    def parse(self) -> ConcreteBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.

        Returns
        -------
        ConcreteBuilder
            A reference to the current builder instance, for chaining.

        Raises
        ------
        TypeError
            If the type implements a reserved attribute.
        """
        for name, value in self.namespace.copy().items():
            if isinstance(value, Empty):
                raise TypeError(
                    f"{self.name} -> concrete types must not have required fields: "
                    f"{self.name}.{name}"
                )
            elif name in self.RESERVED:
                raise TypeError(
                    f"{self.name} -> must not implement reserved attribute: "
                    f"{repr(name)}"
                )
            elif name in self.required:
                self._validate(name, self.namespace.pop(name))

        # validate any remaining required fields that were not explicitly assigned
        for name in list(self.required):
            self._validate(name, EMPTY)

        return self

    def fill(self) -> ConcreteBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.

        Returns
        -------
        ConcreteBuilder
            A reference to the current builder instance, for chaining.
        """
        super().fill()
        self.namespace["_children"] = LinkedSet()
        self.namespace["_subtypes"] = LinkedSet()
        self.namespace["_implementations"] = LinkedDict()
        self.namespace["_default"] = None
        self.namespace["_nullable"] = None

        self.fields["backend"] = self.backend

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """Register a newly-created type, pushing changes to the parent type's
        namespace if necessary.

        Parameters
        ----------
        typ : TypeMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        TypeMeta
            The registered type.
        """
        # pylint: disable=protected-access
        if typ._parent is None:
            typ._parent = typ  # type: ignore
        if typ._base_type is None:
            typ._base_type = typ  # type: ignore
        if typ._default is None:
            typ._default = typ  # type: ignore
        if typ._nullable is None and self.fields.get("is_nullable", False):
            typ._nullable = typ

        parent = self.parent
        if self.parent is not Base:
            typ.aliases.parent = typ  # pushes aliases to global registry
            typ._children.add(typ)
            REGISTRY._types.add(typ)

            parent._implementations[self.backend] = typ
            parent._children.add(typ)
            while not parent.is_root:
                parent = parent.parent
                parent._children.add(typ)

        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc
        parameters = {}

        if "__class_getitem__" in self.namespace:
            skip = True
            sig = inspect.signature(self.namespace["__class_getitem__"])
            for par_name, param in sig.parameters.items():
                if skip:
                    skip = False
                    continue
                if param.kind == param.KEYWORD_ONLY or param.kind == param.VAR_KEYWORD:
                    raise TypeError(
                        f"{self.name} -> __class_getitem__ must not accept any "
                        f"keyword arguments"
                    )
                if param.default is param.empty:
                    raise TypeError(
                        f"{self.name} -> all arguments to __class_getitem__ must have "
                        f"default values"
                    )
                parameters[par_name] = param.default

            self.fields.update((k, v) for k, v in parameters.items())
            self.class_getitem_signature = inspect.Signature([
                p.replace(annotation=p.empty, default=p.empty)
                for p in sig.parameters.values()
            ])

        else:
            def default(cls: TypeMeta, *args: Any) -> TypeMeta:
                """Default to identity function."""
                if args:
                    raise TypeError(
                        f"{self.name} -> type does not accept any parameters"
                    )
                return cls

            self.namespace["__class_getitem__"] = classmethod(default)  # type: ignore
            self.class_getitem_signature = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD)
            ])

        self.namespace["_params"] = tuple(parameters.keys())

    def _from_scalar(self) -> None:
        """Parse a type's from_scalar() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_scalar" in self.namespace:
            method = self.namespace["from_scalar"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_scalar must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> from_scalar must accept a single positional "
                    "argument (in addition to cls)"
                )

            self.namespace["_defines_from_scalar"] = True

        else:
            def default(cls: TypeMeta, scalar: Any) -> TypeMeta:
                """Default to identity function."""
                return cls

            self.namespace["_defines_from_scalar"] = False
            self.namespace["from_scalar"] = classmethod(default)  # type: ignore

    def _from_dtype(self) -> None:
        """Parse a type's from_dtype() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_dtype" in self.namespace:
            method = self.namespace["from_dtype"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_dtype must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> from_dtype must accept a single positional "
                    "argument (in addition to cls)"
                )

            self.namespace["_defines_from_dtype"] = True

        else:
            def default(cls: TypeMeta, dtype: Any) -> TypeMeta:
                """Default to identity function."""
                return cls

            self.namespace["_defines_from_dtype"] = False
            self.namespace["from_dtype"] = classmethod(default)  # type: ignore

    def _from_string(self) -> None:
        """Parse a type's from_string() method (if it has one), ensuring that it is
        callable with the same number of positional arguments as __class_getitem__().
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_string" in self.namespace:
            method = self.namespace["from_string"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_string must be a classmethod")

            params = inspect.signature(method.__wrapped__).parameters.values()
            sig = inspect.Signature([
                p.replace(annotation=p.empty, default=p.default) for p in params
            ])

        else:
            def default(cls: TypeMeta) -> TypeMeta:
                """Default to identity function."""
                return cls

            self.namespace["from_string"] = classmethod(default)  # type: ignore
            sig = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD)
            ])

        if sig != self.class_getitem_signature:
            raise TypeError(
                f"{self.name} -> the signature of from_string must match "
                f"__class_getitem__: {str(sig)} != {str(self.class_getitem_signature)}"
            )


class DecoratorBuilder(TypeBuilder):
    """A builder-style parser for a decorator type's namespace (any subclass of
    DecoratorType).

    Decorator types are similar to concrete types except that they are implicitly
    parametrized to wrap around another type and modify its behavior.  They can accept
    additional parameters to configure the decorator, but must take at least one
    positional argument that specifies the type to be wrapped.
    """

    RESERVED = TypeBuilder.RESERVED - {
        "__class_getitem__",
        "from_scalar",
        "from_dtype",
        "from_string",
        "replace",
        "transform",
        "inverse_transform",
    }

    def __init__(
        self,
        name: str,
        bases: tuple[DecoratorMeta],
        namespace: dict[str, Any],
        cache_size: int | None,
    ):
        if len(bases) != 1 or not (bases[0] is Base or bases[0] is DecoratorType):
            raise TypeError(
                f"{name} -> decorators must inherit from bertrand.DecoratorType, not "
                f"{bases}"
            )

        super().__init__(name, bases[0], namespace, cache_size)
        self.class_getitem_signature: inspect.Signature | None = None

    def parse(self) -> DecoratorBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.

        Returns
        -------
        DecoratorBuilder
            A reference to the current builder instance, for chaining.

        Raises
        ------
        TypeError
            If the type implements a reserved attribute, or defines a required field.
        """
        for name, value in self.namespace.copy().items():
            if isinstance(value, Empty):
                raise TypeError(
                    f"{self.name} -> decorator types must not have required fields: "
                    f"{self.name}.{name}"
                )
            if name in self.RESERVED:
                raise TypeError(
                    f"{self.name} -> must not implement reserved attribute: "
                    f"{repr(name)}"
                )
            if name in self.required:
                self._validate(name, self.namespace.pop(name))

        # validate any remaining required fields that were not explicitly assigned
        for name in list(self.required):
            self._validate(name, EMPTY)

        return self

    def fill(self) -> DecoratorBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.

        Returns
        -------
        DecoratorBuilder
            A reference to the current builder instance, for chaining.
        """
        super().fill()

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()
        self._transform()
        self._inverse_transform()

        return self

    def register(self, typ: DecoratorMeta) -> DecoratorMeta:
        """Instantiate the type and register it in the global type registry.

        Parameters
        ----------
        typ : DecoratorMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        DecoratorMeta
            The registered type.
        """
        # pylint: disable=protected-access
        typ._fields["wrapped"] = typ
        if typ._base_type is None:
            typ._base_type = typ  # type: ignore

        parent = self.parent
        if parent is not Base:
            typ.aliases.parent = typ  # pushes aliases to global registry
            REGISTRY._types.add(typ)
            REGISTRY.decorators._set.add(typ)

        return typ

    def _class_getitem(self) -> None:
        """Parse a decorator's __class_getitem__ method (if it has one) and generate a
        wrapper that can be used to instantiate the type.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc
        parameters = {}

        if "__class_getitem__" in self.namespace:
            invoke = self.namespace["__class_getitem__"]

            def wrapper(
                cls: DecoratorMeta,
                wrapped: TYPESPEC | Iterable[TYPESPEC],
                *args: Any
            ) -> DecoratorMeta | UnionMeta:
                """Unwrap tuples/unions and forward arguments to the wrapped method."""
                if isinstance(wrapped, (TypeMeta, DecoratorMeta)):
                    return _insort_decorator(invoke, cls, wrapped, *args)

                if isinstance(wrapped, StructuredMeta):
                    columns = {}
                    for k, v in wrapped.items():
                        if isinstance(v, UnionMeta):
                            columns[k] = Union.from_types(LinkedSet(
                                _insort_decorator(invoke, cls, t, *args) for t in v
                            ))
                        else:
                            columns[k] = _insort_decorator(invoke, cls, v, *args)

                    return StructuredUnion.from_columns(columns)

                if isinstance(wrapped, UnionMeta):
                    return Union.from_types(LinkedSet(
                        _insort_decorator(invoke, cls, t, *args) for t in wrapped
                    ))

                raise TypeError(
                    f"{self.name} -> decorators must accept another bertrand type as "
                    f"a first argument, not {repr(wrapped)}"
                )

            self.namespace["__class_getitem__"] = classmethod(wrapper)  # type: ignore

            skip = True
            sig = inspect.signature(invoke)
            if len(sig.parameters) < 2:
                raise TypeError(
                    f"{self.name} -> __class_getitem__() must accept at least one "
                    f"positional argument describing the type to decorate (in "
                    f"addition to cls)"
                )

            for par_name, param in sig.parameters.items():
                if skip:
                    skip = False
                    continue
                if param.kind == param.KEYWORD_ONLY or param.kind == param.VAR_KEYWORD:
                    raise TypeError(
                        f"{self.name} -> __class_getitem__ must not accept any "
                        f"keyword arguments"
                    )
                if param.default is param.empty:
                    raise TypeError(
                        f"{self.name} -> __class_getitem__ arguments must have "
                        f"default values"
                    )
                parameters[par_name] = param.default

            self.fields.update((k, v) for k, v in parameters.items())
            self.class_getitem_signature = inspect.Signature([
                p.replace(annotation=p.empty, default=p.empty)
                for p in sig.parameters.values()
            ])

        else:
            invoke = lambda cls, wrapped, *args: cls.flyweight(wrapped, *args)

            def default(cls: DecoratorMeta, wrapped: META) -> DecoratorMeta | UnionMeta:
                """Default to identity function."""
                if isinstance(wrapped, (TypeMeta, DecoratorMeta)):
                    return _insort_decorator(invoke, cls, wrapped)

                if isinstance(wrapped, StructuredMeta):
                    columns = {}
                    for k, v in wrapped.items():
                        if isinstance(v, UnionMeta):
                            columns[k] = Union.from_types(LinkedSet(
                                _insort_decorator(invoke, cls, t) for t in v
                            ))
                        else:
                            columns[k] = _insort_decorator(invoke, cls, v)

                    return StructuredUnion.from_columns(columns)

                if isinstance(wrapped, UnionMeta):
                    return Union.from_types(
                        LinkedSet(_insort_decorator(invoke, cls, t) for t in wrapped)
                    )

                raise TypeError(
                    f"{self.name} -> decorators must accept another bertrand type as "
                    f"a first argument, not {repr(wrapped)}"
                )

            parameters["wrapped"] = None
            self.namespace["__class_getitem__"] = classmethod(default)  # type: ignore
            self.class_getitem_signature = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD),
                inspect.Parameter("wrapped", inspect.Parameter.POSITIONAL_OR_KEYWORD),
            ])

        self.namespace["_params"] = tuple(parameters.keys())

    def _from_scalar(self) -> None:
        """Parse a decorator's from_scalar() method (if it has one) and ensure that it
        is callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_scalar" in self.namespace:
            method = self.namespace["from_scalar"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_scalar must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> from_scalar must accept a single positional "
                    f"argument (in addition to cls)"
                )

            self.namespace["_defines_from_scalar"] = True

        else:
            def default(cls: DecoratorMeta, scalar: Any) -> DecoratorMeta:
                """Default to identity function."""
                return cls

            self.namespace["_defines_from_scalar"] = False
            self.namespace["from_scalar"] = classmethod(default)  # type: ignore

    def _from_dtype(self) -> None:
        """Parse a decorator's from_dtype() method (if it has one) and ensure that it
        is callable with a single positional argument.
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_dtype" in self.namespace:
            method = self.namespace["from_dtype"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_dtype must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> from_dtype must accept a single positional "
                    f"argument (in addition to cls)"
                )

            self.namespace["_defines_from_dtype"] = True

        else:
            def default(cls: DecoratorMeta, dtype: Any) -> DecoratorMeta:
                """Default to identity function."""
                return cls

            self.namespace["_defines_from_dtype"] = False
            self.namespace["from_dtype"] = classmethod(default)  # type: ignore

    def _from_string(self) -> None:
        """Parse a decorator's from_string() method (if it has one) and ensure that it
        is callable with the same number of positional arguments as __class_getitem__().
        """
        # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

        if "from_string" in self.namespace:
            method = self.namespace["from_string"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> from_string must be a classmethod")

            params = inspect.signature(method.__wrapped__).parameters.values()
            sig = inspect.Signature([
                p.replace(annotation=p.empty, default=p.empty) for p in params
            ])

        else:
            def default(cls: DecoratorMeta, wrapped: str) -> DecoratorMeta:
                """Default to identity function."""
                return cls[wrapped]

            self.namespace["from_string"] = classmethod(default)  # type: ignore
            sig = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD),
                inspect.Parameter("wrapped", inspect.Parameter.POSITIONAL_OR_KEYWORD),
            ])

        if sig != self.class_getitem_signature:
            raise TypeError(
                f"{self.name} -> the signature of from_string must match "
                f"__class_getitem__: {str(sig)} != {str(self.class_getitem_signature)}"
            )

    def _transform(self) -> None:
        """Parse a decorator's transform() method (if it has one) and ensure that it
        is callable with a single positional argument.
        """
        if "transform" in self.namespace:
            method = self.namespace["transform"]
            if not isinstance(method, classmethod):
                raise TypeError(f"{self.name} -> transform must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> transform must accept a single positional "
                    f"argument (in addition to cls)"
                )

    def _inverse_transform(self) -> None:
        """Parse a decorator's inverse_transform() method (if it has one) and ensure
        that it is callable with a single positional argument.
        """
        if "inverse_transform" in self.namespace:
            method = self.namespace["inverse_transform"]
            if not isinstance(method, classmethod):
                raise TypeError(
                    f"{self.name} -> inverse_transform must be a classmethod"
                )

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    f"{self.name} -> inverse_transform must accept a single "
                    f"positional argument (in addition to cls)"
                )


##########################
####    BASE TYPES    ####
##########################


# TODO: Type/DecoratorType should go in a separate bases.py file so that they can be
# hosted verbatim in the docs.
# -> metaclasses/builders/union go in meta.py or meta.h + meta.cpp
# -> meta.py, type.py, decorator.py


def _check_scalar(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any],
) -> type:
    """Validate a scalar Python type provided in a bertrand type's namespace or infer
    it from a provided dtype.
    """
    if "scalar" in processed:  # auto-generated in check_dtype
        return processed["scalar"]

    dtype = namespace.get("dtype", EMPTY)

    if value is EMPTY:
        if dtype is EMPTY:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(dtype, (np.dtype, pd.api.extensions.ExtensionDtype)):
            raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(dtype)}")

        processed["dtype"] = dtype
        return dtype.type

    if not isinstance(value, type):
        raise TypeError(f"scalar must be a Python type object, not {repr(value)}")

    if dtype is EMPTY:
        processed["dtype"] = None  # TODO: synthesize dtype
    else:
        processed["dtype"] = dtype
    # elif value == dtype.type:
    #     processed["dtype"] = dtype
    # else:
    #     raise TypeError(
    #         f"scalar must be consistent with dtype.type: {repr(value)} != "
    #         f"{repr(dtype.type)}"
    #     )

    return value


def _check_dtype(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> DTYPE:
    """Validate a numpy or pandas dtype provided in a bertrand type's namespace or
    infer it from a provided scalar.
    """
    if "dtype" in processed:  # auto-generated in check_scalar
        return processed["dtype"]

    scalar = namespace.get("scalar", EMPTY)

    if value is EMPTY:
        if scalar is EMPTY:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(scalar, type):
            raise TypeError(f"scalar must be a Python type object, not {repr(scalar)}")

        processed["scalar"] = scalar
        return None  # TODO: synthesize dtype

    if not isinstance(value, (np.dtype, pd.api.extensions.ExtensionDtype)):
        raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(value)}")

    if scalar is EMPTY:
        processed["scalar"] = value.type
    else:
        processed["scalar"] = scalar
    # elif value.type == scalar:
    #     processed["scalar"] = scalar
    # else:
    #     raise TypeError(
    #         f"dtype.type must be consistent with scalar: {repr(value.type)} != "
    #         f"{repr(scalar)}"
    #     )

    return value


def _check_itemsize(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int:
    """Validate an itemsize provided in a bertrand type's namespace."""
    if value is EMPTY:
        if "dtype" in namespace:
            return namespace["dtype"].itemsize
        if "dtype" in processed:
            return processed["dtype"].itemsize

    if not isinstance(value, int) or value <= 0:
        raise TypeError(f"itemsize must be a positive integer, not {repr(value)}")

    return value


def _check_max(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a maximum value provided in a bertrand type's namespace."""
    if "max" in processed:  # auto-generated in check_min
        return processed["max"]

    min_val = namespace.get("min", -np.inf)

    if value is EMPTY:
        processed["min"] = min_val
        return np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value < min_val:
        raise TypeError(f"max must be greater than min: {value} < {min_val}")

    processed["min"] = min_val
    return value


def _check_min(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a minimum value provided in a bertrand type's namespace."""
    if "min" in processed:  # auto-generated in check_max
        return processed["min"]

    max_val = namespace.get("max", np.inf)

    if value is EMPTY:
        processed["max"] = max_val
        return -np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == -np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value > max_val:
        raise TypeError(f"min must be less than or equal to max: {value} > {max_val}")

    processed["max"] = max_val
    return value


def _check_is_nullable(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> bool:
    """Validate a nullability flag provided in a bertrand type's namespace."""
    return True if value is EMPTY else bool(value)


def _check_missing(
    value: Any | Empty,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> Any:
    """Validate a missing value provided in a bertrand type's namespace."""
    if value is EMPTY:
        return pd.NA

    if not pd.isna(value):
        raise TypeError(f"missing value must pass a pandas.isna() check: {repr(value)}")

    return value


class Base:
    """TODO
    """

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand types cannot be instantiated")


class Type(Base, metaclass=TypeMeta):
    """Parent class for all scalar types.

    Inheriting from this class triggers the metaclass machinery, which automatically
    validates and registers the inheriting type.  As such, any class that inherits from
    this type becomes the root of a new type hierarchy, which can be extended to create
    tree structures of arbitrary depth.
    """

    # NOTE: every type has a collection of strings, python types, and numpy/pandas
    # dtype objects which are used to identify the type during detect() and resolve()
    # calls.  Here's how they work
    #   1.  Python types (e.g. int, str, etc.) - used during elementwise detect() to
    #       identify scalar values.  If defined, the type's `from_scalar()` method will
    #       be called automatically to parse each element.
    #   2.  numpy/pandas dtype objects (e.g. np.dtype("i4")) - used during detect() to
    #       identify array-like containers that implement a `.dtype` attribute.  If
    #       defined, the type's `from_dtype()` method will be called automatically to
    #       parse the dtype.  The same process also occurs whenever resolve() is called
    #       with a literal dtype argument.
    #   3.  Strings - used during resolve() to identify types by their aliases.
    #       Optional arguments can be provided in square brackets after the alias, and
    #       will be tokenized and passed to the type's `from_string()` method, if it
    #       exists.  Note that a type's aliases implicitly include its class name, so
    #       that type hints containing the type can be identified when
    #       `from __future__ import annotations` is enabled.  It is good practice to
    #       follow suit with other aliases so they can be used in type hints as well.

    aliases = {"foo", int, type(np.dtype("i4"))}  # <- for example (not evaluated)

    # NOTE: Any field assigned to EMPTY is a required attribute that must be defined
    # by concretions of this type.  Each field can be inherited from a parent type
    # with normal semantics.  If a type does not define a required attribute, then the
    # validation function will receive EMPTY as its value, and must either raise an
    # error or replace it with a default value.

    scalar:         type            = EMPTY(_check_scalar)  # type: ignore
    dtype:          DTYPE           = EMPTY(_check_dtype)  # type: ignore
    itemsize:       int             = EMPTY(_check_itemsize)  # type: ignore
    max:            int | float     = EMPTY(_check_max)  # type: ignore
    min:            int | float     = EMPTY(_check_min)  # type: ignore
    is_nullable:    bool            = EMPTY(_check_is_nullable)  # type: ignore
    missing:        Any             = EMPTY(_check_missing)

    # NOTE: the following methods can be used to allow parametrization of a type using
    # an integrated flyweight cache.  At minimum, a parametrized type must implement
    # `__class_getitem__`, which specifies the parameter names and default values, as
    # well as `from_string`, which parses the equivalent syntax in the
    # type-specification mini-language.  The other methods are optional, and can be
    # used to customize the behavior of the detect() and resolve() helper functions.

    # None of these methods will be inherited by subclasses, so their behavior is
    # unique for each type.  If they are not defined, they will default to the
    # identity function, which will be optimized away where possible to improve
    # performance.

    def __class_getitem__(cls, spam: str = "foo", eggs: int = 2) -> TypeMeta:
        """Example implementation showing how to create a parametrized type.

        When this method is defined, the metaclass will analyze its signature and
        extract any default values, which will be forwarded to the class's
        :attr:`params <Type.params>` attribute, as well as its base namespace.  Each
        argument must have a default value, and the signature must not contain any
        keyword or variadic arguments (i.e. *args or **kwargs).

        Positional arguments to the flyweight() helper method should be supplied in
        the same order as they appear in the signature, and will be used to form the
        string identifier for the type.  Keywords are piped directly into the resulting
        class's namespace, and will override any existing values.  In the interest of
        speed, no checks are performed on the arguments, so it is up to the user to
        ensure that they are in the expected format.
        """
        return cls.flyweight(spam, eggs, dtype=np.dtype("i4"))

    @classmethod
    def from_string(cls, spam: str, eggs: str) -> TypeMeta:
        """Example implementation showing how to parse a string in the
        type-specification mini-language.

        This method will be called automatically whenever :func:`resolve` encounters
        an alias that matches this type.  Its signature must match that of
        :meth:`__class_getitem__`, and it will be invoked with the comma-separated
        tokens parsed from the alias's argument list.  They are always provided as
        strings, with no further processing other than stripping leading/trailing
        whitespace.  It is up to the user to parse them into the appropriate type.
        """
        return cls.flyweight(spam, int(eggs), dtype=np.dtype("i4"))

    @classmethod
    def from_scalar(cls, scalar: Any) -> TypeMeta:
        """Example implementation showing how to parse a scalar value into a
        parametrized type.

        This method will be called during the main :func:`detect` loop whenever a
        scalar or unlabeled sequence of scalars is encountered.  It should be fast,
        since it will be called for every element in the array.  If left blank, it will
        """
        return cls.flyweight(scalar.spam, scalar.eggs(1, 2, 3), dtype=scalar.ham)

    @classmethod
    def from_dtype(cls, dtype: Any) -> TypeMeta:
        """Example implementation showing how to parse a numpy/pandas dtype into a
        parametrized type.
        
        This method will be called whenever :func:`detect` encounters array-like data
        with a related dtype, or when :func:`resolve` is called with a dtype argument.
        """
        return cls.flyweight(dtype.unit, dtype.step_size, dtype=dtype)

    # NOTE: the following methods are provided as default implementations inherited by
    # descendent types.  They can be overridden to customize their behavior, although
    # doing so is not recommended unless absolutely necessary.  Typically, users should
    # invoke the parent method via super() and modify the input/output, rather than
    # reimplementing the method from scratch.

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        """Base implementation of the replace() method, which is inherited by all
        descendant types.
        """
        forwarded = dict(cls.params)
        positional = collections.deque(args)
        n  = len(args)
        i = 0
        for k in forwarded:
            if i < n:
                forwarded[k] = positional.popleft()
            elif k in kwargs:
                forwarded[k] = kwargs.pop(k)

        if positional:
            raise TypeError(
                f"replace() takes at most {len(cls.params)} positional arguments but "
                f"{n} were given"
            )
        if kwargs:
            singular = len(kwargs) == 1
            raise TypeError(
                f"replace() got {'an ' if singular else ''}unexpected keyword argument"
                f"{'' if singular else 's'} [{', '.join(repr(k) for k in kwargs)}]"
            )

        return cls.base_type[*forwarded.values()]


class DecoratorType(Base, metaclass=DecoratorMeta):
    """TODO
    """

    # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

    # TODO: __class_getitem__(), from_scalar(), from_dtype(), from_string()

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Base implementation of the transform() method, which is inherited by all
        descendant types.
        """
        return series.astype(cls.dtype, copy=False)

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Base implementation of the inverse_transform() method, which is inherited by
        all descendant types.
        """
        return series.astype(cls.wrapped.dtype, copy=False)

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """Copied from TypeMeta."""
        forwarded = dict(cls.params)
        positional = collections.deque(args)
        n  = len(args)
        i = 0
        for k in forwarded:
            if i < n:
                i += 1
                forwarded[k] = positional.popleft()
            elif k in kwargs:
                forwarded[k] = kwargs.pop(k)

        if positional:
            raise TypeError(
                f"replace() takes at most {len(cls.params)} positional arguments but "
                f"{n} were given"
            )
        if kwargs:
            singular = len(kwargs) == 1
            raise TypeError(
                f"replace() got {'an ' if singular else ''}unexpected keyword argument"
                f"{'' if singular else 's'} [{', '.join(repr(k) for k in kwargs)}]"
            )

        return cls.base_type[*forwarded.values()]


class Union(Base, metaclass=UnionMeta):
    """TODO
    """

    def __class_getitem__(cls, types: TYPESPEC | tuple[TYPESPEC, ...]) -> UnionMeta:
        typ = resolve(types)
        if isinstance(typ, UnionMeta):
            return typ
        return cls.from_types(LinkedSet((typ,)))


class StructuredUnion(Union, metaclass=StructuredMeta):
    """TODO
    """















####################
####    TEST    ####
####################


class Int(Type):
    aliases = {"int", "integer"}


@Int.default
class Signed(Int):
    aliases = {"signed"}


class PythonInt(Signed, backend="python"):
    aliases = {int}
    scalar = int
    dtype = np.dtype(object)
    max = np.inf
    min = -np.inf
    is_nullable = True
    missing = None



@Signed.default
class Int64(Signed):
    aliases = {"int64", "long long"}
    max = 2**63 - 1
    min = -2**63


@Int64.default
class NumpyInt64(Int64, backend="numpy"):
    aliases = {np.int64, np.dtype(np.int64)}
    dtype = np.dtype(np.int64)
    is_nullable = False


@NumpyInt64.nullable
class PandasInt64(Int64, backend="pandas"):
    aliases = {pd.Int64Dtype()}
    dtype = pd.Int64Dtype()
    is_nullable = True




class Int32(Signed):
    aliases = {"int32", "long"}
    max = 2**31 - 1
    min = -2**31


@Int32.default
class NumpyInt32(Int32, backend="numpy"):
    aliases = {np.int32, np.dtype(np.int32)}
    dtype = np.dtype(np.int32)
    is_nullable = False


@NumpyInt32.nullable
class PandasInt32(Int32, backend="pandas"):
    aliases = {pd.Int32Dtype()}
    dtype = pd.Int32Dtype()
    is_nullable = True




class Int16(Signed):
    aliases = {"int16", "short"}
    max = 2**15 - 1
    min = -2**15
    missing = np.nan


@Int16.default
class NumpyInt16(Int16, backend="numpy"):
    aliases = {np.int16, np.dtype(np.int16)}
    dtype = np.dtype(np.int16)
    is_nullable = False


@NumpyInt16.nullable
class PandasInt16(Int16, backend="pandas"):
    aliases = {pd.Int16Dtype()}
    dtype = pd.Int16Dtype()
    is_nullable = True




class Int8(Signed):
    aliases = {"int8", "char"}
    max = 2**7 - 1
    min = -2**7


@Int8.default
class NumpyInt8(Int8, backend="numpy", cache_size=2):
    aliases = {np.int8, np.dtype(np.int8)}
    dtype = np.dtype(np.int8)
    is_nullable = False

    def __class_getitem__(cls, arg: Any = "i8") -> TypeMeta:  # type: ignore
        return cls.flyweight(arg, dtype=np.dtype(arg))

    @classmethod
    def from_string(cls, arg: str) -> TypeMeta:  # type: ignore
        """TODO"""
        return cls[arg]


@NumpyInt8.nullable
class PandasInt8(Int8, backend="pandas"):
    aliases = {pd.Int8Dtype()}
    dtype = pd.Int8Dtype()
    is_nullable = True






class Categorical(DecoratorType, cache_size=256):
    aliases = {"categorical", pd.CategoricalDtype()}

    def __class_getitem__(
        cls,
        wrapped: TypeMeta | DecoratorMeta | None = None,
        levels: Iterable[Any] | Empty = EMPTY
    ) -> DecoratorMeta:
        """TODO"""
        if wrapped is None:
            raise NotImplementedError("TODO")  # should never occur

        if levels is EMPTY:
            slug_repr = levels
            dtype = pd.CategoricalDtype()  # NOTE: auto-detects levels when used
            patch = wrapped
            while not patch.is_default:
                patch = patch.as_default
            dtype._bertrand_wrapped_type = patch  # disambiguates wrapped type
        else:
            levels = wrapped(levels)
            slug_repr = list(levels)
            dtype = pd.CategoricalDtype(levels)

        return cls.flyweight(wrapped, slug_repr, dtype=dtype, levels=levels)

    @classmethod
    def from_dtype(cls, dtype: pd.CategoricalDtype) -> DecoratorMeta:
        """TODO"""
        if dtype.categories is None:
            if hasattr(dtype, "_bertrand_wrapped_type"):  # type: ignore
                return cls[dtype._bertrand_wrapped_type]
            return cls
        return cls[resolve(dtype.categories.dtype), dtype.categories]

    @classmethod
    def from_string(cls, wrapped: str, levels: str | Empty = EMPTY) -> DecoratorMeta:
        """TODO"""
        if levels is EMPTY:
            return cls[resolve(wrapped), levels]

        if levels.startswith("[") and levels.endswith("]"):
            stripped = levels[1:-1].strip()
        elif levels.startswith("(") and levels.endswith(")"):
            stripped = levels[1:-1].strip()
        else:
            raise TypeError(f"invalid levels: {repr(levels)}")

        breakpoint()
        return cls[resolve(wrapped), list(s.group() for s in TOKEN.finditer(stripped))]

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """TODO"""
        if cls.dtype.categories is None:
            dtype = pd.CategoricalDtype(cls.wrapped(series.unique()))
        else:
            dtype = cls.dtype
        return series.astype(dtype, copy=False)


class Sparse(DecoratorType, cache_size=256):
    aliases = {"sparse", pd.SparseDtype()}
    _is_empty = False

    # TODO: Sparse[Categorical] does not work

    def __class_getitem__(
        cls,
        wrapped: TypeMeta | DecoratorMeta = None,  # type: ignore
        fill_value: Any | Empty = EMPTY
    ) -> DecoratorMeta:
        """TODO"""
        # NOTE: metaclass automatically raises an error when wrapped is None.  Listing
        # it as such in the signature just sets the default value.
        is_empty = fill_value is EMPTY

        if isinstance(wrapped.unwrapped, DecoratorMeta):
            return cls.flyweight(wrapped, fill_value, dtype=None, _is_empty=is_empty)

        if is_empty:
            fill = wrapped.missing
        elif pd.isna(fill_value):
            fill = fill_value
        else:
            fill = wrapped(fill_value)
            if len(fill) != 1:
                raise TypeError(f"fill_value must be a scalar, not {repr(fill_value)}")
            fill = fill[0]

        dtype = pd.SparseDtype(wrapped.dtype, fill)
        return cls.flyweight(wrapped, fill, dtype=dtype, _is_empty=is_empty)

    @classmethod
    def from_dtype(cls, dtype: pd.SparseDtype) -> DecoratorMeta:
        """TODO"""
        return cls[resolve(dtype.subtype), dtype.fill_value]

    @classmethod
    def from_string(cls, wrapped: str, fill_value: str | Empty = EMPTY) -> DecoratorMeta:
        """TODO"""
        return cls[resolve(wrapped), REGISTRY.na_strings.get(fill_value, fill_value)]

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """TODO"""
        if cls._is_empty and len(args) < 2 and "fill_value" not in kwargs:
            kwargs["fill_value"] = EMPTY
        return super().replace(*args, **kwargs)

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """TODO"""
        return series.sparse.to_dense()





if DEBUG:
    print("=" * 80)
    print(f"aliases: {REGISTRY.aliases}")
    print()
