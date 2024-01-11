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
    """Placeholder for optional arguments in which None might be a valid option."""

    def __bool__(self) -> bool:
        return False

    def __str__(self) -> str:
        return "..."

    def __repr__(self) -> str:
        return "..."


EMPTY: Empty = Empty()
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


# TODO: membership checks against the root Type or DecoratorType should return True for
# all types.


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

    def __contains__(self, spec: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(spec)

        if isinstance(typ, TypeMeta):
            return (typ.parent if typ.is_parametrized else typ) in self._types

        if isinstance(typ, DecoratorMeta):
            return typ.wrapper in self._types

        return all((t.parent if t.is_parametrized else t) in self._types for t in typ)

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
    def parent(self) -> TypeMeta | DecoratorMeta | None:
        """Get the type that the aliases point to."""
        return self._parent

    @parent.setter
    def parent(self, typ: TypeMeta | DecoratorMeta) -> None:
        """Set the type associated with the aliases.  This is called automatically by
        the metaclass machinery when a type is instantiated, and will raise an error if
        called after that point.
        """
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

    def add(self, alias: ALIAS) -> None:
        """Add an alias to a type, pushing it to the global registry."""
        if alias in REGISTRY.aliases:
            raise TypeError(f"aliases must be unique: {repr(alias)}")

        elif isinstance(alias, str):
            REGISTRY.refresh_regex()
            REGISTRY.strings[alias] = self.parent

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

        self.aliases.add(alias)

    def remove(self, alias: ALIAS) -> None:
        """Remove an alias from a type, removing it from the global registry."""
        if alias not in REGISTRY.aliases:
            raise TypeError(f"alias not found: {repr(alias)}")

        elif isinstance(alias, str):
            self.aliases.remove(alias)
            REGISTRY.refresh_regex()
            del REGISTRY.strings[alias]

        elif isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            self.aliases.remove(type(alias))
            del REGISTRY.dtypes[type(alias)]

        elif isinstance(alias, type):
            self.aliases.remove(alias)
            if issubclass(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                del REGISTRY.dtypes[alias]
            else:
                del REGISTRY.types[alias]

        else:
            raise TypeError(
                f"aliases must be strings, types, or dtypes: {repr(alias)}"
            )

    # TODO: update(), discard(), pop(), clear(), etc.

    def __repr__(self) -> str:
        return f"Aliases({', '.join(repr(a) for a in self.aliases)})"


class TypeBuilder:
    """Common base for all namespace builders.

    The builders that inherit from this class are responsible for transforming a type's
    simplified namespace into a fully-featured bertrand type.  They do this by looping
    through all the attributes that are defined in the class and executing any relevant
    helper functions to validate their configuration.  The builders also mark all
    required fields (those assigned to Ellipsis) and reserved attributes that they
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
        self.annotations = namespace.get("__annotations__", {})
        if parent is object:
            self.required = {}
            self.fields = {}
        else:
            self.required = self.parent._required.copy()
            self.fields = self.parent._fields.copy()

    def validate(self, arg: str, value: Any) -> None:
        """Validate a required argument by invoking its type hint with the specified
        value and current namespace.
        """
        hint = self.required.pop(arg)

        if isinstance(hint, str):
            func = get_from_calling_context(hint)
        else:
            func = hint

        try:
            result = func(value, self.namespace, self.fields)
            self.fields[arg] = result
            self.annotations[arg] = type(result)
        except Exception as err:
            raise type(err)(f"{self.name}.{arg} -> {str(err)}")

    def fill(self) -> TypeBuilder:
        """Fill in any reserved slots in the namespace with their default values.  This
        method should be called after parsing the namespace to avoid unnecessary work.
        """
        # required fields
        self.namespace["_required"] = self.required
        self.namespace["_fields"] = self.fields
        self.namespace["_slug"] = self.name
        self.namespace["_hash"] = hash(self.name)
        if self.parent is object or self.parent is Type or self.parent is DecoratorType:
            self.namespace["_parent"] = None
        else:
            self.namespace["_parent"] = self.parent
        self.namespace["_subtypes"] = LinkedSet()
        self.namespace["_implementations"] = LinkedDict()
        self.namespace["_parametrized"] = False
        self.namespace["_default"] = []
        self.namespace["_nullable"] = []
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict(max_size=self.cache_size)
        # self.namespace["__class_getitem__"]  # handled in subclasses
        # self.namespace["from_scalar"]
        # self.namespace["from_dtype"]
        # self.namespace["from_string"]

        # static fields (disables redirects)
        self.aliases()

        # default fields
        self.namespace.setdefault("__annotations__", {}).update(self.annotations)
        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Push a newly-created type into the global registry, registering any aliases
        provided in its namespace.
        """
        self.namespace["aliases"].parent = typ  # assigns aliases to global registry
        REGISTRY._types.add(typ)
        return typ

    def aliases(self) -> None:
        """Parse a type's aliases field (if it has one), registering each one in the
        global type registry.
        """
        if "aliases" in self.namespace:
            aliases = LinkedSet[str | type]()
            for alias in self.namespace["aliases"]:
                if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
                    aliases.add(type(alias))
                else:
                    aliases.add(alias)

            aliases.add_left(self.name)
            self.namespace["aliases"] = Aliases(aliases)

        else:
            self.namespace["aliases"] = Aliases(LinkedSet[str | type]({self.name}))


def get_from_calling_context(name: str) -> Any:
    """Get an object from the calling context given its fully-qualified (dotted) name
    as a string.

    Parameters
    ----------
    name : str
        The fully-qualified name of the object to retrieve.

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
        frame = frame.f_back  # skip this frame

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


REGISTRY = TypeRegistry()


#########################
####    RESOLVE()    ####
#########################


# TODO: it makes sense to modify aliases to match python syntax.  This would make it
# trivial to parse python-style type hints.


# TODO: register NoneType to Missing type, such that int | None resolves to
# Union[PythonInt, Missing]
# Same with None alias, which allows parsing of type hints with None as a type


# NOTE: string parsing currently does not support dictionaries or key-value mappings
# within Union[] specifiers (i.e. Union[{"foo": ...}], Union[[("foo", ...)]]).  This
# would add a lot of complexity, but could be added in the future if there's a need.


def resolve(target: TYPESPEC | Iterable[TYPESPEC]) -> META:
    """TODO"""
    if isinstance(target, (TypeMeta, DecoratorMeta, UnionMeta)):
        return target

    if isinstance(target, type):
        if target in REGISTRY.types:
            return REGISTRY.types[target]
        return ObjectType[target]

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
        typ = StringType
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
    """TODO"""
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
                index = np.full(missing.shape[0], NullType, dtype=object)
                index[~missing] = union.index  # pylint: disable=invalid-unary-operand-type
                return Union.from_types(union | NullType, _rle_encode(index))

            return union

    return _detect_scalar(data, data_type)


def _detect_scalar(data: Any, data_type: type) -> META:
    if pd.isna(data):
        return NullType

    typ = REGISTRY.types.get(data_type, None)
    if typ is None:
        return ObjectType(data_type)

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
            index = np.full(is_na.shape[0], NullType, dtype=object)
            if isinstance(result, UnionMeta):
                index[~is_na] = result.index  # pylint: disable=invalid-unary-operand-type
            else:
                index[~is_na] = result  # pylint: disable=invalid-unary-operand-type
            return Union.from_types(LinkedSet([result, NullType]), _rle_encode(index))

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
            typ = ObjectType[element_type]
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


class TypeMeta(type):
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
    _default: list[TypeMeta]  # NOTE: modify directly at the C++ level
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
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,
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

    def default(cls, redirect: TypeMeta) -> TypeMeta:
        """A class decorator that registers a default implementation for an abstract
        type.

        Parameters
        ----------
        redirect: TypeMeta
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

        if not issubclass(redirect, cls):
            raise TypeError(
                f"default implementation must be a subclass of {cls.__name__}"
            )

        if cls._default:
            cls._default.pop()
        cls._default.append(redirect)  # NOTE: easier at C++ level
        return redirect

    def nullable(cls, redirect: TypeMeta) -> TypeMeta:
        """A class decorator that registers an alternate, nullable implementation for
        a concrete type.

        Parameters
        ----------
        redirect: TypeMeta
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
        if cls.is_abstract:
            raise TypeError(
                f"abstract types cannot have nullable implementations: {cls.slug}"
            )
        if cls.is_nullable:
            raise TypeError(f"type is already nullable: {cls.slug}")

        if redirect.is_abstract:
            raise TypeError(
                f"nullable implementations must be concrete: {redirect.slug}"
            )

        if cls._nullable:
            cls._nullable.pop()
        cls._nullable.append(redirect)
        return redirect

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
        if cls.is_default:
            return cls
        return cls._default[0]

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
        if not cls._nullable:
            raise TypeError(f"type is not nullable -> {cls.slug}")
        return cls._nullable[0]

    ################################
    ####    CLASS PROPERTIES    ####
    ################################

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
        return MappingProxyType({k: cls._fields[k] for k in cls._params})

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
    def is_default(cls) -> bool:
        """Check whether the type is its own default implementation.

        Returns
        -------
        bool
            True if this type is concrete (and therefore cannot delegate) or if
            it is abstract and does not have a :meth:`default` implementation.
            False otherwise.

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
    def is_parametrized(cls) -> bool:
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

            >>> Object.is_parametrized
            False
            >>> Object[int].is_parametrized
            True
        """
        return cls._parametrized

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
        return cls.parent is None

    @property
    def is_leaf(cls) -> bool:
        """Check whether the type is a leaf of its type hierarchy.

        Returns
        -------
        bool
            True if the type has no children.  False otherwise.

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
        return not cls.subtypes and not cls.implementations

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
        parent = cls
        while parent.parent is not None:
            parent = parent.parent
        return parent

    @property
    def parent(cls) -> TypeMeta | None:
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

        Examples
        --------
        .. doctest::

            >>> Int.children
            Union[Signed, Int64, NumpyInt64, PandasInt64, Int32, NumpyInt32, PandasInt32, Int16, NumpyInt16, PandasInt16, Int8, NumpyInt8, PandasInt8, PythonInt]
            >>> Signed.children
            Union[Int64, NumpyInt64, PandasInt64, Int32, NumpyInt32, PandasInt32, Int16, NumpyInt16, PandasInt16, Int8, NumpyInt8, PandasInt8, PythonInt]
            >>> Int64.children
            Union[NumpyInt64, PandasInt64]
            >>> NumpyInt64.children
            Union[]
        """
        result = LinkedSet()
        _explode_children(cls, result)
        return Union.from_types(result[1:])

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
            Union[]
        """
        result = LinkedSet()
        _explode_leaves(cls, result)
        return Union.from_types(result[1:])

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
        result = LinkedSet(sorted(t for t in cls.root.leaves if t > cls))
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
        result = LinkedSet(sorted(t for t in cls.root.leaves if t < cls))
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
    def wrapped(cls) -> None:  # pylint: disable=redundant-returns-doc
        """Return the type that this type is decorating.

        Returns
        -------
        TypeMeta | DecoratorMeta | None
            If the type is a parametrized decorator, then the type that it wraps.
            Otherwise, None.

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
        return None

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
        if not cls.is_default:
            return getattr(cls.as_default, name)

        fields = super().__getattribute__("_fields")
        if name in fields:
            return fields[name]

        raise AttributeError(
            f"type object '{cls.__name__}' has no attribute '{name}'"
        )

    def __setattr__(cls, name: str, val: Any) -> NoReturn:
        raise TypeError("bertrand types are immutable")

    def __dir__(cls) -> set[str]:
        result = set(super().__dir__())
        result.update({
            "slug", "hash", "backend", "itemsize", "decorators", "wrapped",
            "unwrapped", "cache_size", "flyweights", "params", "is_abstract",
            "is_default", "is_parametrized", "is_root", "is_leaf", "root", "parent",
            "subtypes", "implementations", "children", "leaves", "larger", "smaller"
        })
        if cls.is_default:
            result.update(cls._fields)
        else:
            leaf = cls.as_default
            while not leaf.is_default:
                leaf = leaf.as_default
            result.update(leaf._fields)

        return result

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls.is_default:
            return pd.Series(*args, dtype=cls.dtype, **kwargs)
        return cls.as_default(*args, **kwargs)

    def __instancecheck__(cls, other: Any) -> bool:
        return cls.__subclasscheck__(detect(other))  # pylint: disable=no-value-for-parameter

    def __subclasscheck__(cls, other: TYPESPEC) -> bool:
        typ = resolve(other)
        check = super().__subclasscheck__
        if isinstance(typ, UnionMeta):
            return all(check(t) for t in typ)
        return check(typ)

    def __contains__(cls, other: Any | TYPESPEC) -> bool:
        try:
            return cls.__subclasscheck__(other)  # pylint: disable=no-value-for-parameter
        except TypeError:
            return cls.__instancecheck__(other)  # pylint: disable=no-value-for-parameter

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:  # pylint: disable=invalid-length-returned
        return cls.itemsize  # basically a Python-level sizeof() operator

    def __bool__(cls) -> bool:
        return True  # without this, Python defaults to len() > 0, which is wrong

    # TODO: reverse versions to allow for ["int16", "int32"] | Int64 ?

    def __or__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:  # type: ignore
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        if isinstance(typ, (TypeMeta, DecoratorMeta)):
            return Union.from_types(LinkedSet((cls, typ)))

        result = LinkedSet((cls,))
        result.update(typ)
        return Union.from_types(result)

    def __and__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) & typ

    def __sub__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) - typ

    def __xor__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> UnionMeta:
        try:
            typ = resolve(other)
        except TypeError:
            return NotImplemented

        return Union.from_types(LinkedSet((cls,))) ^ typ

    def __lt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls < t for t in typ)

        t1 = cls
        t2 = typ.parent if typ.is_parametrized else typ  # TODO: might have problems if t2 is a decorator
        while t2.parent is not None:  # type: ignore
            while t1.parent is not None:
                if (t1, t2) in REGISTRY.edges:
                    return True
                if (t2, t1) in REGISTRY.edges:
                    return False
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent  # type: ignore

        return _features(cls) < _features(typ)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls <= t for t in typ)

        t1 = cls
        t2 = typ.parent if typ.is_parametrized else typ  # TODO: might have problems if t2 is a decorator
        while t2.parent is not None:  # type: ignore
            while t1.parent is not None:
                if (t1, t2) in REGISTRY.edges:
                    return True
                if (t2, t1) in REGISTRY.edges:
                    return False
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent  # type: ignore

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
        t2 = typ.parent if typ.is_parametrized else typ  # TODO: might have problems if t2 is a decorator
        while t2.parent is not None:  # type: ignore
            while t1.parent is not None:
                if (t1, t2) in REGISTRY.edges:
                    return False
                if (t2, t1) in REGISTRY.edges:
                    return True
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent  # type: ignore

        return _features(cls) >= _features(typ)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls > t for t in typ)

        t1 = cls
        t2 = typ.parent if typ.is_parametrized else typ  # TODO: might have problems if t2 is a decorator
        while t2.parent is not None:  # type: ignore
            while t1.parent is not None:
                if (t1, t2) in REGISTRY.edges:
                    return False
                if (t2, t1) in REGISTRY.edges:
                    return True
                t1 = t1.parent

            t1 = cls
            t2 = t2.parent  # type: ignore

        return _features(cls) > _features(typ)

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


class AbstractBuilder(TypeBuilder):
    """A builder-style parser for an abstract type's namespace (backend == None).

    Abstract types support the creation of required fields through the Ellipsis syntax,
    which must be inherited or defined by each of their concrete implementations.  This
    rule is enforced by ConcreteBuilder, which is automatically chosen when the backend
    is not None.  Note that in contrast to concrete types, abstract types do not
    support parametrization, and attempting defining a corresponding constructor will
    throw an error.
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
        self.fields["backend"] = ""

    def parse(self) -> AbstractBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        namespace = self.namespace.copy()

        for name, value in namespace.items():
            if value is Ellipsis:
                self.required[name] = self.annotations.pop(name, _identity)
                del self.namespace[name]
            elif name in self.required:
                self.validate(name, self.namespace.pop(name))
            elif name in self.RESERVED and self.parent is not object:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )

        return self

    def fill(self) -> AbstractBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.
        """
        super().fill()
        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """TODO"""
        # pylint: disable=protected-access
        if self.parent is not object:
            super().register(typ)
            self.parent._subtypes.add(typ)

        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        if "__class_getitem__" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement __class_getitem__()")

        def default(cls: type, val: str | tuple[Any, ...]) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            # pylint: disable=protected-access
            if isinstance(val, str):
                key = val
                if key in cls._implementations:  # type: ignore
                    return cls._implementations[key]  # type: ignore
            else:
                key = val[0]
                if key in cls._implementations:  # type: ignore
                    return cls._implementations[key][*val[1:]]  # type: ignore

            if not cls.is_default:  # type: ignore
                default = cls.as_default  # type: ignore
                if default.is_abstract:
                    return default.__class_getitem__(val)

            raise TypeError(f"invalid backend: {repr(key)}")

        self.namespace["__class_getitem__"] = classmethod(default)
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

            if not cls.is_default:
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
        self.fields["backend"] = backend

    def parse(self) -> ConcreteBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        for name, value in self.namespace.copy().items():
            if value is Ellipsis:
                raise TypeError(
                    f"concrete types must not have required fields: {self.name}.{name}"
                )
            elif name in self.RESERVED:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )
            elif name in self.required:
                self.validate(name, self.namespace.pop(name))

        # validate any remaining required fields that were not explicitly assigned
        for name in list(self.required):
            self.validate(name, Ellipsis)

        return self

    def fill(self) -> ConcreteBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.
        """
        super().fill()
        self._class_getitem()
        self._from_scalar()
        self._from_dtype()
        self._from_string()

        return self

    def register(self, typ: TypeMeta | DecoratorMeta) -> TypeMeta | DecoratorMeta:
        """Instantiate the type and register it in the global type registry."""
        # pylint: disable=protected-access
        if self.parent is not object:
            super().register(typ)
            self.parent._implementations[self.backend] = typ

        return typ

    def _class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        parameters = {}

        if "__class_getitem__" in self.namespace:
            original = self.namespace["__class_getitem__"]

            def wrapper(cls: TypeMeta, val: Any | tuple[Any, ...]) -> TypeMeta:
                """Unwrap tuples and forward arguments to the original function."""
                if isinstance(val, tuple):
                    return original(cls, *val)
                return original(cls, val)

            self.namespace["__class_getitem__"] = classmethod(wrapper)  # type: ignore

            skip = True
            sig = inspect.signature(original)
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
            def default(cls: TypeMeta, val: Any | tuple[Any, ...]) -> TypeMeta:
                """Default to identity function."""
                if val:
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


def _identity(value: Any, namespace: dict[str, Any], processed: dict[str, Any]) -> Any:
    """Simple identity function used to validate required arguments that do not
    have any type hints.
    """
    if value is Ellipsis:
        raise TypeError("missing required field")
    return value


def _explode_children(t: TypeMeta, result: LinkedSet[TypeMeta]) -> None:  # lol
    """Explode a type's hierarchy into a flat list containing all of its subtypes and
    implementations.
    """
    result.add(t)
    for sub in t.subtypes:
        _explode_children(sub, result)
    for impl in t.implementations:
        _explode_children(impl, result)


def _explode_leaves(t: TypeMeta, result: LinkedSet[TypeMeta]) -> None:
    """Explode a type's hierarchy into a flat list of concrete leaf types."""
    if t.is_leaf:
        result.add(t)
    for sub in t.subtypes:
        _explode_leaves(sub, result)
    for impl in t.implementations:
        _explode_leaves(impl, result)


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


class DecoratorMeta(type):
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
    def is_parametrized(cls) -> bool:
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
    def parent(cls) -> DecoratorMeta | None:
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
        return cls._parent if cls.is_parametrized else cls

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
        fields = super().__getattribute__("_fields")
        if name in fields:
            return fields[name]

        wrapped = fields["wrapped"]
        if wrapped:
            return getattr(wrapped, name)

        raise AttributeError(
            f"type object '{cls.__name__}' has no attribute '{name}'"
        )

    def __setattr__(cls, name: str, val: Any) -> NoReturn:
        raise TypeError("bertrand types are immutable")

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

    def __instancecheck__(cls, other: Any) -> bool:
        # TODO: should require exact match?  i.e. isinstance(1, Sparse[Int]) == False.
        # This starts getting complicated
        return isinstance(other, cls.wrapped)

    # TODO:
    # >>> Sparse[Int16, 1] in Sparse[Int]
    # False  (should be True?)

    # TODO: probably need some kind of supplemental is_empty field that checks whether
    # parameters were supplied beyond wrapped.

    def __subclasscheck__(cls, other: type) -> bool:
        # naked types cannot be subclasses of decorators
        if isinstance(other, TypeMeta):
            return False

        wrapper = cls.wrapper
        wrapped = cls.wrapped

        if isinstance(other, DecoratorMeta):
            # top-level decorator types do not match
            if wrapper is not other.wrapper:
                return False

            # this decorator is not parametrized - treat as wildcard
            if wrapped is None:  # TODO: check for is_empty?  Or rework is_parametrized to refer to params beyond wrapped?
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

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return cls.__subclasscheck__(other)
        return cls.__instancecheck__(other)

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        return cls.itemsize

    def __bool__(cls) -> bool:
        return True  # without this, Python defaults to len() > 0, which is wrong

    # TODO: all of these should be able to accept a decorator, type, or union

    def __or__(cls, other: META | Iterable[META]) -> UnionMeta:  # type: ignore
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return Union.from_types(LinkedSet((cls, other)))

        result = LinkedSet(other)
        result.add_left(cls)
        return Union.from_types(result)

    def __and__(cls, other: META | Iterable[META]) -> UnionMeta:
        return Union.from_types(LinkedSet((cls,))) & other

    def __sub__(cls, other: META | Iterable[META]) -> UnionMeta:
        return Union.from_types(LinkedSet((cls,))) - other

    def __xor__(cls, other: META | Iterable[META]) -> UnionMeta:
        return Union.from_types(LinkedSet((cls,))) ^ other

    def __lt__(cls, other: META) -> bool:
        if cls is other:
            return False

        t1 = cls.wrapped
        if t1 is None:
            return False

        if isinstance(other, DecoratorMeta):
            t2 = other.wrapped
            if t2 is None:
                return False
            return t1 < t2

        return t1 < other

    def __le__(cls, other: META) -> bool:
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

    def __ge__(cls, other: META) -> bool:
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

    def __gt__(cls, other: META) -> bool:
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
            if value is Ellipsis:
                raise TypeError(
                    f"decorator types must not have required fields: {self.name}.{name}"
                )
            elif name in self.RESERVED:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )
            elif name in self.required:
                self.validate(name, self.namespace.pop(name))

        # validate any remaining required fields that were not explicitly assigned
        for name in list(self.required):
            self.validate(name, Ellipsis)

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
                val: Any | tuple[Any, ...]
            ) -> DecoratorMeta | UnionMeta:
                """Unwrap tuples/unions and forward arguments to the wrapped method."""
                if isinstance(val, (TypeMeta, DecoratorMeta)):
                    return _insort_decorator(invoke, cls, val)

                if isinstance(val, UnionMeta):
                    return Union.from_types(
                        LinkedSet(_insort_decorator(invoke, cls, t) for t in val)
                    )

                if isinstance(val, tuple):
                    wrapped = val[0]
                    args = val[1:]
                    if isinstance(wrapped, (TypeMeta, DecoratorMeta)):
                        return _insort_decorator(invoke, cls, wrapped, *args)
                    if isinstance(wrapped, UnionMeta):
                        result = LinkedSet(
                            _insort_decorator(invoke, cls, t, *args) for t in wrapped
                        )
                        return Union.from_types(result)

                raise TypeError(
                    f"Decorators must accept another bertrand type as a first "
                    f"argument, not {repr(val)}"
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

            def default(
                cls: DecoratorMeta,
                wrapped: TypeMeta | DecoratorMeta | UnionMeta | None = None
            ) -> DecoratorMeta | UnionMeta:
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


class UnionMeta(type):
    """Metaclass for all composite bertrand types (those produced by Union[] or setlike
    operators).

    This metaclass is responsible for delegating operations to the union's members, and
    is an example of the Gang of Four's `Composite Pattern
    <https://en.wikipedia.org/wiki/Composite_pattern>`.  The union can thus be treated
    similarly to an individual type, with operations producing new unions as output.

    See the documentation for the `Union` class for more information on how these work.
    """

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[type, ...],
        namespace: dict[str, Any],
    ) -> UnionMeta:
        if len(bases) != 0:
            raise TypeError("Union must not inherit from anything")

        return super().__new__(mcs, name, bases, namespace | {
            "_columns": None,
            "_types": LinkedSet(),
            "_hash": 42,
            "_index": None,
        })

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
                "_columns": {},
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

    def __setattr__(cls, key: str, val: Any) -> NoReturn:
        raise TypeError("unions are immutable")

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

    def __contains__(cls, other: Any | TYPESPEC) -> bool:
        try:
            return cls.__subclasscheck__(other)
        except TypeError:
            return cls.__instancecheck__(other)

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

    def __class_getitem__(cls, val: TYPESPEC | tuple[TYPESPEC, ...]) -> UnionMeta:
        typ = resolve(val)
        if isinstance(typ, UnionMeta):
            return typ
        return cls.from_types(LinkedSet((typ,)))


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


