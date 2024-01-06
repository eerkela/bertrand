from __future__ import annotations
import collections
import inspect
from types import MappingProxyType
from typing import (
    Any, Callable, Iterable, Iterator, Mapping, NoReturn, TypeAlias
)

import numpy as np
import pandas as pd
import regex  # alternate (PCRE-style) regex engine

from linked import *


class Empty:
    """Placeholder for optional arguments for which None might be a valid option."""

    def __repr__(self) -> str:
        return "..."


EMPTY: Empty = Empty()
POINTER_SIZE: int = np.dtype(np.intp).itemsize
META: TypeAlias = "TypeMeta | DecoratorMeta | UnionMeta"
DTYPE: TypeAlias = np.dtype[Any] | pd.api.extensions.ExtensionDtype
ALIAS: TypeAlias = str | type | DTYPE
TYPESPEC: TypeAlias = "META | ALIAS"
OBJECT_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[np.object_]]
RLE_ARRAY: TypeAlias = np.ndarray[Any, np.dtype[tuple[np.object_, np.intp]]]  # type: ignore


####################
####    BASE    ####
####################


# TODO: focus on getting normal and union types working solidly, then worry about
# decorators.  These are more complicated, since they can be applied to any type, and
# can be nested.  They also need to implement the same interface as normal types in a
# fairly intuitive way, which is tricky to get right.
# -> Also, flesh out TypeRegistry.  regex + comparison overrides.
# -> Could go whole hog and implement resolve()/detect() as registry methods.  That way
# we could tie them into type methods to approximate the final configuration.  We
# could also get a rough estimate of the performance we can expect.
# -> resolve()/detect() should be non-member methods for simplicity.


# TODO: Union comparisons should do the same thing as normal types in order to follow
# the composite pattern.  We would just add separate issubset(), issuperset(), and
# isdisjoint() methods to the Union class.

# Union[Int16 | Int32] < Int64 == True


# TODO: metaclass throws error if any non-classmethods are implemented on type objects.
# -> non-member functions are more explicit and flexible.  Plus, if there's only one
# syntax for declaring them, then it's easier to document and use.


