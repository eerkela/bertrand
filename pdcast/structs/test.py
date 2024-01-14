"""This module defines the core of the bertrand type system, including the metaclass
machinery, type primitives, and helper functions for detecting and resolving types.
"""
from __future__ import annotations
import collections
import inspect
from types import MappingProxyType, UnionType
from typing import (
    Any, Callable, ItemsView, Iterable, Iterator, KeysView, Mapping, NoReturn,
    TypeAlias, ValuesView
)

import numpy as np
import pandas as pd
import regex as re  # alternate (PCRE-style) regex engine

from linked import LinkedSet, LinkedDict


# pylint: disable=unused-argument


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
META: TypeAlias = "TypeMeta | DecoratorMeta | UnionMeta"
DTYPE: TypeAlias = np.dtype[Any] | pd.api.extensions.ExtensionDtype
ALIAS: TypeAlias = str | type | DTYPE
TYPESPEC: TypeAlias = "META | ALIAS"
OBJECT_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[np.object_]]
RLE_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[tuple[np.object_, np.intp]]]


########################
####    REGISTRY    ####
########################


# TODO: metaclass throws error if any non-classmethods are implemented on type objects.
# -> non-member functions are more explicit and flexible.  Plus, if there's only one
# syntax for declaring them, then it's easier to document and use.
# -> just filter out any methods that start/end with double underscore


# TODO: it might be generally easier if the cases where None is returned (i.e. parent,
# wrapped) returned a self reference to the type they were called on.  That would
# simplify union implementation somewhat.


# TODO: change every reference to
# - parent (never None)
# - wrapped (never None)
# - children (includes self)
# - leaves (may include self)
# - 