def check_scalar(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any],
) -> type:
    """Validate a scalar Python type provided in a bertrand type's namespace or infer
    it from a provided dtype.
    """
    if "scalar" in processed:  # auto-generated in check_dtype
        return processed["scalar"]

    dtype = namespace.get("dtype", Ellipsis)

    if value is Ellipsis:
        if dtype is Ellipsis:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(dtype, (np.dtype, pd.api.extensions.ExtensionDtype)):
            raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(dtype)}")

        processed["dtype"] = dtype
        return dtype.type

    if not isinstance(value, type):
        raise TypeError(f"scalar must be a Python type object, not {repr(value)}")

    if dtype is Ellipsis:
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


def check_dtype(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> np.dtype | pd.api.extensions.ExtensionDtype:
    """Validate a numpy or pandas dtype provided in a bertrand type's namespace or
    infer it from a provided scalar.
    """
    if "dtype" in processed:  # auto-generated in check_scalar
        return processed["dtype"]

    scalar = namespace.get("scalar", Ellipsis)

    if value is Ellipsis:
        if scalar is Ellipsis:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")
        if not isinstance(scalar, type):
            raise TypeError(f"scalar must be a Python type object, not {repr(scalar)}")

        processed["scalar"] = scalar
        return None  # TODO: synthesize dtype

    if not isinstance(value, (np.dtype, pd.api.extensions.ExtensionDtype)):
        raise TypeError(f"dtype must be a numpy/pandas dtype, not {repr(value)}")

    if scalar is Ellipsis:
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


def check_max(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a maximum value provided in a bertrand type's namespace."""
    if "max" in processed:  # auto-generated in check_min
        return processed["max"]

    min_val = namespace.get("min", -np.inf)

    if value is Ellipsis:
        processed["min"] = min_val
        return np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value < min_val:
        raise TypeError(f"max must be greater than min: {value} < {min_val}")

    processed["min"] = min_val
    return value


def check_min(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> int | float:
    """Validate a minimum value provided in a bertrand type's namespace."""
    if "min" in processed:  # auto-generated in check_max
        return processed["min"]

    max_val = namespace.get("max", np.inf)

    if value is Ellipsis:
        processed["max"] = max_val
        return -np.inf

    if not (isinstance(value, int) or isinstance(value, float) and value == -np.inf):
        raise TypeError(f"min must be an integer or infinity, not {repr(value)}")

    if value > max_val:
        raise TypeError(f"min must be less than or equal to max: {value} > {max_val}")

    processed["max"] = max_val
    return value


def check_is_nullable(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> bool:
    """Validate a nullability flag provided in a bertrand type's namespace."""
    return True if value is Ellipsis else bool(value)


def check_missing(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any]
) -> Any:
    """Validate a missing value provided in a bertrand type's namespace."""
    if value is Ellipsis:
        return pd.NA

    if not pd.isna(value):
        raise TypeError(f"missing value must pass a pandas.isna() check: {repr(value)}")

    return value


class Type(object, metaclass=TypeMeta):
    """TODO
    """

    registry = REGISTRY

    # NOTE: every type has aliases which are strings, python types, or subclasses of
    # np.dtype or pd.api.extensions.ExtensionDtype.  These aliases are used to identify
    # the type during detect() and resolve() calls according to the following rules:
    #   1.  Python type objects (e.g. int, float, datetime.datetime, or some other
    #       custom type) are used during detect() to identify scalar values.  This
    #       effectively calls the type() function on every index of an iterable and
    #       searches the alias registry for the associated type.  It then calls the
    #       type's from_scalar() method to allow for parametrization.
    #   2.  numpy.dtype and pandas.api.extensions.ExtensionDtype objects or their types
    #       (e.g. np.dtype("i4") vs type(np.dtype("i4"))) are used during detect() to
    #       identify array-like containers that implement a `.dtype` attribute.  When
    #       this occurs, the dtype's type is searched in the global registry to link
    #       if to a bertrand type.  If found, the type's from_dtype() method is called
    #       with the original dtype object.  The same process also occurs whenever
    #       resolve() is called with a literal dtype argument.
    #   3.  Strings are used during resolve() to identify types by their aliases.  When
    #       a registered alias is encountered, the associated type's from_string()
    #       method is called with the tokenized arguments.  Note that a type's aliases
    #       implicitly include its class name, so that the type can be identified when
    #       from __future__ import annotations is enabled.

    aliases = {"foo", int, type(np.dtype("i4"))}

    # NOTE: Any field assigned to Ellipsis (...) is a required attribute that must be
    # filled in by concrete subclasses of this type.  This rule is automatically
    # enforced by the metaclass using the type-hinted validation function, defaulting
    # to identity.  Each field can be inherited from a parent type unless otherwise
    # noted.  If a type does not define a required attribute, then the validation
    # function will receive Ellipsis as its value, and must either raise an error or
    # replace it with a default value.

    scalar: check_scalar = ...
    dtype: check_dtype = ...
    max: check_max = ...
    min: check_min = ...
    is_nullable: check_is_nullable = ...
    missing: check_missing = ...

    def __new__(cls) -> NoReturn:
        # NOTE: Type's metaclass overloads the __call__() operator to produce a series
        # of this type.  As such, it is not possible to instantiate a type directly,
        # and neither __init__ nor this method will ever be called.
        raise TypeError("bertrand types cannot have instances")

    def __class_getitem__(cls, x: str = "foo", y: int = 2) -> TypeMeta:
        """Example implementation showing how to create a parametrized type.  This
        method is a special case that will not be inherited by any subclasses, but can
        be overridden to provide support for parametrization using a flyweight cache.

        When this method is defined, the metaclass will analyze its signature and
        extract any default values, which will be forwarded to the class's base
        namespace.  Each argument must have a default value, and the signature must
        not contain any keyword arguments or varargs (i.e. *args or **kwargs) to be
        considered valid.

        Positional arguments to the flyweight() helper function should be supplied in
        the same order as they appear in the signature, and keywords are piped directly
        into the resulting class's namespace, overriding any existing values.  In the
        interest of speed, no checks are performed on the arguments, so it is up to the
        user to ensure that they are in the expected format.
        """
        return cls.flyweight(x, y, dtype=np.dtype("i4"), max=42)

    @classmethod
    def from_scalar(cls, scalar: Any) -> TypeMeta:
        """Example implementation showing how to parse a scalar value into a
        parametrized type.  This method is a special case that will not be inherited by
        any subclasses, and will be called automatically at every iteration of the
        detect() loop.  If left blank, it will default to the identity function, which
        will be optimized away during type detection.
        """
        foo = scalar.foo
        bar = scalar.do_stuff(1, 2, 3)
        return cls.flyweight(foo, bar, dtype=pd.DatetimeTZDtype(tz="UTC"))

    @classmethod
    def from_dtype(cls, dtype: Any) -> TypeMeta:
        """Example implementation showing how to parse a numpy/pandas dtype into a
        parametrized type.  This method is a special case that will not be inherited by
        any subclasses, and will be called automatically whenever detect() or resolve()
        encounters array-like data labeled with this type.  If left blank, it will
        default to the identity function, which will be optimized away during type
        detection.
        """
        unit = dtype.unit
        step_size = dtype.step_size
        return cls.flyweight(unit, step_size, dtype=dtype)

    @classmethod
    def from_string(cls, spam: str, eggs: str) -> TypeMeta:
        """Example implementation showing how to parse a string in the
        type-specification mini-language into a parametrized type.  This method is a
        special case that will not be inherited by any subclasses, and will be called
        automatically whenever resolve() encounters a string identifier that matches
        one of this type's aliases. The arguments are the comma-separated tokens parsed
        from the alias's argument list.  They are always provided as strings with no
        further processing other than stripping leading/trailing whitespace.  It is up
        to the user to parse them into the appropriate values.

        If left blank, this method will default to a zero-argument implementation,
        which effectively disables argument lists and will be optimized away during
        type resolution.
        """
        return cls.flyweight(int(spam), pd.Timestamp(eggs))

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        """Base implementation of the replace() method, which produces a new type with
        a modified set of parameters.
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