class DecoratorPrecedence:
    """A linked set representing the priority given to each decorator in the type
    system.

    This interface is used to determine the order in which decorators are nested when
    applied to the same type.  For instance, if the precedence is set to `{A, B, C}`,
    and each is applied to a type `T`, then the resulting type will always be
    `A[B[C[T]]]`, no matter what order the decorators are applied in.  To demonstrate:

    .. code-block:: python

        >>> Type.registry.decorator_precedence
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

        >>> Type.registry.decorator_precedence.move_to_index(A, -1)
        >>> Type.registry.decorator_precedence
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
        """Get the index of a decorator in the precedence order."""
        return self._set.index(decorator)

    def distance(self, decorator1: DecoratorMeta, decorator2: DecoratorMeta) -> int:
        """Get the distance between a decorator and the end of the precedence order."""
        return self._set.distance(decorator1, decorator2)

    def higher(self, decorator1: DecoratorMeta, decorator2: DecoratorMeta) -> bool:
        """Check whether a decorator is higher in the precedence order than another."""
        return self.distance(decorator1, decorator2) > 0

    def lower(self, decorator1: DecoratorMeta, decorator2: DecoratorMeta) -> bool:
        """Check whether a decorator is lower in the precedence order than another."""
        return self.distance(decorator1, decorator2) < 0

    def move(self, decorator: DecoratorMeta, offset: int) -> None:
        """Move a decorator in the precedence order relative to its current position."""
        self._set.move(decorator, offset)

    def move_to_index(self, decorator: DecoratorMeta, index: int) -> None:
        """Move a decorator to the specified index in the precedence order."""
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

    # aliases are stored in separate dictionaries for performance reasons
    types: dict[type, TypeMeta | DecoratorMeta] = {}
    dtypes: dict[type, TypeMeta | DecoratorMeta] = {}
    strings: dict[str, TypeMeta | DecoratorMeta] = {}

    # overrides for type comparisons, decorator precedence
    comparison_overrides: set[tuple[TypeMeta, TypeMeta]] = set()
    decorator_precedence: DecoratorPrecedence = DecoratorPrecedence()

    def __init__(self) -> None:
        self._regex = None
        self._resolvable = None

    #######################
    ####    ALIASES    ####
    #######################

    @property
    def aliases(self) -> dict[type | str, TypeMeta | DecoratorMeta]:
        """Unify the type, dtype, and string registries into a single dictionary.
        These are usually separated for performance reasons.
        """
        return self.types | self.dtypes | self.strings

    @property
    def regex(self) -> regex.Pattern:
        """TODO"""
        if self._regex is None:
            if not self.strings:
                pattern = r".^"  # matches nothing
            else:
                escaped = [regex.escape(alias) for alias in self.strings]
                escaped.sort(key=len, reverse=True)  # avoids partial matches
                escaped.append(r"(?P<sized_unicode>U(?P<size>[0-9]*))$")  # U32, U...
                pattern = (
                    rf"(?P<type>{'|'.join(escaped)})"
                    rf"(?P<nested>\[(?P<args>([^\[\]]|(?&nested))*)\])?"  # recursive args
                )

            self._regex = regex.compile(pattern)

        return self._regex

    @property
    def resolvable(self) -> regex.Pattern:
        """TODO"""
        if self._resolvable is None:
            # match any number of comma-separated identifiers
            pattern = rf"(?P<atomic>{self.regex.pattern})(\s*[,|]\s*(?&atomic))*"

            # ignore optional prefix/suffix if given
            pattern = rf"(Union\[)?(?P<body>{pattern})(\])?"

            self._resolvable = regex.compile(pattern)

        return self._resolvable

    def flush_regex(self) -> None:
        """TODO"""
        self._regex = None
        self._resolvable = None

    ################################
    ####    GLOBAL ACCESSORS    ####
    ################################

    @property
    def roots(self) -> UnionMeta:
        """TODO"""
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_root)
        return Union.from_types(result)

    @property
    def leaves(self) -> UnionMeta:
        """TODO"""
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_leaf)
        return Union.from_types(result)

    @property
    def backends(self) -> dict[str, UnionMeta]:
        """TODO"""
        result: dict[str, LinkedSet] = {}
        for typ in self:
            if isinstance(typ, TypeMeta) and typ.backend:
                result.setdefault(typ.backend, LinkedSet()).add(typ)

        return {k: Union.from_types(v) for k, v in result.items()}

    @property
    def decorators(self) -> UnionMeta:
        """TODO"""
        result = LinkedSet(t for t in self if isinstance(t, DecoratorMeta))
        return Union.from_types(result)

    @property
    def abstract(self) -> UnionMeta:
        """TODO"""
        result = LinkedSet(t for t in self if isinstance(t, TypeMeta) and t.is_abstract)
        return Union.from_types(result)

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
    def parent(self) -> TypeMeta:
        """Get the type that the aliases point to."""
        return self._parent  # type: ignore

    @parent.setter
    def parent(self, typ: TypeMeta) -> None:
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
                REGISTRY.flush_regex()
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
            REGISTRY.flush_regex()
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
            REGISTRY.flush_regex()
            del REGISTRY.strings[alias]

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
        parent: TypeMeta | DecoratorMeta | type,
        namespace: dict[str, Any],
    ):
        self.name = name
        self.parent = parent
        self.namespace = namespace
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
        # self.namespace["_cache_size"]  # handled in subclasses
        # self.namespace["_flyweights"]
        # self.namespace["__class_getitem__"]
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

    This function avoids calling eval() and the associated security risks by walking up
    the call stack and searching the name in each frame's local, global, and built-in
    namespaces.  More specifically, it searches for the first component of a dotted
    name, and, if found, progressively resolves the remaining components by calling
    getattr() on the previous result until the string is exhausted.  Otherwise, if no
    match is found, it moves up to the next frame and repeats the process.

    This is primarily used to extract named objects from stringified type hints when
    `from __future__ import annotations` is enabled, or for parsing object strings in
    the type specification mini-language.

    NOTE: PEP 649 (https://peps.python.org/pep-0649/) could make this partially
    obsolete by avoiding stringification in the first place.  Still, it can be useful
    in other contexts, so it's worth keeping around.
    """
    path = name.split(".")
    prefix = path[0]
    frame = inspect.currentframe().f_back  # skip this frame
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

    raise TypeError(f"could not find object: {repr(name)}")


REGISTRY = TypeRegistry()


#########################
####    RESOLVE()    ####
#########################


def resolve(target: TYPESPEC | Iterable[TYPESPEC]) -> META:
    """TODO"""
    if isinstance(target, (TypeMeta, DecoratorMeta, UnionMeta)):
        return target

    if isinstance(target, type):
        if target in REGISTRY.types:
            return REGISTRY.types[target]
        return ObjectType[target]

    if isinstance(target, (np.dtype, pd.api.extensions.ExtensionDtype)):
        # if isinstance(target, SyntheticDtype):
        #     return target._bertrand_type

        typ = REGISTRY.dtypes.get(type(target), None)
        if typ is None:
            raise TypeError(f"unrecognized dtype -> {repr(target)}")

        return typ.from_dtype(target)

    if isinstance(target, str):
        stripped = target.strip()

        fullmatch = REGISTRY.resolvable.fullmatch(stripped)
        if not fullmatch:
            raise TypeError(f"invalid specifier -> {repr(target)}")

        matches = list(REGISTRY.regex.finditer(fullmatch.group("body")))
        if len(matches) > 1:
            result = LinkedSet(process_string(s) for s in matches)
            if len(result) == 1:
                return result[0]
            return Union.from_types(result)

        return process_string(matches[0])

    if hasattr(target, "__iter__"):
        result = LinkedSet(resolve(t) for t in target)  # type: ignore
        if len(result) == 1:
            return result[0]
        return Union.from_types(result)

    raise TypeError(f"invalid specifier -> {repr(target)}")


def process_string(match: regex.Match) -> TypeMeta | DecoratorMeta | UnionMeta:
    """TODO"""
    groups = match.groupdict()

    if groups.get("sized_unicode"):  # special case for U32, U{xx} syntax
        typ = StringType
    else:
        typ = REGISTRY.strings[groups["type"]]

    arguments = groups["args"]
    if not arguments:
        return typ

    args = []
    for m in TOKEN.finditer(arguments):
        substring = m.group()
        if (
            (substring.startswith("'") and substring.endswith("'")) or
            (substring.startswith('"') and substring.endswith('"'))
        ):
            substring = substring[1:-1]

        args.append(substring.strip())

    return typ.from_string(*args)