class DecoratorPrecedence:
    """A linked set representing the priority given to each decorator in the type
    system.

    This interface is used to determine the order in which decorators are nested when
    applied to the same type.  For instance, if the precedence is set to `{A, B, C}`,
    and each is applied to a type `T`, then the resulting type will always be
    `A[B[C[T]]]`, no matter what order the decorators are applied in.  To demonstrate:

    .. code-block:: python

        >>> Type.registry.decorators
        DecoratorPrecedence({A, B, C})
        >>> T
        T
        >>> B[T]
        B[T]
        >>> C[B[T]]
        B[C[T]]
        >>> B[C[A[T]]]
        A[B[C[T]]]

    At each step, the decorator that is furthest to the right is applied first, and the
    decorator furthest to the left is applied last.  We can modify this by changing the
    precedence order:

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
    _edges: set[tuple[TypeMeta, TypeMeta]] = set()

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
    def edges(self) -> set[tuple[TypeMeta, TypeMeta]]:
        """A set of edges (A, B) where A is considered to be less than B.

        Returns
        -------
        set[tuple[TypeMeta, TypeMeta]]
            A set of edges representing overrides for the less-than and greater-than
            operators.  Each edge is a tuple of two types, where the first type is
            always considered to be less than the second type.

        Examples
        --------
        By adding an edge to this set, users can customize the behavior of the
        comparison operators for a particular type.

        .. doctest::

            >>> REGISTRY.edges
            set()
            >>> Int16 < Int32
            True
            >>> Int32 < Int16
            False
            >>> REGISTRY.edges.add((Int32, Int16))
            >>> Int16 < Int32
            False
            >>> Int32 < Int16
            True

        Note that this also affects the order of the :attr:`Type.larger` and
        :attr:`Type.smaller` attributes.

        .. doctest::

            >>> Int8.larger
            Union[NumpyInt32, NumpyInt16, PandasInt64, PythonInt]
            >>> Int64.smaller
            Union[NumpyInt8, NumpyInt32, NumpyInt16]

        Note also that edges that are applied to abstract parents will be propagated
        to all their children.

        .. doctest::

            >>> Int32["numpy"] < Int16
            True
            >>> Int32["pandas"] < Int16
            True
            >>> Int32["numpy"] < Int16["numpy"]
            True
            >>> Int32["pandas] < Int16["numpy"]
            True
            >>> Int32["numpy"] < Int16["pandas"]
            True
            >>> Int32["pandas] < Int16["pandas"]
            True

        Lastly, edges are automatically inverted for the greater-than operator.

        .. doctest::

            >>> Int32 > Int16
            False
            >>> Int16 > Int32
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
            return typ.wrapper in self._types

        for t in typ:
            if isinstance(t, TypeMeta):
                if (t.parent if t.is_flyweight else t) not in self._types:
                    return False
            elif isinstance(t, DecoratorMeta):
                if t.wrapper not in self._types:
                    return False

        return True

    def __str__(self) -> str:
        return f"{', '.join(str(t) for t in self)}"

    def __repr__(self) -> str:
        return f"TypeRegistry({{{', '.join(repr(t) for t in self)}}})"


REGISTRY = TypeRegistry()


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


class BaseMeta(type):
    """Base class for all bertrand metaclasses.

    This is not meant to be used directly.  It exists only to provide shared methods
    and attributes to the other metaclasses.
    """
    # pylint: disable=no-value-for-parameter

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

    def __getitem__(cls, params: Any | tuple[Any, ...]) -> META:
        """A wrapper around a type's `__class_getitem__` method that automatically
        unpacks tuples into a more traditional call syntax.
        """
        if isinstance(params, tuple):
            return cls.__class_getitem__(*params)  # type: ignore
        return cls.__class_getitem__(params)  # type: ignore

    def __setattr__(cls, name: str, val: Any) -> NoReturn:
        raise TypeError("bertrand types are immutable")

    def __instancecheck__(cls, other: Any | Iterable[Any]) -> bool:
        return cls.__subclasscheck__(detect(other))

    def __contains__(cls, other: Any | TYPESPEC | Iterable[Any | TYPESPEC]) -> bool:
        try:
            return cls.__subclasscheck__(other)  # type: ignore
        except TypeError:
            return cls.__instancecheck__(other)

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

    # RESERVED: set[str] = set(dir(TypeMeta)) ^ {
    #     "__init__",
    #     "__module__",
    #     "__qualname__",
    #     "__annotations__",
    # }

    # reserved attributes will throw an error if implemented on user-defined types
    RESERVED: set[str] = {
        "__init__",
        "__new__",
        # TODO: others?
    }

    # constructors use special logic to allow for parametrization
    CONSTRUCTORS: set[str] = {
        "__class_getitem__",
        "from_scalar",
        "from_dtype",
        "from_string"
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
        if parent is object:
            self.required = {}
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
        self.namespace["_required"] = self.required  # TODO: not relevant for decorators
        self.namespace["_fields"] = self.fields
        self.namespace["_slug"] = self.name
        self.namespace["_hash"] = hash(self.name)
        if self.parent is object or self.parent is Type or self.parent is DecoratorType:
            self.namespace["_parent"] = None
        else:
            self.namespace["_parent"] = self.parent
        self.namespace["_parametrized"] = False
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict(max_size=self.cache_size)

        self._parse_aliases()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Push a newly-created type to the global registry, registering any aliases
        provided in its namespace.

        Parameters
        ----------
        typ : TypeMeta | DecoratorMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        TypeMeta | DecoratorMeta
            The registered type.
        """
        typ.aliases.parent = typ  # pushes aliases to global registry
        REGISTRY._types.add(typ)  # pylint: disable=protected-access
        return typ

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

    def _parse_aliases(self) -> None:
        """Parse a type's aliases field (if it has one), registering each one in the
        global type registry.
        """
        if "aliases" in self.namespace:
            aliases = LinkedSet[str | type]()
            for alias in self.namespace.pop("aliases"):
                if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                    aliases.add(type(alias))
                else:
                    aliases.add(alias)

            aliases.add_left(self.name)
            self.namespace["_aliases"] = Aliases(aliases)

        else:
            self.namespace["_aliases"] = Aliases(LinkedSet[str | type]({self.name}))


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
# within Union[] specifiers (i.e. Union[{"foo": ...}], Union[[("foo", ...)]]).  This
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
    if isinstance(target, (TypeMeta, DecoratorMeta, UnionMeta)):
        return target

    if isinstance(target, type):
        if target in REGISTRY.types:
            return REGISTRY.types[target]
        return Object[target]

    if isinstance(target, UnionType):  # python int | float | None syntax, not bertrand
        return Union.from_types(LinkedSet(resolve(t) for t in target.__args__))

    if isinstance(target, np.dtype):
        if target.fields:
            return Union.from_columns({  # type: ignore
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
        return Union.from_columns({target.start: resolve(target.stop)})

    if isinstance(target, dict):
        return Union.from_columns({
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
                slices[item.start] = resolve(item.stop)
            return Union.from_columns(slices)

        if isinstance(first, (list, tuple)):
            if len(first) != 2:
                raise TypeError(f"expected a tuple of length 2 -> {repr(first)}")

            pairs = {first[0]: resolve(first[1])}
            for item in it:
                if not isinstance(item, (list, tuple)) or len(item) != 2:
                    raise TypeError(f"expected a tuple of length 2 -> {repr(item)}")
                pairs[item[0]] = resolve(item[1])
            return Union.from_columns(pairs)

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
        return Union.from_columns(columns)

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

    if issubclass(data_type, (TypeMeta, DecoratorMeta, UnionMeta)):
        return data

    if issubclass(data_type, pd.DataFrame):
        return Union.from_columns({
            col: _detect_dtype(data[col], drop_na=drop_na) for col in data.columns
        })

    if issubclass(data_type, np.ndarray) and data.dtype.fields:
        return Union.from_columns({
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
            if isinstance(result, UnionMeta):
                index[~is_na] = result.index  # pylint: disable=invalid-unary-operand-type
            else:
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


############################
####    SCALAR TYPES    ####
############################


class TypeMeta(BaseMeta):
    """Metaclass for all scalar bertrand types (those that inherit from bertrand.Type).

    This metaclass is responsible for parsing the namespace of a new type, validating
    its configuration, and adding it to the global type registry.  It also defines the
    basic behavior of all bertrand types, including the establishment of hierarchical
    relationships, parametrization, and operator overloading.

    See the documentation for the `Type` class for more information on how these work.
    """

    # This metaclass uses the following internal fields, as populated by AbstractBuilder
    # or ConcreteBuilder based on the `backend` metaclass argument
    _slug: str
    _hash: int
    _required: dict[str, Callable[..., Any]]
    _fields: dict[str, Any]
    _parent: TypeMeta | None
    _subtypes: LinkedSet[TypeMeta]
    _implementations: LinkedDict[str, TypeMeta]
    _default: list[TypeMeta]
    _nullable: list[TypeMeta]
    _params: tuple[str]
    _parametrized: bool
    _cache_size: int | None
    _flyweights: LinkedDict[str, TypeMeta]
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
        fields = {k: "..." for k in cls._required}
        fields |= cls._fields

        if not (len(bases) == 0 or bases[0] is object):
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
            return abstract.parse().fill().register(
                super().__new__(  # type: ignore
                    mcs,
                    abstract.name,
                    (abstract.parent,),
                    abstract.namespace
                )
            )

        concrete = ConcreteBuilder(name, bases, namespace, backend, cache_size)
        return concrete.parse().fill().register(
            super().__new__(  # type: ignore
                mcs,
                concrete.name,
                (concrete.parent,),
                concrete.namespace
            )
        )

    def flyweight(cls, *args: Any, **kwargs: Any) -> TypeMeta:
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
        TypeMeta
            A flyweight type with the specified parameters.

        Raises
        ------
        TypeError
            If the number of positional arguments does not match the signature of
            `__class_getitem__()`.  Note that order is not checked, so users should
            take care to ensure that the arguments are passed in the correct order.

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
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls._flyweights.lru_get(slug, None)

        if typ is None:
            if len(args) != len(cls._params):
                raise TypeError(
                    f"expected {len(cls._params)} positional arguments, got "
                    f"{len(args)}"
                )

            def __class_getitem__(cls: type, *args: Any) -> NoReturn:
                raise TypeError(f"{slug} cannot be re-parametrized")

            typ = super().__new__(
                TypeMeta,
                cls.__name__,
                (cls,),
                {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,  # TODO: unnecessary if we modify approach to _fields
                    "_parent": cls,
                    "_parametrized": True,
                    "__class_getitem__": __class_getitem__
                } | kwargs
            )
            cls._flyweights.lru_add(slug, typ)

        return typ

    ################################
    ####    CLASS DECORATORS    ####
    ################################

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
        if not cls.is_abstract:
            raise TypeError("concrete types cannot have default implementations")

        if not issubclass(other, cls):
            raise TypeError(
                f"default implementation must be a subclass of {cls.__name__}"
            )

        if cls._default:
            cls._default.pop()
        cls._default.append(other)
        return other

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

        if cls._nullable:
            cls._nullable.pop()
        cls._nullable.append(other)
        return other

    @property
    def as_default(cls) -> TypeMeta:
        """Convert an abstract type to its default implementation, if it has one.

        Returns
        -------
        TypeMeta
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
        if cls._default:
            return cls._default[0]
        return cls

    @property
    def as_nullable(cls) -> TypeMeta:
        """Convert a concrete type to its nullable implementation, if it has one.

        Returns
        -------
        TypeMeta
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
        if cls.is_nullable:
            return cls
        if cls._nullable:
            return cls._nullable[0]
        raise TypeError(f"type is not nullable -> {cls.slug}")

    ################################
    ####    CLASS PROPERTIES    ####
    ################################

    @property
    def aliases(cls) -> Aliases:
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
        return cls._aliases

    @property
    def scalar(cls) -> type:
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
        if not cls.is_default:
            return cls.as_default.scalar

        result = cls._fields.get("scalar", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'scalar'"
            )
        return result

    @property
    def dtype(cls) -> DTYPE:
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
        if not cls.is_default:
            return cls.as_default.dtype

        result = cls._fields.get("dtype", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'dtype'"
            )
        return result

    @property
    def max(cls) -> int | float:
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
        if not cls.is_default:
            return cls.as_default.max

        result = cls._fields.get("max", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'max'"
            )
        return result

    @property
    def min(cls) -> int | float:
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
        if not cls.is_default:
            return cls.as_default.min

        result = cls._fields.get("min", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'min'"
            )
        return result

    @property
    def missing(cls) -> Any:
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
        if not cls.is_default:
            return cls.as_default.missing

        result = cls._fields.get("missing", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'missing'"
            )
        return result

    @property
    def slug(cls) -> str:
        """Get a string identifier for this type, as used to look up flyweights.

        Returns
        -------
        str
            A string concatenating the type's class name with its parameter
            configuration, if any.
        """
        return cls._slug

    @property
    def hash(cls) -> int:
        """Get a precomputed hash value for this type.

        Returns
        -------
        int
            A unique hash based on the type's string identifier.

        Notes
        -----
        This is identical to the output of the :func:`hash` built-in function.
        """
        return cls._hash

    @property
    def backend(cls) -> str:
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
        return cls._fields["backend"]

    @property
    def itemsize(cls) -> int:
        """The size of a single element of this type, in bytes.

        Returns
        -------
        int
            The width of this type in memory.
    
        Notes
        -----
        This is identical to the result of the :func:`len` operator when applied
        to a scalar type.  This behaves similarly to a Python-level ``sizeof()``
        operator as used in C/C++.

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
        return cls.dtype.itemsize

    @property
    def cache_size(cls) -> int | None:
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
        return cls._cache_size

    @property
    def flyweights(cls) -> Mapping[str, TypeMeta]:
        """A read-only proxy for this type's flyweight cache.

        Returns
        -------
        Mapping[str, TypeMeta]
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
        return MappingProxyType(cls._flyweights)

    @property
    def params(cls) -> Mapping[str, Any]:
        """A read-only mapping of parameters to their configured values.

        Returns
        -------
        Mapping[str, Any]
            A mapping of parameter names to their values.  These are determined by
            to :meth:`Type.__class_getitem__()` when the type is parametrized.

        Notes
        -----
        The parameter names and default values are inferred from the signature of
        :meth:`Type.__class_getitem__()`, and are available as dotted attributes
        on the type itself.

        Examples
        --------
        .. doctest::

            >>> Datetime["pandas"].params
            mappingproxy({'tz': None})
            >>> Datetime["pandas"].tz is None
            True
            >>> Datetime["pandas", "UTC"].params
            mappingproxy({'tz': datetime.timezone.utc})
            >>> Datetime["pandas", "UTC"].tz
            datetime.timezone.utc
        """
        return MappingProxyType({k: getattr(cls, k) for k in cls._params})

    @property
    def is_abstract(cls) -> bool:
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
        return not cls.backend

    @property
    def is_decorator(cls) -> bool:
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
        return False

    @property
    def is_default(cls) -> bool:
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
        return not cls._default

    @property
    def is_flyweight(cls) -> bool:
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
        return cls._parametrized

    # TODO: note that is_leaf does not necessarily imply type is concrete

    @property
    def is_leaf(cls) -> bool:
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
        return not cls._subtypes and not cls._implementations

    @property
    def is_nullable(cls) -> bool:
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
        if not cls.is_default:
            return cls.as_default.is_nullable

        result = cls._fields.get("is_nullable", None)
        if result is None:
            raise AttributeError(
                f"type object '{cls.__name__}' has no attribute 'is_nullable'"
            )
        return result

    @property
    def is_root(cls) -> bool:
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
        return cls._parent is None

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
    def root(cls) -> TypeMeta:
        """Get the root of this type's hierarchy.

        Returns
        -------
        TypeMeta
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
        # pylint: disable=protected-access
        result = cls
        while result._parent is not None:
            result = result._parent
        return result

    @property
    def parent(cls) -> TypeMeta:
        """Get the immediate parent of this type within the hierarchy.

        Returns
        -------
        TypeMeta | None
            The type that this type directly inherits from or None if :attr:`is_root`
            is True.

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
        if cls._parent is None:
            return cls
        return cls._parent

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
        return Union.from_types(cls._subtypes)

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
        return Union.from_types(LinkedSet(cls._implementations.values()))

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
        return Union.from_types(cls._children)

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
        return Union.from_types(LinkedSet(t for t in cls._children if t.is_leaf))

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

    @property
    def decorators(cls) -> UnionMeta:
        """A union containing all the decorators applied to this type.

        Returns
        -------
        UnionMeta
            A union containing all the decorators that are attached to this type.

        See Also
        --------
        wrapper: convert the outermost decorator into its unparametrized base type.
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
        return Union.from_types(LinkedSet())

    @property
    def wrapper(cls) -> None:  # pylint: disable=redundant-returns-doc
        """Return the outermost decorator as an unparametrized type.

        Returns
        -------
        DecoratorMeta | None
            A generic version of the outermost decorator, or None if the type is not
            decorated.

        See Also
        --------
        decorators: a union containing all the decorators that are attached to this type.
        wrapped: the type that this type is decorating.
        unwrapped: the innermost non-decorator type.

        Examples
        --------
        .. doctest::

            >>> Int.wrapper
            None
            >>> Sparse[Int].wrapper
            Sparse
            >>> Categorical[Sparse[Int]].wrapper
            Categorical
        """
        return None

    @property
    def wrapped(cls) -> TypeMeta:  # pylint: disable=redundant-returns-doc
        """Return the type that this type is decorating.

        Returns
        -------
        TypeMeta | DecoratorMeta
            If the type is a parametrized decorator, then the type that it wraps.
            Otherwise, a self reference to the current type.

        See Also
        --------
        decorators: a union containing all the decorators that are attached to this type.
        wrapper: convert the outermost decorator into its unparametrized base type.
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
        return cls

    @property
    def unwrapped(cls) -> TypeMeta:
        """Return the innermost non-decorator type in the stack.

        Returns
        -------
        TypeMeta
            The innermost type in the stack that is not a parametrized decorator, or a
            self-reference if the type is not decorated.

        See Also
        --------
        decorators: a union containing all the decorators that are attached to this type.
        wrapper: convert the outermost decorator into its unparametrized base type.
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
        return cls

    def __getattr__(cls, name: str) -> Any:
        if cls._default:
            return getattr(cls.as_default, name)

        if name in cls._fields:
            return cls._fields[name]

        raise AttributeError(
            f"type object '{cls.__name__}' has no attribute '{name}'"
        )

    def __dir__(cls) -> set[str]:
        result = set(super().__dir__())
        result.update({
            "slug", "hash", "backend", "itemsize", "decorators", "wrapped",
            "unwrapped", "cache_size", "flyweights", "params", "is_abstract",
            "is_default", "is_flyweight", "is_root", "is_leaf", "root", "parent",
            "subtypes", "implementations", "children", "leaves", "larger", "smaller"
        })
        result.update(cls._fields)
        end = cls
        while not end.is_default:
            end = end.as_default
            result.update(end._fields)
        return result

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls._default:
            return cls.as_default(*args, **kwargs)
        return pd.Series(*args, dtype=cls.dtype, **kwargs)

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        check = super().__subclasscheck__
        if isinstance(typ, UnionMeta):
            return all(check(t) for t in typ)
        return check(typ)

    def __hash__(cls) -> int:
        return cls._hash

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

        t1 = cls
        t2 = typ.parent if typ.is_flyweight else typ  # TODO: might have problems if t2 is a decorator
        while not t2.is_root:
            while not t1.is_root:
                if (t1, t2) in REGISTRY.edges:
                    return True
                if (t2, t1) in REGISTRY.edges:
                    return False
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent

        return _features(cls) < _features(typ)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls <= t for t in typ)

        t1 = cls
        t2 = typ.parent if typ.is_flyweight else typ  # TODO: might have problems if t2 is a decorator
        while not t2.is_root:
            while not t1.is_root:
                if (t1, t2) in REGISTRY.edges:
                    return True
                if (t2, t1) in REGISTRY.edges:
                    return False
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent

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

        t1 = cls
        t2 = typ.parent if typ.is_flyweight else typ  # TODO: might have problems if t2 is a decorator
        while not t2.is_root:
            while not t1.is_root:
                if (t1, t2) in REGISTRY.edges:
                    return False
                if (t2, t1) in REGISTRY.edges:
                    return True
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent

        return _features(cls) >= _features(typ)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls > t for t in typ)

        t1 = cls
        t2 = typ.parent if typ.is_flyweight else typ  # TODO: might have problems if t2 is a decorator
        while not t2.is_root:
            while not t1.is_root:
                if (t1, t2) in REGISTRY.edges:
                    return False
                if (t2, t1) in REGISTRY.edges:
                    return True
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent

        return _features(cls) > _features(typ)

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


class AbstractBuilder(TypeBuilder):
    """A builder-style parser for an abstract type's namespace (backend == None).

    Abstract types support the creation of required fields through the ``EMPTY``
    syntax, which must be inherited or defined by each of their concrete
    implementations.  This rule is enforced by ConcreteBuilder, which is automatically
    chosen when the backend is not None.  Note that in contrast to concrete types,
    abstract types do not support parametrization, and attempting defining a
    corresponding constructor will throw an error.
    """

    RESERVED = TypeBuilder.RESERVED | {
        "__class_getitem__",
        "from_scalar",
        "from_dtype"
        "from_string"
    }

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        cache_size: int | None
    ):
        if len(bases) != 1 or not (bases[0] is object or issubclass(bases[0], Type)):
            raise TypeError(
                f"abstract types must inherit from bertrand.Type or one of its "
                f"subclasses, not {bases}"
            )

        parent = bases[0]
        if parent is not object and not parent.is_abstract:
            raise TypeError("abstract types must not inherit from concrete types")

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
            elif name in self.RESERVED and self.parent is not object:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
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
        self.namespace["_default"] = []
        self.namespace["_nullable"] = []

        self.fields["backend"] = ""

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Register a newly-created type, pushing changes to the parent type's
        namespace if necessary.

        Parameters
        ----------
        typ : TypeMeta | DecoratorMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        TypeMeta | DecoratorMeta
            The registered type.
        """
        # pylint: disable=protected-access
        parent = self.parent
        if parent is not object:
            super().register(typ)
            parent._subtypes.add(typ)
            parent._children.add(typ)
            while not parent.is_root:
                parent = parent.parent
                parent._children.add(typ)

        typ._children.add(typ)
        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        if "__class_getitem__" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement __class_getitem__()")

        def default(cls: TypeMeta, backend: str, *args: Any) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            # pylint: disable=protected-access
            if backend in cls._implementations:
                if args:
                    return cls._implementations[backend][*args]
                return cls._implementations[backend]

            if cls._default:
                default = cls.as_default
                if default.is_abstract:
                    return default.__class_getitem__(backend, *args)

            raise TypeError(f"invalid backend: {repr(backend)}")

        self.namespace["__class_getitem__"] = classmethod(default)  # type: ignore
        self.namespace["_params"] = ("backend",)

    def _from_scalar(self) -> None:
        """Parse a type's from_scalar() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_scalar" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_scalar()")

        # TODO: alternatively, we could forward to the default implementation

        def default(cls: TypeMeta, scalar: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError("abstract types cannot be constructed using from_scalar()")

        self.namespace["_defines_from_scalar"] = False
        self.namespace["from_scalar"] = classmethod(default)  # type: ignore

    def _from_dtype(self) -> None:
        """Parse a type's from_dtype() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_dtype" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_dtype()")

        def default(cls: TypeMeta, dtype: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError("abstract types cannot be constructed using from_dtype()")

        self.namespace["_defines_from_scalar"] = False
        self.namespace["from_dtype"] = classmethod(default)  # type: ignore

    def _from_string(self) -> None:
        """Parse a type's from_string() method (if it has one), ensuring that it is
        callable with the same number of positional arguments as __class_getitem__().
        """
        if "from_string" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_string()")

        def default(cls: TypeMeta, backend: str = "", *args: str) -> TypeMeta:
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

            raise TypeError(f"invalid backend: {repr(backend)}")

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

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str,
        cache_size: int | None,
    ):
        if len(bases) != 1 or not (bases[0] is object or issubclass(bases[0], Type)):
            raise TypeError(
                f"concrete types must only inherit from bertrand.Type or one of its "
                f"subclasses, not {bases}"
            )

        if not isinstance(backend, str):
            raise TypeError(f"backend id must be a string, not {repr(backend)}")

        parent = bases[0]
        if parent is not object:
            if not parent.is_abstract:
                raise TypeError(
                    "concrete types must not inherit from other concrete types"
                )
            if backend in parent._implementations:
                raise TypeError(f"backend id must be unique: {repr(backend)}")

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
                    f"concrete types must not have required fields: {self.name}.{name}"
                )
            elif name in self.RESERVED:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
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
        self.namespace["_default"] = []
        self.namespace["_nullable"] = []

        self.fields["backend"] = self.backend

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Register a newly-created type, pushing changes to the parent type's
        namespace if necessary.

        Parameters
        ----------
        typ : TypeMeta | DecoratorMeta
            The final type to register.  This is typically the result of a fresh
            call to type.__new__().

        Returns
        -------
        TypeMeta | DecoratorMeta
            The registered type.
        """
        # pylint: disable=protected-access
        parent = self.parent
        if self.parent is not object:
            super().register(typ)
            parent._implementations[self.backend] = typ
            parent._children.add(typ)
            while not parent.is_root:
                parent = parent.parent
                parent._children.add(typ)

        typ._children.add(typ)
        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
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
                        "__class_getitem__() must not accept any keyword arguments"
                    )
                if param.default is param.empty:
                    raise TypeError(
                        "__class_getitem__() arguments must have default values"
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
                    raise TypeError(f"{cls.__name__} does not accept any parameters")
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
        if "from_scalar" in self.namespace:
            method = self.namespace["from_scalar"]
            if not isinstance(method, classmethod):
                raise TypeError("from_scalar() must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_scalar() must accept a single positional argument (in "
                    "addition to cls)"
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
        if "from_dtype" in self.namespace:
            method = self.namespace["from_dtype"]
            if not isinstance(method, classmethod):
                raise TypeError("from_dtype() must be a classmethod")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_dtype() must accept a single positional argument in "
                    "addition to cls"
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
        if "from_string" in self.namespace:
            method = self.namespace["from_string"]
            if not isinstance(method, classmethod):
                raise TypeError("from_string() must be a classmethod")

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
                f"the signature of from_string() must match __class_getitem__(): "
                f"{str(sig)} != {str(self.class_getitem_signature)}"
            )


def _format_dict(d: dict[str, Any]) -> str:
    """Convert a dictionary into a string with standard indentation."""
    if not d:
        return "{}"

    prefix = "{\n    "
    sep = ",\n    "
    suffix = "\n}"
    return prefix + sep.join(f"{k}: {repr(v)}" for k, v in d.items()) + suffix


def _features(t: TypeMeta | DecoratorMeta) -> tuple[int | float, int, bool, int]:
    """Extract a tuple of features representing the allowable range of a type, used for
    range-based sorting and comparison.
    """
    return (
        t.max - t.min,  # total range
        t.itemsize,  # memory footprint
        t.is_nullable,  # nullability
        abs(t.max + t.min),  # bias away from zero
    )


###############################
####    DECORATOR TYPES    ####
###############################


# TODO: focus on making decorators as minimally invasive as possible.
# -> is_flyweight should always refer to underlying type?  Add a second flag to
# check if the decorator itself is parametrized?
# -> this might be reduced to .wrapped is not None


# TODO: DecoratorMeta.is_decorated replaces is_flyweight checks?


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

    # NOTE: this metaclass uses the following internal fields, which are populated
    # by DecoratorBuilder:
    _slug: str
    _hash: int
    _required: dict[str, Callable[..., Any]]
    _fields: dict[str, Any]
    _parent: DecoratorMeta  # NOTE: parent of this decorator, not wrapped
    _params: tuple[str]
    _parametrized: bool
    _cache_size: int | None
    _flyweights: LinkedDict[str, DecoratorMeta]
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
        if not (len(bases) == 0 or bases[0] is object):
            print("-" * 80)
            print(f"slug: {cls.slug}")
            print(f"aliases: {cls.aliases}")
            print(f"fields: {_format_dict(cls._fields)}")
            print(f"cache_size: {cls.cache_size}")
            print()

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[type, ...],
        namespace: dict[str, Any],
        cache_size: int | None = None,
    ) -> DecoratorMeta:
        build = DecoratorBuilder(name, bases, namespace, cache_size)

        return build.parse().fill().register(
            super().__new__(mcs, build.name, (build.parent,), build.namespace)
        )

    def flyweight(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """Return a cached instance of the decorator or create a new one if it does
        not exist.  Positional arguments are used to identify the decorator, and
        keyword arguments are piped into its resulting namespace.
        """
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls._flyweights.lru_get(slug, None)

        if typ is None:
            if len(args) != len(cls._params):
                raise TypeError(
                    f"expected {len(cls._params)} positional arguments, got "
                    f"{len(args)}"
                )

            typ = super().__new__(
                DecoratorMeta,
                cls.__name__,
                (cls,),
                {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,
                    "_parent": cls,
                    "_parametrized": True,
                } | kwargs
            )
            cls._flyweights.lru_add(slug, typ)

        return typ

    ################################
    ####    CLASS PROPERTIES    ####
    ################################

    @property
    def aliases(cls) -> Aliases:
        """TODO"""
        return cls._aliases

    @property
    def slug(cls) -> str:
        """TODO"""
        return cls._slug

    @property
    def hash(cls) -> int:
        """TODO"""
        return cls._hash

    @property
    def cache_size(cls) -> int:
        """TODO"""
        return cls._cache_size

    @property
    def flyweights(cls) -> Mapping[str, DecoratorMeta]:
        """TODO"""
        return MappingProxyType(cls._flyweights)

    @property
    def is_flyweight(cls) -> bool:
        """TODO"""
        return cls._parametrized

    @property
    def params(cls) -> Mapping[str, Any]:
        """TODO"""
        return MappingProxyType({k: cls._fields[k] for k in cls._params})

    @property
    def as_default(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.replace(cls.wrapped.as_default)  # NOTE: will recurse

    @property
    def as_nullable(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.replace(cls.wrapped.as_nullable)

    @property
    def root(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.replace(cls.wrapped.root)

    @property
    def parent(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        parent = cls.wrapped.parent
        if parent is None:
            return None
        return cls.replace(parent)

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.subtypes:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.implementations:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.children:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.leaves:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.larger:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.smaller:
                result.add(cls.replace(typ))
        return Union.from_types(result)

    @property
    def decorators(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        curr = cls
        while curr.wrapped is not None:
            result.add(curr)
            curr = curr.wrapped

        return Union.from_types(result)

    @property
    def wrapper(cls) -> DecoratorMeta | None:
        """Return the outermost decorator as a non-parametrized type."""
        return cls._parent if cls.is_flyweight else cls

    @property
    def wrapped(cls) -> TypeMeta | DecoratorMeta | None:
        """TODO"""
        return cls._fields["wrapped"]

    @property
    def unwrapped(cls) -> TypeMeta | None:
        """TODO"""
        result = cls
        while isinstance(result, DecoratorMeta):
            result = result.wrapped
        return result

    def __getattr__(cls, name: str) -> Any:
        if name in cls._fields:
            return cls._fields[name]

        wrapped = cls._fields["wrapped"]
        if wrapped:
            return getattr(wrapped, name)

        raise AttributeError(
            f"type object '{cls.__name__}' has no attribute '{name}'"
        )

    def __dir__(cls) -> set[str]:
        result = set(super().__dir__())
        result.update({
            "slug", "hash", "decorators", "wrapped", "unwrapped", "cache_size",
            "flyweights", "params", "as_default", "as_nullable", "root", "parent",
            "subtypes", "implementations", "children", "leaves", "larger", "smaller"
        })
        result.update(cls._fields)
        curr = cls.wrapped
        while curr:
            result.update(dir(curr))
            curr = curr.wrapped
        return result

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    # TODO: comparisons, isinstance(), issubclass(), etc. should delegate to wrapped
    # type.  They should also be able to compare against other decorators, and should
    # take decorator order into account.

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        return cls.transform(cls.wrapped(*args, **kwargs))

    # TODO:
    # >>> Sparse[Int16, 1] in Sparse[Int]
    # False  (should be True?)

    # TODO: probably need some kind of supplemental is_empty field that checks whether
    # parameters were supplied beyond wrapped.

    def __subclasscheck__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        # naked types cannot be subclasses of decorators
        if isinstance(other, TypeMeta):
            return False

        wrapper = cls.wrapper
        wrapped = cls.wrapped

        if isinstance(other, DecoratorMeta):
            if cls is DecoratorType:
                return True

            # top-level decorator types do not match
            if wrapper is not other.wrapper:
                return False

            # this decorator is not parametrized - treat as wildcard
            if wrapped is None:  # TODO: check for is_empty?  Or rework is_flyweight to refer to params beyond wrapped?
                return True

            # other decorator is not parametrized - invert wildcard
            forwarded = other.wrapped
            if forwarded is None:
                return False

            # check parameters are compatible
            params = list(cls.params.values())[1:]
            forwarded_params = list(other.params.values())[1:]
            for p1, p2 in zip(params, forwarded_params):
                na1 = pd.isna(p1)
                na2 = pd.isna(p2)
                if na1 ^ na2 or not (na1 and na2 or p1 is p2 or p1 == p2):
                    return False

            # delegate to wrapped type
            return wrapped.__subclasscheck__(forwarded)

        # recur for unions
        if isinstance(other, UnionMeta):
            return all(cls.__subclasscheck__(o) for o in other)

        # fall back to traditional issubclass() check (always returns false or error)
        return super().__subclasscheck__(other)

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        return cls.itemsize

    def __bool__(cls) -> bool:
        return True  # without this, Python defaults to len() > 0, which is wrong

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls < t for t in typ)

        # TODO: check this for correctness.  If we swap to making wrapped return a
        # self reference instead of None, this could be slightly simpler.

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(typ, DecoratorMeta):
            t2 = typ.wrapped
            if t2 is None:
                return False
            return t1 < t2

        return t1 < typ

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        if cls is other:
            return True

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(other, DecoratorMeta):
            t2 = other.wrapped
            if t2 is None:
                return False
            return t1 <= t2

        return t1 <= other

    def __eq__(cls, other: Any) -> bool:
        if cls is other:
            return True

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(other, DecoratorMeta):
            t2 = other.wrapped
            if t2 is None:
                return False
            return t1 == t2

        return t1 == other

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        if cls is other:
            return True

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(other, DecoratorMeta):
            t2 = other.wrapped
            if t2 is None:
                return False
            return t1 >= t2

        return t1 >= other

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        if cls is other:
            return False

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(other, DecoratorMeta):
            t2 = other.wrapped
            if t2 is None:
                return False
            return t1 > t2

        return t1 > other

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


class DecoratorBuilder(TypeBuilder):
    """A builder-style parser for a decorator type's namespace (any subclass of
    DecoratorType).

    Decorator types are similar to concrete types except that they are implicitly
    parametrized to wrap around another type and modify its behavior.  They can accept
    additional parameters to configure the decorator, but must take at least one
    positional argument that specifies the type to be wrapped.
    """

    def __init__(
        self,
        name: str,
        bases: tuple[DecoratorMeta],
        namespace: dict[str, Any],
        cache_size: int | None,
    ):
        if len(bases) != 1 or not (bases[0] is object or bases[0] is DecoratorType):
            raise TypeError(
                f"decorators must only inherit from bertrand.DecoratorType, not "
                f"{bases}"
            )

        super().__init__(name, bases[0], namespace, cache_size)
        self.class_getitem_signature: inspect.Signature | None = None

    def parse(self) -> DecoratorBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        for name, value in self.namespace.copy().items():
            if isinstance(value, Empty):
                raise TypeError(
                    f"decorator types must not have required fields: {self.name}.{name}"
                )
            elif name in self.RESERVED:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )
            elif name in self.required:
                self._validate(name, self.namespace.pop(name))

        # validate any remaining required fields that were not explicitly assigned
        for name in list(self.required):
            self._validate(name, EMPTY)

        return self

    def fill(self) -> DecoratorBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.
        """
        super().fill()

        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()
        self._transform()
        self._inverse_transform()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Instantiate the type and register it in the global type registry."""
        # pylint: disable=protected-access
        if self.parent is not object:
            super().register(typ)
            REGISTRY.decorators._set.add(typ)

        return typ

    def _class_getitem(self) -> None:
        """Parse a decorator's __class_getitem__ method (if it has one) and generate a
        wrapper that can be used to instantiate the type.
        """
        parameters = {}

        if "__class_getitem__" in self.namespace:
            invoke = self.namespace["__class_getitem__"]

            def wrapper(
                cls: DecoratorMeta,
                wrapped: META,
                *args: Any
            ) -> DecoratorMeta | UnionMeta:
                """Unwrap tuples/unions and forward arguments to the wrapped method."""
                if isinstance(wrapped, (TypeMeta, DecoratorMeta)):
                    return _insort_decorator(invoke, cls, wrapped, *args)

                if isinstance(wrapped, UnionMeta):
                    return Union.from_types(LinkedSet(
                        _insort_decorator(invoke, cls, t, *args) for t in wrapped
                    ))

                raise TypeError(
                    f"Decorators must accept another bertrand type as a first "
                    f"argument, not {repr(wrapped)}"
                )

            self.namespace["__class_getitem__"] = classmethod(wrapper)  # type: ignore

            skip = True
            sig = inspect.signature(invoke)
            if len(sig.parameters) < 2:
                raise TypeError(
                    "__class_getitem__() must accept at least one positional argument "
                    "describing the type to decorate (in addition to cls)"
                )

            for par_name, param in sig.parameters.items():
                if skip:
                    skip = False
                    continue
                if param.kind == param.KEYWORD_ONLY or param.kind == param.VAR_KEYWORD:
                    raise TypeError(
                        "__class_getitem__() must not accept any keyword arguments"
                    )
                if param.default is param.empty:
                    raise TypeError(
                        "__class_getitem__() arguments must have default values"
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

                if isinstance(wrapped, UnionMeta):
                    return Union.from_types(
                        LinkedSet(_insort_decorator(invoke, cls, t) for t in wrapped)
                    )

                raise TypeError(
                    f"Decorators must accept another bertrand type as a first "
                    f"argument, not {repr(wrapped)}"
                )

            parameters["wrapped"] = None
            self.fields["wrapped"] = None
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
        if "from_scalar" in self.namespace:
            method = self.namespace["from_scalar"]
            if not isinstance(method, classmethod):
                raise TypeError("from_scalar() must be a class method")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_scalar() must accept a single positional argument (in "
                    "addition to cls)"
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
        if "from_dtype" in self.namespace:
            method = self.namespace["from_dtype"]
            if not isinstance(method, classmethod):
                raise TypeError("from_dtype() must be a class method")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_dtype() must accept a single positional argument in "
                    "addition to cls"
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
        if "from_string" in self.namespace:
            method = self.namespace["from_string"]
            if not isinstance(method, classmethod):
                raise TypeError("from_string() must be a class method")

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
                f"the signature of from_string() must match __class_getitem__() "
                f"exactly: {str(sig)} != {str(self.class_getitem_signature)}"
            )

    def _transform(self) -> None:
        """Parse a decorator's transform() method (if it has one) and ensure that it
        is callable with a single positional argument.
        """
        if "transform" in self.namespace:
            method = self.namespace["transform"]
            if not isinstance(method, classmethod):
                raise TypeError("transform() must be a class method")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "transform() must accept a single positional argument (in "
                    "addition to cls)"
                )

    def _inverse_transform(self) -> None:
        """Parse a decorator's inverse_transform() method (if it has one) and ensure
        that it is callable with a single positional argument.
        """
        if "inverse_transform" in self.namespace:
            method = self.namespace["inverse_transform"]
            if not isinstance(method, classmethod):
                raise TypeError("inverse_transform() must be a class method")

            sig = inspect.signature(method.__wrapped__)
            params = list(sig.parameters.values())
            if len(params) != 2 or not (
                params[1].kind == params[1].POSITIONAL_ONLY or
                params[1].kind == params[1].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "inverse_transform() must accept a single positional argument (in "
                    "addition to cls)"
                )


def _insort_decorator(
    invoke: Callable[..., DecoratorMeta],
    cls: DecoratorMeta,
    other: TypeMeta | DecoratorMeta,
    *args: Any,
) -> DecoratorMeta:
    """Insert a decorator into a type's decorator stack, ensuring that it obeys the
    registry's current precedence.
    """
    # TODO: use replace() rather than reimplementing it here
    visited = []
    curr: DecoratorMeta | TypeMeta = other
    wrapped = curr.wrapped
    while wrapped:
        wrapper = curr.wrapper
        delta = REGISTRY.decorators.distance(cls, wrapper)

        if delta > 0:  # cls is higher priority or identical to wrapper
            break
        elif delta == 0:  # cls is identical to wrapper
            curr = wrapped
            break

        visited.append((wrapper, list(curr.params.values())[1:]))
        curr = wrapped
        wrapped = curr.wrapped

    result = invoke(cls, curr, *args)
    for wrapper, params in reversed(visited):
        if not params:
            result = wrapper[result]
        else:
            result = wrapper[result, *params]
    return result


###########################
####    UNION TYPES    ####
###########################


# TODO: segmentation fault
# >>> Union[Int8["numpy", "i4"]].replace("i2")
# Union[NumpyInt8['i2']]
# >>> Union[Int8["numpy", "i4"] | Int8["numpy", "?"]].replace("i2")
# Traceback (most recent call last):
#   File "<stdin>", line 1, in <module>
#   File "/home/eerkela/data/pdcast/pdcast/structs/test.py", line 2207, in __repr__
#     return f"Union[{', '.join(t.slug for t in cls._types)}]"
#                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
# TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# >>> Union[Int8["numpy", "i4"], Int8["numpy", "?"]].replace("i2")
# Union[NumpyInt8['i2']]
# >>> Union[Int8["numpy", "i4"] | Int8["numpy", "?"]]
# Traceback (most recent call last):
#   File "<stdin>", line 1, in <module>
#   File "/home/eerkela/data/pdcast/pdcast/structs/test.py", line 2207, in __repr__
#     return f"Union[{', '.join(t.slug for t in cls._types)}]"
#                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
# TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# >>> Union[Int8["numpy", "i4"] | Int8["numpy", "?"]]
# Traceback (most recent call last):
#   File "<stdin>", line 1, in <module>
#   File "/home/eerkela/data/pdcast/pdcast/structs/test.py", line 2207, in __repr__
#     return f"Union[{', '.join(t.slug for t in cls._types)}]"
#                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
# TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# >>> import pdb; pdb.pm()
# > /home/eerkela/data/pdcast/pdcast/structs/test.py(2207)__repr__()
# -> return f"Union[{', '.join(t.slug for t in cls._types)}]"
# (Pdb) cls
# *** TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# (Pdb) cls._types
# LinkedSet({Union[NumpyInt8['i4'], NumpyInt8['?']]})
# (Pdb) cls
# *** TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# (Pdb) q
# >>> Int32 | Int64
# Union[Int32, Int64]
# >>> Union[Int32 | Int64]
# Traceback (most recent call last):
#   File "<stdin>", line 1, in <module>
#   File "/home/eerkela/data/pdcast/pdcast/structs/test.py", line 2207, in __repr__
# TypeError: sequence item 0: expected str instance, bertrand.LinkedDict found
# >>> quit()
# Segmentation fault


# TODO: need a bunch of special cases to handle empty unions


# TODO: separate structured unions into StructuredMeta, which inherits from UnionMeta
# and StructuredUnion, which is returned by Union.__getitem__().  This would avoid
# having to check for is_structured() everywhere.


class UnionMeta(BaseMeta):
    """Metaclass for all composite bertrand types (those produced by Union[] or setlike
    operators).

    This metaclass is responsible for delegating operations to the union's members, and
    is an example of the Gang of Four's `Composite Pattern
    <https://en.wikipedia.org/wiki/Composite_pattern>`.  The union can thus be treated
    similarly to an individual type, with operations producing new unions as output.

    See the documentation for the `Union` class for more information on how these work.
    """

    _columns: dict[str, META] = {}
    _types: LinkedSet[TypeMeta | DecoratorMeta] = LinkedSet()
    _hash: int = 42
    _index: RLE_ARRAY | None = None

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __getitem__(cls, types: TYPESPEC | tuple[TYPESPEC, ...]) -> UnionMeta:
        typ = resolve(types)
        if isinstance(typ, UnionMeta):
            return typ
        return cls.from_types(LinkedSet((typ,)))

    def from_types(
        cls,
        types: LinkedSet[TypeMeta | DecoratorMeta],
        index: RLE_ARRAY | None = None
    ) -> UnionMeta:
        """TODO"""
        hash_val = 42  # to avoid potential collisions at hash=0
        for t in types:
            if not isinstance(t, (TypeMeta, DecoratorMeta)):
                raise TypeError("union members must be bertrand types")
            hash_val = (hash_val * 31 + hash(t)) % (2**63 - 1)

        return super().__new__(
            UnionMeta,
            cls.__name__,
            (cls,),
            {
                "_types": types,
                "_hash": hash_val,
                "_index": index,
                "__class_getitem__": union_getitem
            }
        )

    def from_columns(cls, columns: dict[str, META]) -> UnionMeta:
        """TODO"""
        hash_val = 42
        flattened = LinkedSet()
        for k, v in columns.items():
            if not isinstance(k, str):
                raise TypeError("column names must be strings")
            if isinstance(v, UnionMeta):
                flattened.update(v)
            elif isinstance(v, (TypeMeta, DecoratorMeta)):
                flattened.add(v)
            else:
                raise TypeError("column values must be bertrand types")
            hash_val = (hash_val * 31 + hash(k) + hash(v)) % (2**63 - 1)

        return super().__new__(UnionMeta, cls.__name__, (cls,), {
            "_columns": columns,
            "_types": flattened,
            "_hash": hash_val,
            "_index": None,
            "__class_getitem__": union_getitem
        })

    ################################
    ####    UNION ATTRIBUTES    ####
    ################################

    @property
    def is_structured(cls) -> bool:
        """TODO"""
        return bool(cls._columns)

    @property
    def index(cls) -> OBJECT_ARRAY | None:
        """A 1D numpy array containing the observed type at every index of an iterable
        passed to `bertrand.detect()`.  This is stored internally as a run-length
        encoded array of flyweights for memory efficiency, and is expanded into a
        full array when accessed.
        """
        if cls.is_structured:
            indices = {
                col: _rle_decode(typ._index) for col, typ in cls.items()  # type: ignore
                if isinstance(typ, UnionMeta) and typ._index is not None
            }
            if not indices:
                return None

            dtype = np.dtype([(k, object) for k in cls._columns])
            shape = next(iter(indices.values())).shape
            arr = np.empty(shape, dtype=dtype)
            for col, typ in cls.items():
                if col in indices:
                    arr[col] = indices[col]
                else:
                    arr[col] = np.full(shape, typ, dtype=object)

            return arr

        if cls._index is None:
            return None  # type: ignore
        return _rle_decode(cls._index)  # type: ignore

    def collapse(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({
                k: v.collapse() if isinstance(v, UnionMeta) else v
                for k, v in cls.items()
            })

        result = LinkedSet()
        for t in cls:
            if not any(t is not u and issubclass(t, u) for u in cls):
                result.add(t)
        return cls.from_types(result)

    def issubset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        """TODO"""
        typ = resolve(other)
        if cls is typ:
            return not strict

        if isinstance(typ, UnionMeta):
            if cls.is_structured and typ.is_structured:
                raise NotImplementedError("check each column independently")

            result = all(issubclass(t, typ) for t in cls)
        else:
            result = issubclass(cls, typ)

        if strict:
            return result and len(cls.children) < len(typ.children)
        return result

    def issuperset(
        cls,
        other: TYPESPEC | Iterable[TYPESPEC],
        strict: bool = False
    ) -> bool:
        """TODO"""
        typ = resolve(other)
        if cls is typ:
            return not strict

        if isinstance(typ, UnionMeta):
            if cls.is_structured and typ.is_structured:
                raise NotImplementedError("check each column independently")

            result = all(issubclass(typ, t) for t in cls)
        else:
            result = issubclass(typ, cls)

        if strict:
            return result and len(cls.children) > len(typ.children)
        return result

    def isdisjoint(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        """TODO"""
        typ = resolve(other)
        if cls is typ:
            return False

        if cls.is_structured and typ.is_structured:
            raise NotImplementedError("check each column independently")

        return not any(issubclass(t, typ) or issubclass(typ, t) for t in cls)

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            raise NotImplementedError("sort each column independently")

        result = cls._types.copy()
        result.sort(key=key, reverse=reverse)  # type: ignore
        return cls.from_types(result)

    def keys(cls) -> KeysView[str]:
        """TODO"""
        return cls._columns.keys()  # type: ignore

    def values(cls) -> ValuesView[META]:
        """TODO"""
        return cls._columns.values()

    def items(cls) -> ItemsView[str, META]:
        """TODO"""
        return cls._columns.items()  # type: ignore

    def replace(cls, *args: Any, **kwargs: Any) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            raise NotImplementedError("not even sure what to do here")

        return Union.from_types(
            LinkedSet(t.replace(*args, **kwargs) for t in cls)
        )

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def as_default(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.as_default for k, v in cls.items()})
        return cls.from_types(LinkedSet(t.as_default for t in cls))

    @property
    def as_nullable(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.as_nullable for k, v in cls.items()})
        return cls.from_types(LinkedSet(t.as_nullable for t in cls))

    @property
    def root(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.root for k, v in cls.items()})
        return cls.from_types(LinkedSet(t.root for t in cls))

    @property
    def parent(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({
                k: v.parent if v.parent else v for k, v in cls.items()
            })

        result = LinkedSet()
        for t in cls:
            result.add(t.parent if t.parent else t)
        return cls.from_types(result)

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.subtypes for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.subtypes)
        return cls.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.implementations for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.implementations)
        return cls.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.children for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.children)
        return cls.from_types(result)

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.leaves for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.leaves)
        return cls.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.larger for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.larger)
        return cls.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.smaller for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.smaller)
        return cls.from_types(result)

    @property
    def decorators(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.decorators for k, v in cls.items()})

        result = LinkedSet()
        for t in cls:
            result.update(t.decorators)
        return cls.from_types(result)

    @property
    def wrapper(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({
                k: v.wrapper if v.wrapper else v for k, v in cls.items()
            })
        return cls.from_types(LinkedSet(t.wrapper for t in cls))

    @property
    def wrapped(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({
                k: v.wrapped if v.wrapped else v for k, v in cls.items()
            })
        return cls.from_types(LinkedSet(t.wrapped for t in cls))

    @property
    def unwrapped(cls) -> UnionMeta:
        """TODO"""
        if cls.is_structured:
            return cls.from_columns({k: v.unwrapped for k, v in cls.items()})
        return cls.from_types(LinkedSet(t.unwrapped for t in cls))

    def __getattr__(cls, name: str) -> dict[TypeMeta | str, Any]:
        if cls.is_structured:
            return {k: getattr(v, name) for k, v in cls.items()}
        return {t: getattr(t, name) for t in super().__getattribute__("_types")}

    def __dir__(cls) -> set[str]:
        result = set(super().__dir__())
        result.update({
            "is_structured", "index", "collapse", "issubset", "issuperset",
            "isdisjoint", "sorted", "keys", "values", "items",
        })
        for t in cls:
            result.update(dir(t))
        return result

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any] | pd.DataFrame:
        """TODO"""
        if cls.is_structured:
            if len(args) != len(cls._columns):
                raise TypeError(
                    f"structured union expects {len(cls._columns)} positional "
                    f"arguments, not {len(args)}"
                )
            return pd.DataFrame({
                k: v(args[i], **kwargs) for i, (k, v) in enumerate(cls.items())
            })

        for t in cls:
            try:
                return t(*args, **kwargs)
            except Exception:
                continue

        raise TypeError(f"cannot convert to union type: {repr(cls)}")

    def __instancecheck__(cls, other: Any) -> bool:
        if cls.is_structured:
            if isinstance(other, pd.DataFrame):
                return (
                    len(cls._columns) == len(other.columns) and
                    all(x == y for x, y in zip(cls._columns, other.columns)) and
                    all(isinstance(other[x], y) for x, y in cls.items())
                )

            if isinstance(other, np.ndarray) and other.dtype.fields:
                return (
                    len(cls._columns) == len(other.dtype.names) and
                    all(x == y for x, y in zip(cls._columns, other.dtype.names)) and
                    all(isinstance(other[x], y) for x, y in cls.items())
                )

            return False

        return any(isinstance(other, t) for t in cls)

    def __subclasscheck__(cls, other: TYPESPEC) -> bool:
        typ = resolve(other)

        if isinstance(typ, UnionMeta):
            if cls.is_structured and typ.is_structured:
                return (
                    all(k in cls._columns for k in typ._columns) and
                    all(issubclass(v, cls._columns[k]) for k, v in typ.items())  # type: ignore
                )
            return all(any(issubclass(o, t) for t in cls) for o in typ)

        return any(issubclass(typ, t) for t in cls)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        return len(cls._types)

    def __iter__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        return iter(cls._types)

    def __reversed__(cls) -> Iterator[TypeMeta | DecoratorMeta]:
        return reversed(cls._types)

    def __or__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if cls.is_structured:
            columns = cls._columns.copy()
            if isinstance(typ, UnionMeta) and typ.is_structured:
                for k, v in typ._columns.items():
                    if k in columns:
                        columns[k] |= v
                    else:
                        columns[k] = v
            else:
                for k, v in columns.items():
                    columns[k] |= typ
            return cls.from_columns(columns)  # type: ignore

        if isinstance(typ, UnionMeta):
            return cls.from_types(cls._types | typ)
        return cls.from_types(cls._types | (typ,))  # type: ignore

    def __and__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if cls.is_structured:
            if isinstance(typ, UnionMeta) and typ.is_structured:
                columns = {}
                for k, v in typ.items():
                    if k in cls._columns:
                        columns[k] = cls._columns[k] & v
            else:
                columns = {k: v & typ for k, v in columns.items()}
            return cls.from_columns(columns)

        result = LinkedSet()
        if isinstance(typ, UnionMeta):
            for t in cls._types | cls.children:
                if any(issubclass(t, u) for u in typ):
                    result.add(t)
        else:
            for t in cls._types | cls.children:
                if issubclass(t, typ):
                    result.add(t)

        return cls.from_types(result).collapse()

    def __sub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if cls.is_structured:
            if isinstance(typ, UnionMeta) and typ.is_structured:
                columns = cls._columns.copy()
                for k, v in typ.items():
                    if k in columns:
                        columns[k] -= v
            else:
                columns = {k: v - typ for k, v in cls.items()}
            return cls.from_columns(columns)  # type: ignore

        result = LinkedSet()
        if isinstance(typ, UnionMeta):
            for t in cls._types | cls.children:
                if not any(issubclass(t, u) or issubclass(u, t) for u in typ):
                    result.add(t)

        else:
            for t in cls._types | cls.children:
                if not (issubclass(t, typ) or issubclass(typ, t)):
                    result.add(t)

        return cls.from_types(result).collapse()

    def __xor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if cls.is_structured:
            if isinstance(typ, UnionMeta) and typ.is_structured:
                columns = cls._columns.copy()
                for k, v in typ.items():
                    if k in columns:
                        columns[k] ^= v
                    else:
                        columns[k] = v
            else:
                columns = {k: v ^ typ for k, v in cls.items()}
            return cls.from_columns(columns)  # type: ignore

        result = LinkedSet()
        if isinstance(typ, UnionMeta):
            for t in cls._types | cls.children:
                if not any(issubclass(t, u) or issubclass(u, t) for u in typ):
                    result.add(t)
            for t in typ._types | typ.children:
                if not any(issubclass(t, u) or issubclass(u, t) for u in cls):
                    result.add(t)

        else:
            for t in cls._types | cls.children:
                if not (issubclass(t, typ) or issubclass(typ, t)):
                    result.add(t)
            for t in typ | typ.children:
                if not (issubclass(t, cls) or issubclass(cls, t)):
                    result.add(t)

        return cls.from_types(result).collapse()

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return all(t < typ for t in cls)

        if cls.is_structured and typ.is_structured:
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v < t:
                    return False
            return True

        return all(all(t < u for u in typ) for t in cls)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return all(t <= typ for t in cls)

        if cls.is_structured and typ.is_structured:
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v <= t:
                    return False
            return True

        return all(all(t <= u for u in typ) for t in cls)

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return all(t is typ for t in cls)

        if cls.is_structured and typ.is_structured:
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if v is not t:
                    return False

            return True

        elif cls.is_structured or typ.is_structured:
            return False

        return len(cls) == len(typ) and all(t is u for t, u in zip(cls, other))

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return all(t >= typ for t in cls)

        if cls.is_structured and typ.is_structured:
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v >= t:
                    return False
            return True

        return all(all(t >= u for u in typ) for t in cls)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return all(t > typ for t in cls)

        if cls.is_structured and typ.is_structured:
            for k, v in cls.items():
                t = typ._columns.get(k, None)
                if t is None:
                    raise TypeError(f"no match for column '{k}'")
                if not v > t:
                    return False
            return True

        return all(all(t > u for u in typ) for t in cls)

    def __str__(cls) -> str:
        if cls.is_structured:
            items = [
                f"'{k}': {str(v) if v else '{}'}"
                for k, v in cls._columns.items()
            ]
            return f"{{{', '.join(items)}}}"

        return f"{' | '.join(t.slug for t in cls._types) or '{}'}"

    def __repr__(cls) -> str:
        if cls.is_structured:
            items = [
                f"'{k}': {str(v) if v else 'Union[]'}"
                for k, v in cls._columns.items()
            ]
            return f"Union[{', '.join(items)}]"

        return f"Union[{', '.join(t.slug for t in cls._types)}]"


class Union(metaclass=UnionMeta):
    """TODO
    """

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand unions cannot be instantiated")


def union_getitem(cls: UnionMeta, key: int | slice | str) -> META:
    """A parametrized replacement for the base Union's __class_getitem__() method that
    allows for integer-based indexing/slicing of a union's types, as well as
    string-based indexing of structured unions.
    """
    if cls.is_structured and isinstance(key, str):
        return cls._columns[key]

    if isinstance(key, slice):
        return cls.from_types(cls._types[key])  # type: ignore

    return cls._types[key]  # type: ignore


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


class Type(object, metaclass=TypeMeta):
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
    max:            int | float     = EMPTY(_check_max)  # type: ignore
    min:            int | float     = EMPTY(_check_min)  # type: ignore
    is_nullable:    bool            = EMPTY(_check_is_nullable)  # type: ignore
    missing:        Any             = EMPTY(_check_missing)

    # NOTE: Type's metaclass overloads the __call__() operator to produce a series of
    # this type.  As such, it is not possible to instantiate a type directly, and
    # implementing either `__init__` or `__new__` will cause a metaclass error.

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand types cannot have instances")

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
    # invoke the parent method and modify the input/output, rather than reimplementing
    # the method from scratch.

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        """Base implementation of the replace() method, which produces a new type with
        a modified set of parameters.

        This is a convenience method that allows users to modify a type without
        mutating it.  It is equivalent to re-parametrizing the type with the provided
        arguments, after merging with their existing values.
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

        return cls.wrapper[*forwarded.values()]  # type: ignore


class DecoratorType(object, metaclass=DecoratorMeta):
    """TODO
    """

    # TODO: __class_getitem__(), from_scalar(), from_dtype(), from_string()

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Apply the decorator to a series of the wrapped type."""
        return series.astype(cls.dtype, copy=False)

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Remove the decorator from a series of the wrapped type."""
        return series.astype(cls.wrapped.dtype, copy=False)

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """Base implementation of the replace() method, which produces a new type with
        a modified set of parameters.
        """
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

        return cls.wrapper[*forwarded.values()]  # type: ignore






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
            while patch._default:
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

    def __class_getitem__(
        cls,
        wrapped: TypeMeta | DecoratorMeta = None,  # type: ignore
        fill_value: Any | Empty = EMPTY
    ) -> DecoratorMeta:
        """TODO"""
        # NOTE: metaclass automatically raises an error when wrapped is None.  Listing
        # it as such in the signature just sets the default value.
        is_empty = fill_value is EMPTY

        if wrapped.unwrapped is None:  # nested decorator without a base type
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






print("=" * 80)
print(f"aliases: {REGISTRY.aliases}")
print()