def nested_expr(prefix: str, suffix: str, group_name: str) -> str:
    """Produce a regular expression to match nested sequences with the specified
    opening and closing tokens.  Relies on PCRE-style recursive patterns.
    """
    opener = regex.escape(prefix)
    closer = regex.escape(suffix)
    body = rf"(?P<body>([^{opener}{closer}]|(?&{group_name}))*)"
    return rf"(?P<{group_name}>{opener}{body}{closer})"


INVOCATION = regex.compile(
    rf"(?P<invocation>[^\(\)\[\],]+)"
    rf"({nested_expr('(', ')', 'signature')}|{nested_expr('[', ']', 'options')})"
)
SEQUENCE = regex.compile(
    rf"(?P<sequence>"
    rf"{nested_expr('(', ')', 'parens')}|"
    rf"{nested_expr('[', ']', 'brackets')})"
)
LITERAL = regex.compile(r"[^,]+")
TOKEN = regex.compile(rf"{INVOCATION.pattern}|{SEQUENCE.pattern}|{LITERAL.pattern}")


########################
####    DETECT()    ####
########################


def detect(data: Any, drop_na: bool = True) -> META | dict[str, META]:
    """TODO"""
    data_type = type(data)

    if issubclass(data_type, (TypeMeta, DecoratorMeta, UnionMeta)):
        return data

    if issubclass(data_type, pd.DataFrame):  # TODO: or a numpy array with a structured dtype
        columnwise = {}
        for col in data.columns:
            columnwise[col] = detect(data[col], drop_na=drop_na)
        return columnwise

    if hasattr(data, "__iter__") and not isinstance(data, type):
        if data_type in REGISTRY.types:
            return detect_scalar(data, data_type)
        elif hasattr(data, "dtype"):
            return detect_dtype(data, data_type, drop_na)
        else:
            # convert to numpy array
            arr = np.asarray(data, dtype=object)
            missing = pd.isna(arr)
            hasnans = missing.any()
            if hasnans:
                arr = arr[~missing]  # pylint: disable=invalid-unary-operand-type

            # loop through elementwise
            union = detect_elementwise(arr, data_type, drop_na)

            # reinsert missing values
            if hasnans and not drop_na:
                index = np.full(missing.shape[0], NullType, dtype=object)
                index[~missing] = union.index  # pylint: disable=invalid-unary-operand-type
                return Union.from_types(union | NullType, rle_encode(index))

            return union

    return detect_scalar(data, data_type)


def detect_scalar(data: Any, data_type: type) -> META:
    """TODO"""
    if pd.isna(data):
        return NullType

    typ = REGISTRY.types.get(data_type, None)
    if typ is None:
        return ObjectType(data_type)
    return typ.from_scalar(data)


def detect_dtype(data: Any, data_type: type, drop_na: bool) -> META:
    """TODO"""
    dtype = data.dtype

    if dtype == np.dtype(object):  # ambiguous - loop through elementwise
        return detect_elementwise(data, data_type, drop_na)

    # if isinstance(dtype, SyntheticDtype):
    #     typ = dtype._bertrand_type
    # else:
    typ = REGISTRY.dtypes.get(type(dtype), None)
    if typ is None:
        raise TypeError(f"unrecognized dtype -> {repr(dtype)}")

    result = typ.from_dtype(dtype)

    if not drop_na:
        is_na = pd.isna(data)
        if is_na.any():
            index = np.full(is_na.shape[0], NullType, dtype=object)
            if isinstance(result, UnionMeta):
                index[~is_na] = result.index  # pylint: disable=invalid-unary-operand-type
            else:
                index[~is_na] = result  # pylint: disable=invalid-unary-operand-type

            # TODO: from_types must accept optional index
            return Union.from_types(LinkedSet((result, NullType)), rle_encode(index))

    return result


def detect_elementwise(data: Any, data_type: type, drop_na: bool) -> UnionMeta:
    """TODO"""
    observed = LinkedSet()
    counts = []
    count = 0
    prev = None
    lookup = REGISTRY.types

    for element in data:
        element_type = type(element)

        # search for a matching type in the registry
        typ = lookup.get(element_type, None)
        if typ is None:
            typ = ObjectType[element_type]
        # else:
        #     typ = typ.from_scalar(element)  # TODO: skip if type is not parametrizable

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


def rle_encode(arr: OBJECT_ARRAY) -> RLE_ARRAY:  # type: ignore
    """TODO"""
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


def rle_decode(arr: RLE_ARRAY) -> OBJECT_ARRAY:
    """TODO"""
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

    # NOTE: this metaclass uses the following internal fields, which are populated by
    # either AbstractBuilder or ConcreteBuilder:
    #   _slug: str
    #   _hash: int
    #   _required: dict[str, Callable[..., Any]]
    #   _fields: dict[str, Any]
    #   _parent: TypeMeta | None
    #   _subtypes: LinkedSet[TypeMeta]
    #   _implementations: LinkedDict[str, TypeMeta]
    #   _default: list[TypeMeta]  # TODO: modify directly rather than using a list
    #   _nullable: list[TypeMeta]
    #   _params: tuple[str]
    #   _parametrized: bool
    #   _cache_size: int | None
    #   _flyweights: LinkedDict[str, TypeMeta]

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
            print(f"fields: {format_dict(fields)}")
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
            build = AbstractBuilder(name, bases, namespace)
        else:
            build = ConcreteBuilder(name, bases, namespace, backend, cache_size)

        return build.parse().fill().register(
            super().__new__(mcs, build.name, (build.parent,), build.namespace)
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

    def default(cls, implementation: TypeMeta) -> TypeMeta:
        """A class decorator that registers a default implementation for an abstract
        type.
        """
        if not cls.is_abstract:
            raise TypeError("concrete types cannot have default implementations")

        if not issubclass(implementation, cls):
            raise TypeError(
                f"default implementation must be a subclass of {cls.__name__}"
            )

        if cls._default:
            cls._default.pop()
        cls._default.append(implementation)  # NOTE: easier at C++ level
        return implementation

    def nullable(cls, implementation: TypeMeta) -> TypeMeta:
        """A class decorator that registers an alternate, nullable implementation for
        a concrete type.
        """
        if cls.is_abstract:
            raise TypeError(
                f"abstract types cannot have nullable implementations: {cls.slug}"
            )

        if cls.is_nullable:
            raise TypeError(f"type is already nullable: {cls.slug}")

        if cls._nullable:
            cls._nullable.pop()
        cls._nullable.append(implementation)
        return implementation

    @property
    def as_default(cls) -> TypeMeta:
        """Convert an abstract type to its default implementation, if it has one."""
        return cls if cls.is_default else cls._default[0]

    @property
    def as_nullable(cls) -> TypeMeta:
        """Convert a concrete type to its nullable implementation, if it has one."""
        return cls if cls.is_nullable else cls._nullable[0]

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
    def backend(cls) -> str:
        """TODO"""
        return cls._fields["backend"]

    @property
    def itemsize(cls) -> int:
        """TODO"""
        return cls.dtype.itemsize

    @property
    def cache_size(cls) -> int:
        """TODO"""
        return cls._cache_size

    @property
    def flyweights(cls) -> Mapping[str, TypeMeta]:
        """TODO"""
        return MappingProxyType(cls._flyweights)

    @property
    def params(cls) -> Mapping[str, Any]:
        """TODO"""
        return MappingProxyType({k: cls._fields[k] for k in cls._params})

    @property
    def is_abstract(cls) -> bool:
        """TODO"""
        return not cls.backend

    @property
    def is_default(cls) -> bool:
        """Indicates whether this type redirects to a default implementation."""
        return not cls._default

    @property
    def is_parametrized(cls) -> bool:
        """TODO"""
        return cls._parametrized

    @property
    def is_root(cls) -> bool:
        """TODO"""
        return cls.parent is None

    @property
    def is_leaf(cls) -> bool:
        """TODO"""
        return not cls.subtypes and not cls.implementations

    @property
    def root(cls) -> TypeMeta:
        """TODO"""
        parent = cls
        while parent.parent is not None:
            parent = parent.parent
        return parent

    @property
    def parent(cls) -> TypeMeta | None:
        """TODO"""
        return cls._parent

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        return Union.from_types(cls._subtypes)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        return Union.from_types(LinkedSet(cls._implementations.values()))

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        explode_children(cls, result)
        return Union.from_types(result[1:])

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        explode_leaves(cls, result)
        return Union.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet[TypeMeta](sorted(t for t in cls.root.leaves if t > cls))
        return Union.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet[TypeMeta](sorted(t for t in cls.root.leaves if t < cls))
        return Union.from_types(result)

    @property
    def decorators(cls) -> UnionMeta:
        """TODO"""
        return Union.from_types(LinkedSet())  # always empty

    @property
    def wrapper(cls) -> None:
        """TODO"""
        return None  # base case for decorators

    @property
    def wrapped(cls) -> None:
        """TODO"""
        return None  # base case for recursion

    @property
    def unwrapped(cls) -> TypeMeta:
        """TODO"""
        return cls  # for consistency with decorators

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
        typ = detect(other)

        # pylint: disable=no-value-for-parameter
        if isinstance(typ, dict):
            return all(cls.__subclasscheck__(t) for t in typ.values())
        return cls.__subclasscheck__(typ)

    def __subclasscheck__(cls, other: TYPESPEC) -> bool:
        check = super().__subclasscheck__
        typ = resolve(other)
        if isinstance(typ, UnionMeta):
            return all(check(t) for t in typ)
        return check(typ)

    def __contains__(cls, other: Any | TYPESPEC) -> bool:
        # pylint: disable=no-value-for-parameter
        if (
            isinstance(other, type) or
            isinstance(other, (np.dtype, pd.api.extensions.ExtensionDtype)) or
            isinstance(other, str) and REGISTRY.resolvable.fullmatch(other)
        ):
            return cls.__subclasscheck__(other)
        return cls.__instancecheck__(other)

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

        if (cls, typ) in REGISTRY.comparison_overrides:
            return True
        if (typ, cls) in REGISTRY.comparison_overrides:
            return False

        return features(cls) < features(typ)

    def __le__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls <= t for t in typ)

        if (cls, typ) in REGISTRY.comparison_overrides:
            return True
        if (typ, cls) in REGISTRY.comparison_overrides:
            return False

        return features(cls) <= features(typ)

    def __eq__(cls, other: Any) -> bool:
        try:
            typ = resolve(other)
        except TypeError:
            return False

        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls < t for t in typ)

        if (cls, typ) in REGISTRY.comparison_overrides:
            return False
        if (typ, cls) in REGISTRY.comparison_overrides:
            return False

        return features(cls) == features(typ)

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return True

        if isinstance(typ, UnionMeta):
            return all(cls >= t for t in typ)

        if (cls, typ) in REGISTRY.comparison_overrides:
            return False
        if (typ, cls) in REGISTRY.comparison_overrides:
            return True

        return features(cls) >= features(typ)

    def __gt__(cls, other: TYPESPEC | Iterable[TYPESPEC]) -> bool:
        typ = resolve(other)
        if cls is typ:
            return False

        if isinstance(typ, UnionMeta):
            return all(cls > t for t in typ)

        if (cls, typ) in REGISTRY.comparison_overrides:
            return False
        if (typ, cls) in REGISTRY.comparison_overrides:
            return True

        return features(cls) > features(typ)

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

    def __init__(self, name: str, bases: tuple[TypeMeta], namespace: dict[str, Any]):
        if len(bases) != 1 or not (bases[0] is object or issubclass(bases[0], Type)):
            raise TypeError(
                f"abstract types must inherit from bertrand.Type or one of its "
                f"subclasses, not {bases}"
            )

        parent = bases[0]
        if parent is not object and not parent.is_abstract:
            raise TypeError("abstract types must not inherit from concrete types")

        super().__init__(name, parent, namespace)
        self.fields["backend"] = ""

    def parse(self) -> AbstractBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        namespace = self.namespace.copy()

        for name, value in namespace.items():
            if value is Ellipsis:
                self.required[name] = self.annotations.pop(name, identity)
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
        any derived attributes."""
        super().fill()
        self.namespace["_cache_size"] = None
        self.namespace["_flyweights"] = LinkedDict()

        self.class_getitem()
        self.from_scalar()
        self.from_dtype()
        self.from_string()

        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """TODO"""
        if self.parent is not object:
            super().register(typ)
            self.parent._subtypes.add(typ)

        return typ

    def class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        if "__class_getitem__" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement __class_getitem__()")

        def default(cls: type, val: str | tuple[Any, ...]) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
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

    def from_scalar(self) -> None:
        """Parse a type's from_scalar() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_scalar" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_scalar()")

        # TODO: alternatively, we could forward to the default implementation

        def default(cls: type, scalar: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError("abstract types cannot be constructed using from_scalar()")

        self.namespace["from_scalar"] = classmethod(default)

    def from_dtype(self) -> None:
        """Parse a type's from_dtype() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_dtype" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_dtype()")

        def default(cls: type, dtype: Any) -> TypeMeta:
            """Throw an error if attempting to construct an abstract type."""
            raise TypeError("abstract types cannot be constructed using from_dtype()")

        self.namespace["from_dtype"] = classmethod(default)

    def from_string(self) -> None:
        """Parse a type's from_string() method (if it has one), ensuring that it is
        callable with the same number of positional arguments as __class_getitem__().
        """
        if "from_string" in self.namespace and self.parent is not object:
            raise TypeError("abstract types must not implement from_string()")

        def default(cls: type, backend: str = "", *args: str) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            if not backend:
                return cls

            if backend in cls._implementations:  # type: ignore
                typ = cls._implementations[backend]  # type: ignore
                if not args:
                    return typ
                return typ.from_string(*args)

            if not cls.is_default:  # type: ignore
                return cls.as_default.from_string(backend, *args)  # type: ignore

            raise TypeError(f"invalid backend: {repr(backend)}")

        self.namespace["from_string"] = classmethod(default)


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

        super().__init__(name, parent, namespace)

        self.backend = backend
        self.cache_size = cache_size
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
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict(max_size=self.cache_size)

        self.class_getitem()
        self.from_scalar()
        self.from_dtype()
        self.from_string()

        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """Instantiate the type and register it in the global type registry."""
        if self.parent is not object:
            super().register(typ)
            self.parent._implementations[self.backend] = typ

        return typ

    def class_getitem(self) -> None:
        """Parse a type's __class_getitem__ method (if it has one), producing a list of
        parameters and their default values, as well as a wrapper function that can be
        used to instantiate the type.
        """
        parameters = {}

        if "__class_getitem__" in self.namespace:
            original = self.namespace["__class_getitem__"]

            def wrapper(cls: type, val: Any | tuple[Any, ...]) -> TypeMeta:
                """Unwrap tuples and forward arguments to the original function."""
                if isinstance(val, tuple):
                    return original(cls, *val)
                return original(cls, val)

            self.namespace["__class_getitem__"] = classmethod(wrapper)

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
            def default(cls: type, val: Any | tuple[Any, ...]) -> TypeMeta:
                """Default to identity function."""
                if val:
                    raise TypeError(f"{cls.__name__} does not accept any parameters")
                return cls  # type: ignore

            self.namespace["__class_getitem__"] = classmethod(default)
            self.class_getitem_signature = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD)
            ])

        self.namespace["_params"] = tuple(parameters.keys())

    def from_scalar(self) -> None:
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

        else:
            def default(cls: type, scalar: Any) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_scalar"] = classmethod(default)

    def from_dtype(self) -> None:
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
                    "from_dtype() must accept a single positional argument (in "
                    "addition to cls)"
                )

        else:
            def default(cls: type, dtype: Any) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_dtype"] = classmethod(default)

    def from_string(self) -> None:
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
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_string"] = classmethod(default)
            sig = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD)
            ])

        if sig != self.class_getitem_signature:
            raise TypeError(
                f"the signature of from_string() must match __class_getitem__(): "
                f"{str(sig)} != {str(self.class_getitem_signature)}"
            )


def format_dict(d: dict[str, Any]) -> str:
    """Convert a dictionary into a string with standard indentation."""
    if not d:
        return "{}"

    prefix = "{\n    "
    sep = ",\n    "
    suffix = "\n}"
    return prefix + sep.join(f"{k}: {repr(v)}" for k, v in d.items()) + suffix


def identity(value: Any, namespace: dict[str, Any], processed: dict[str, Any]) -> Any:
    """Simple identity function used to validate required arguments that do not
    have any type hints.
    """
    if value is Ellipsis:
        raise TypeError("missing required field")
    return value


def explode_children(t: TypeMeta, result: LinkedSet[TypeMeta]) -> None:  # lol
    """Explode a type's hierarchy into a flat list containing all of its subtypes and
    implementations.
    """
    result.add(t)
    for sub in t.subtypes:
        explode_children(sub, result)
    for impl in t.implementations:
        explode_children(impl, result)


def explode_leaves(t: TypeMeta, result: LinkedSet[TypeMeta]) -> None:
    """Explode a type's hierarchy into a flat list of concrete leaf types."""
    if t.is_leaf:
        result.add(t)
    for sub in t.subtypes:
        explode_leaves(sub, result)
    for impl in t.implementations:
        explode_leaves(impl, result)


def features(t: TypeMeta | DecoratorMeta) -> tuple[int | float, int, bool, int]:
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
    #   _slug: str
    #   _hash: int
    #   _required: dict[str, Callable[..., Any]]
    #   _fields: dict[str, Any]
    #   _parent: DecoratorMeta      # NOTE: parent of this decorator, not wrapped
    #   _params: tuple[str]
    #   _parametrized: bool
    #   _cache_size: int | None
    #   _flyweights: LinkedDict[str, DecoratorMeta]

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
            print(f"fields: {format_dict(cls._fields)}")
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

        super().__init__(name, bases[0], namespace)

        self.cache_size = cache_size
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
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict(max_size=self.cache_size)

        self.class_getitem()
        self.from_scalar()
        self.from_dtype()
        self.from_string()
        self.transform()
        self.inverse_transform()

        return self

    def register(self, typ: DecoratorMeta) -> DecoratorMeta:
        """Instantiate the type and register it in the global type registry."""
        if self.parent is not object:
            super().register(typ)
            REGISTRY.decorator_precedence._set.add(typ)  # pylint: disable=protected-access

        return typ

    def class_getitem(self) -> None:
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
                    return insort_decorator(invoke, cls, val)

                if isinstance(val, UnionMeta):
                    return Union.from_types(
                        LinkedSet(insort_decorator(invoke, cls, t) for t in val)
                    )

                if isinstance(val, tuple):
                    wrapped = val[0]
                    args = val[1:]
                    if isinstance(wrapped, (TypeMeta, DecoratorMeta)):
                        return insort_decorator(invoke, cls, wrapped, *args)
                    if isinstance(wrapped, UnionMeta):
                        result = LinkedSet(
                            insort_decorator(invoke, cls, t, *args) for t in wrapped
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
                    return insort_decorator(invoke, cls, wrapped)

                if isinstance(wrapped, UnionMeta):
                    return Union.from_types(
                        LinkedSet(insort_decorator(invoke, cls, t) for t in wrapped)
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

    def from_scalar(self) -> None:
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

        else:
            def default(cls: type, scalar: Any) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_scalar"] = classmethod(default)

    def from_dtype(self) -> None:
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
                    "from_dtype() must accept a single positional argument (in "
                    "addition to cls)"
                )

        else:
            def default(cls: type, dtype: Any) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_dtype"] = classmethod(default)

    def from_string(self) -> None:
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
            def default(cls: type, wrapped: str) -> TypeMeta:
                """Default to identity function."""
                return cls[wrapped]

            self.namespace["from_string"] = classmethod(default)
            sig = inspect.Signature([
                inspect.Parameter("cls", inspect.Parameter.POSITIONAL_OR_KEYWORD),
                inspect.Parameter("wrapped", inspect.Parameter.POSITIONAL_OR_KEYWORD),
            ])

        if sig != self.class_getitem_signature:
            raise TypeError(
                f"the signature of from_string() must match __class_getitem__() "
                f"exactly: {str(sig)} != {str(self.class_getitem_signature)}"
            )

    def transform(self) -> None:
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

    def inverse_transform(self) -> None:
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


def insort_decorator(
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
        delta = REGISTRY.decorator_precedence.distance(cls, wrapper)

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


class UnionMeta(type):
    """Metaclass for all composite bertrand types (those produced by Union[] or the
    bitwise or operator).

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
            "_types": LinkedSet(),
            "_hash": 42,
            "_index": None,
        })

    def from_types(cls, types: LinkedSet[TypeMeta], index: RLE_ARRAY | None = None) -> UnionMeta:
        """TODO"""
        hash_val = 42  # to avoid collisions at hash=0
        for t in types:
            hash_val = (hash_val * 31 + hash(t)) % (2**63 - 1)  # TODO: mod not necessary in C++

        return super().__new__(UnionMeta, cls.__name__, (cls,), {
            "_types": types,
            "_hash": hash_val,
            "_index": index,
            "__class_getitem__": union_getitem
        })

    ################################
    ####    UNION ATTRIBUTES    ####
    ################################

    @property
    def index(cls) -> OBJECT_ARRAY | None:
        """A 1D numpy array containing the observed type at every index of an iterable
        passed to `bertrand.detect()`.  This is stored internally as a run-length
        encoded array of flyweights for memory efficiency, and is expanded into a
        full array when accessed.
        """
        if cls._index is None:
            return None
        return rle_decode(cls._index)

    def collapse(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            if not any(t is not other and t in other for other in cls):
                result.add(t)
        return cls.from_types(result)

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def as_default(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.as_default for t in cls))

    @property
    def as_nullable(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.as_nullable for t in cls))

    @property
    def root(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.root for t in cls))

    @property
    def parent(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.add(t.parent if t.parent else t)
        return cls.from_types(result)

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.subtypes)
        return cls.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.implementations)
        return cls.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.children)
        return cls.from_types(result)

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.leaves)
        return cls.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.larger)
        return cls.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.smaller)
        return cls.from_types(result)

    @property
    def decorators(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls:
            result.update(t.decorators)
        return cls.from_types(result)

    @property
    def wrapper(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.wrapper for t in cls))

    @property
    def wrapped(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.wrapped for t in cls))

    @property
    def unwrapped(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.unwrapped for t in cls))

    def __getattr__(cls, name: str) -> dict[TypeMeta, Any]:
        types = super().__getattribute__("_types")
        return {t: getattr(t, name) for t in types}

    def __setattr__(cls, key: str, val: Any) -> NoReturn:
        raise TypeError("unions are immutable")

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        """TODO"""
        for t in cls:
            try:
                return t(*args, **kwargs)
            except Exception:
                continue

        raise TypeError(f"cannot convert to union type: {repr(cls)}")

    def __instancecheck__(cls, other: Any) -> bool:
        return any(isinstance(other, t) for t in cls)

    def __subclasscheck__(cls, other: type) -> bool:
        check = lambda o: any(issubclass(o, t) for t in cls)
        if isinstance(other, UnionMeta):
            return all(check(o) for o in other)
        return check(other)

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return cls.__subclasscheck__(other)
        return cls.__instancecheck__(other)

    #############################
    ####    UNION METHODS    ####
    #############################

    def replace(cls, *args: Any, **kwargs: Any) -> UnionMeta:
        """TODO"""
        return Union.from_types(
            LinkedSet(t.replace(*args, **kwargs) for t in cls)
        )

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> UnionMeta:
        """TODO"""
        result = cls._types.copy()
        result.sort(key=key, reverse=reverse)
        return cls.from_types(result)

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

    def __or__(cls, other: META | Iterable[META]) -> UnionMeta:  # type: ignore
        if isinstance(other, UnionMeta):
            result = cls._types | other._types
        elif isinstance(other, (TypeMeta, DecoratorMeta)):
            result = cls._types | (other,)
        else:
            result = cls._types | other
        return cls.from_types(result)

    def __and__(cls, other: META | Iterable[META]) -> UnionMeta:
        if isinstance(other, UnionMeta):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if any(typ in t for t in other):
                    result.add(typ)

        elif isinstance(other, (TypeMeta, DecoratorMeta)):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if typ in other:
                    result.add(typ)

        else:
            return NotImplemented

        return cls.from_types(result).collapse()

    def __sub__(cls, other: META | Iterable[META]) -> UnionMeta:
        if isinstance(other, UnionMeta):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if not any(t in typ or typ in t for t in other):
                    result.add(typ)

        elif isinstance(other, (TypeMeta, DecoratorMeta)):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if not (other in typ or typ in other):
                    result.add(typ)

        else:
            return NotImplemented

        return cls.from_types(result).collapse()

    def __xor__(cls, other: META | Iterable[META]) -> UnionMeta:
        if isinstance(other, UnionMeta):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if not any(t in typ or typ in t for t in other):
                    result.add(typ)
            for typ in other._types | other.children:
                if not any(t in typ or typ in t for t in cls):
                    result.add(typ)

        elif isinstance(other, (TypeMeta, DecoratorMeta)):
            result = LinkedSet()
            for typ in cls._types | cls.children:
                if not (other in typ or typ in other):
                    result.add(typ)
            for typ in other | other.children:
                if not (t in typ or typ in t for t in cls):
                    result.add(typ)

        else:
            return NotImplemented

        return cls.from_types(result).collapse()

    def __lt__(cls, other: META | Iterable[META]) -> bool:
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            other = cls.from_types(LinkedSet([other]))
        elif not isinstance(other, UnionMeta):
            return NotImplemented

        return all(t in other for t in cls._types) and len(cls) < len(other)

    def __le__(cls, other: META | Iterable[META]) -> bool:
        if other is cls:
            return True

        if isinstance(other, (TypeMeta, DecoratorMeta)):
            other = cls.from_types(LinkedSet([other]))
        elif not isinstance(other, UnionMeta):
            return NotImplemented

        return all(t in other for t in cls._types) and len(cls) <= len(other)

    def __eq__(cls, other: Any) -> bool:
        if isinstance(other, UnionMeta):
            return (
                cls is other or
                len(cls) == len(other) and
                all(t is u for t, u in zip(cls, other))
            )

        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return len(cls) == 1 and cls._types[0] is other

        return NotImplemented

    def __ne__(cls, other: Any) -> bool:
        return not cls == other

    def __ge__(cls, other: META | Iterable[META]) -> bool:
        if not isinstance(other, (TypeMeta, UnionMeta)):
            other = cls.from_types(other)
        return cls.children._types >= other.children._types

    def __gt__(cls, other: META | Iterable[META]) -> bool:
        if not isinstance(other, (TypeMeta, UnionMeta)):
            other = cls.from_types(other)
        return cls.children._types > other.children._types

    def __str__(cls) -> str:
        return f"{' | '.join(t.slug for t in cls._types)}"

    def __repr__(cls) -> str:
        return f"Union[{', '.join(t.slug for t in cls._types)}]"


class Union(metaclass=UnionMeta):
    """TODO
    """

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand unions cannot be instantiated")

    def __class_getitem__(
        cls,
        val: (TypeMeta | DecoratorMeta | UnionMeta |
              tuple[TypeMeta | DecoratorMeta | UnionMeta, ...])
    ) -> UnionMeta:
        if isinstance(val, (TypeMeta, DecoratorMeta)):
            return cls.from_types(LinkedSet((val,)))
        if isinstance(val, UnionMeta):
            return val
        if isinstance(val, tuple):
            return cls.from_types(LinkedSet(val))

        raise TypeError(
            f"Union types must be instantiated with a bertrand type, not {repr(val)}"
        )


def union_getitem(cls: UnionMeta, key: int | slice) -> TypeMeta | DecoratorMeta | UnionMeta:
    """A parametrized replacement for the base Union's __class_getitem__() method that
    allows for integer-based indexing/slicing of a union's types.
    """
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
    elif value == dtype.type:
        processed["dtype"] = dtype
    else:
        raise TypeError(
            f"scalar must be consistent with dtype.type: {repr(value)} != "
            f"{repr(dtype.type)}"
        )

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
    elif value.type == scalar:
        processed["scalar"] = scalar
    else:
        raise TypeError(
            f"dtype.type must be consistent with scalar: {repr(value.type)} != "
            f"{repr(scalar)}"
        )

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


# class PythonInt(Signed, backend="python"):
#     aliases = {int}
#     scalar = int
#     dtype = np.dtype(object)
#     max = np.inf
#     min = -np.inf
#     is_nullable = True
#     missing = None



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




# TODO: nested sparse/categorical doesn't work unless levels are explicitly specified
# >>> Sparse[Categorical[Int]]([1, 2, 3])
# 0    1
# 1    2
# 2    3
# dtype: category
# Categories (3, int64): [1, 2, 3]
# >>> Sparse[Categorical[Int, [1, 2, 3]]]([1, 2, 3])
# 0    1
# 1    2
# 2    3
# dtype: category
# Categories (3, Sparse[int64, <NA>]): [1, 2, 3]


class Categorical(DecoratorType, cache_size=256):
    aliases = {"categorical"}

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
        else:
            levels = wrapped(levels)
            slug_repr = list(levels)
            dtype = pd.CategoricalDtype(levels)

        return cls.flyweight(wrapped, slug_repr, dtype=dtype, levels=levels)

    @classmethod
    def from_string(cls, wrapped: str, levels: str | Empty = EMPTY) -> DecoratorMeta:
        """TODO"""
        typ = resolve(wrapped)
        # TODO: parse fill_value
        return cls[typ, levels]


class Sparse(DecoratorType, cache_size=256):
    aliases = {"sparse"}
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
        else:
            fill = wrapped(fill_value)
            if len(fill) != 1:
                raise TypeError(f"fill_value must be a scalar, not {repr(fill_value)}")
            fill = fill[0]

        dtype = pd.SparseDtype(wrapped.dtype, fill)
        return cls.flyweight(wrapped, fill, dtype=dtype, _is_empty=is_empty)

    @classmethod
    def from_string(cls, wrapped: str, fill_value: str | Empty = EMPTY) -> DecoratorMeta:
        """TODO"""
        typ = resolve(wrapped)
        # TODO: parse fill_value
        return cls[typ, fill_value]

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



def foo(a: Int32["numpy"] | Int64["pandas"]):
    pass
