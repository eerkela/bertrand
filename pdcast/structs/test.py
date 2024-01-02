from __future__ import annotations
import inspect
from types import MappingProxyType
from typing import Any, Callable, Iterable, Iterator, Mapping, NoReturn

import numpy as np
import pandas as pd

from linked import *


POINTER_SIZE = np.dtype(np.intp).itemsize


####################
####    BASE    ####
####################


# TODO: need a set of edges to represent overrides for <, >, etc.


class Precedence:
    """A linked set representing the priority given to each decorator in the type
    system.

    This interface is used to determine the order in which decorators are nested when
    applied to the same type.  For instance, if the precedence is set to `{A, B, C}`,
    and each is applied to a type `T`, then the resulting type will always be
    `A[B[C[T]]]`, no matter what order the decorators are applied in.  To demonstrate:

    .. code-block:: python

        >>> Type.registry.decorator_precedence
        Precedence({A, B, C})
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
        Precedence({B, C, A})
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
        return f"Precedence({', '.join(repr(d) for d in self)})"


class TypeRegistry:
    """A registry containing the global state of the bertrand type system.
    
    This is automatically populated by the metaclass machinery, and is used by the
    detect() and resolve() helper functions to link aliases to their corresponding
    types.  It also provides a convenient way to interact with the type system as a
    whole, for instance to attach() or detach() it from pandas, or list all the
    types that are currently defined.
    """

    types: dict[type, TypeMeta] = {}
    dtypes: dict[type, TypeMeta] = {}
    strings: dict[str, TypeMeta] = {}
    decorator_precedence: Precedence = Precedence()

    @property
    def aliases(self) -> dict[str | type, TypeMeta]:
        """Unify the type, dtype, and string registries into a single dictionary.
        These are usually separated for performance reasons.
        """
        result = self.types | self.dtypes
        result.update(self.strings)
        return result


REGISTRY = TypeRegistry()


class Aliases:
    """A mutable proxy for a type's aliases, which automatically pushes all changes to
    the global registry.

    This interface allows the user to remap the output of the detect() and resolve()
    helper functions at runtime.  This makes it possible to swap types out on the fly,
    or add custom syntax to the type specification mini-language to support the
    language of your choice.
    """

    ALIAS = str | type | np.dtype[Any] | pd.api.extensions.ExtensionDtype
    DTYPE_LIKE = (np.dtype, pd.api.extensions.ExtensionDtype)

    def __init__(self, aliases: LinkedSet[str | type]):
        self.aliases = aliases
        self._parent: TypeMeta | None = None  # assigned after instantiation

    @property
    def parent(self) -> TypeMeta | None:
        """TODO"""
        return self._parent

    @parent.setter
    def parent(self, typ: TypeMeta) -> None:
        """TODO"""
        if self._parent is not None:
            raise TypeError("cannot reassign type")

        # push all aliases to the global registry
        for alias in self.aliases:
            if alias in REGISTRY.aliases:
                raise TypeError(f"aliases must be unique: {repr(alias)}")

            if isinstance(alias, str):
                REGISTRY.strings[alias] = typ
                continue

            if isinstance(alias, self.DTYPE_LIKE):
                REGISTRY.dtypes[type(alias)] = typ
                continue

            REGISTRY.types[alias] = typ

        self._parent = typ

    def add(self, alias: ALIAS) -> None:
        """Add an alias to a type, pushing it to the global registry."""
        if alias in REGISTRY.aliases:
            raise TypeError(f"aliases must be unique: {repr(alias)}")

        elif isinstance(alias, str):
            REGISTRY.strings[alias] = self.parent

        elif isinstance(alias, self.DTYPE_LIKE):
            REGISTRY.dtypes[type(alias)] = self.parent

        elif isinstance(alias, type):
            if issubclass(alias, self.DTYPE_LIKE):
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
            del REGISTRY.strings[alias]

        elif isinstance(alias, self.DTYPE_LIKE):
            del REGISTRY.dtypes[type(alias)]

        elif isinstance(alias, type):
            if issubclass(alias, self.DTYPE_LIKE):
                del REGISTRY.dtypes[alias]
            else:
                del REGISTRY.types[alias]

        else:
            raise TypeError(
                f"aliases must be strings, types, or dtypes: {repr(alias)}"
            )

    def __repr__(self) -> str:
        return f"Aliases({', '.join(repr(a) for a in self.aliases)})"


class TypeBuilder:
    """Common base for all namespace builders.

    The builders that inherit from this class are responsible for transforming a type's
    simplified namespace into a fully-featured bertrand type.  They do this by looping
    through all the attributes that are defined in the class and executing any relevant
    helper functions to validate their configuration.  The builders also mark all
    required fields (those assigned to Ellipsis), non-type dispatch methods, or
    reserved attributes that they encounter, and fill in missing slots with default
    values where possible.  If the type is improperly configured, the builder will
    throw a series of informative error messages guiding the user towards a solution.

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
            self.methods = {}
        else:
            self.required = self.parent._required.copy()
            self.fields = self.parent._fields.copy()
            self.methods = self.parent._methods.copy()

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
        self.namespace["_methods"] = self.methods
        self.namespace["_slug"] = self.name
        self.namespace["_hash"] = hash(self.name)
        if self.parent is object or self.parent is Type or self.parent is DecoratorType:
            self.namespace["_supertype"] = None
        else:
            self.namespace["_supertype"] = self.parent
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
            # TODO: automatically add scalar, dtype?
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


############################
####    SCALAR TYPES    ####
############################


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


def features(t: TypeMeta) -> tuple[Any, ...]:
    """Extract a tuple of features representing the allowable range of a type, used for
    range-based sorting and comparison.
    """
    return (
        t.max - t.min,  # total range
        t.itemsize,  # memory footprint
        t.is_nullable,  # nullability
        abs(t.max + t.min),  # bias away from zero
    )


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
    #   _required: dict[str, Callable[..., Any]]
    #   _fields: dict[str, Any]
    #   _methods: dict[str, Callable[..., Any]]
    #   _slug: str
    #   _hash: int
    #   _cache_size: int | None
    #   _flyweights: LinkedDict[str, TypeMeta]
    #   _parametrized: bool
    #   _params: tuple[str]
    #   _supertype: TypeMeta | None
    #   _subtypes: LinkedSet[TypeMeta]
    #   _implementations: LinkedDict[str, TypeMeta]
    #   _default: list[TypeMeta]  # TODO: modify directly rather than using a list
    #   _nullable: list[TypeMeta]

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
        prefix = "{\n    "
        sep = ",\n    "
        suffix = "\n}"
        format_dict = lambda d: ("{}" if not d else
            prefix +
            sep.join(f"{k}: {repr(v)}" for k, v in d.items()) +
            suffix
        )

        fields = {k: "..." for k in cls._required}
        fields |= cls._fields

        if not (len(bases) == 0 or bases[0] is object):
            print("-" * 80)
            print(f"slug: {cls.slug}")
            print(f"aliases: {cls.aliases}")
            print(f"fields: {format_dict(fields)}")
            print(f"methods: {format_dict(cls._methods)}")
            print(f"is_abstract: {cls.is_abstract}")
            print(f"is_concrete: {cls.is_concrete}")
            print(f"is_root: {cls.is_root}")
            print(f"is_leaf: {cls.is_leaf}")
            print(f"abstract: {cls.abstract}")
            print(f"supertype: {cls.supertype}")
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
        """TODO"""
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls._flyweights.lru_get(slug, None)

        if typ is None:
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
                    "_supertype": cls,
                    "__class_getitem__": __class_getitem__
                }
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
    def methods(cls) -> Mapping[str, Callable[..., Any]]:
        """TODO"""
        return MappingProxyType(cls._methods)

    @property
    def decorators(cls) -> UnionMeta:
        """TODO"""
        return Union.from_types(LinkedSet())  # always empty

    @property
    def wrapped(cls) -> None:
        """TODO"""
        return None  # base case for recursion

    @property
    def unwrapped(cls) -> TypeMeta:
        """TODO"""
        return cls  # for consistency with decorators

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
    def is_concrete(cls) -> bool:
        """TODO"""
        return not cls.is_abstract and not cls.is_parametrized

    @property
    def is_parametrized(cls) -> bool:
        """TODO"""
        return cls._parametrized

    @property
    def is_root(cls) -> bool:
        """TODO"""
        return cls.supertype is None

    @property
    def is_leaf(cls) -> bool:
        """TODO"""
        return not cls.subtypes and not cls.implementations

    @property
    def root(cls) -> TypeMeta:
        """TODO"""
        parent = cls
        while parent.supertype is not None:
            parent = parent.supertype
        return parent

    @property
    def supertype(cls) -> TypeMeta | None:
        """TODO"""
        return cls._supertype

    @property
    def abstract(cls) -> TypeMeta | None:
        """TODO"""
        if cls.is_abstract:
            return cls
        if cls.supertype is None:
            return None
        return cls.supertype.abstract

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
            "slug", "hash", "backend", "itemsize", "methods", "decorators", "wrapped",
            "unwrapped", "cache_size", "flyweights", "params", "is_abstract",
            "is_default", "is_concrete", "is_parametrized", "is_root", "is_leaf",
            "root", "supertype", "abstract", "subtypes", "implementations",
            "children", "leaves", "larger", "smaller"
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
        return isinstance(other, cls.scalar)

    def __subclasscheck__(cls, other: type) -> bool:
        return super().__subclasscheck__(other)

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return cls.__subclasscheck__(other)
        return cls.__instancecheck__(other)

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:  # pylint: disable=invalid-length-returned
        return cls.itemsize  # basically a Python-level sizeof() operator

    def __or__(cls, other: TypeMeta | Iterable[TypeMeta]) -> UnionMeta:  # type: ignore
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return Union[cls, other]

        result = LinkedSet[TypeMeta]([cls])
        result.update(other)
        return Union.from_types(result)

    def __ror__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:  # type: ignore
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return Union[other, cls]

        result = LinkedSet[TypeMeta]([cls])
        result.update(other)
        return Union.from_types(result)

    def __sub__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
        return Union[cls] - other

    def __and__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
        return Union[cls] & other

    def __xor__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
        return Union[cls] ^ other

    def __lt__(cls, other: TypeMeta) -> bool:
        return cls is not other and features(cls) < features(other)

    def __le__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) <= features(other)

    def __eq__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) == features(other)

    def __ne__(cls, other: Any) -> bool:
        return cls is not other and features(cls) != features(other)

    def __ge__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) >= features(other)

    def __gt__(cls, other: TypeMeta) -> bool:
        return cls is not other and features(cls) > features(other)

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

    def identity(
        self,
        value: Any,
        namespace: dict[str, Any],
        processed: dict[str, Any]
    ) -> Any:
        """Simple identity function used to validate required arguments that do not
        have any type hints.
        """
        if value is Ellipsis:
            raise TypeError("missing required field")
        return value

    def parse(self) -> AbstractBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        namespace = self.namespace.copy()

        for name, value in namespace.items():
            if value is Ellipsis:
                self.required[name] = self.annotations.pop(name, self.identity)
                del self.namespace[name]
            elif name in self.required:
                self.validate(name, self.namespace.pop(name))
            elif name in self.RESERVED and self.parent is not object:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )
            elif inspect.isfunction(value):
                self.methods[name] = self.namespace.pop(name)

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
                return cls._implementations[val]
            return cls._implementations[val[0]][*val[1:]]

        self.namespace["__class_getitem__"] = classmethod(default)
        self.namespace["_params"] = ()

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

        def default(cls: type, backend: str, *args: str) -> TypeMeta:
            """Forward all arguments to the specified implementation."""
            return cls._implementations[backend].from_string(*args)

        self.namespace["from_string"] = classmethod(default)


class ConcreteBuilder(TypeBuilder):
    """A builder-style parser for a concrete type's namespace (backend != None).

    Concrete types must implement all required fields, and may optionally define
    additional fields that are not required.  They can also specify any number of
    dispatch methods (any method or property that is not marked as a @classmethod),
    which will automatically be converted into single-dispatch functions that are
    attached directly to `pandas.Series` when bertrand.attach() is invoked.

    Concrete types can also be parametrized by specifying a __class_getitem__ method
    that accepts any number of positional arguments.  These arguments must have default
    values, and the signature will be used to populate the base type's `params` dict.
    Parametrized types are then instantiated using a flyweight caching strategy, which
    involves an LRU dict of a specific size attached to the type itself.
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
            elif inspect.isfunction(value) and name not in self.CONSTRUCTORS:
                self.methods[name] = self.namespace.pop(name)

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
            self.class_getitem_signature = sig

        else:
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["__class_getitem__"] = classmethod(default)
            self.class_getitem_signature = inspect.signature(default)

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

            sig = inspect.signature(method.__wrapped__)
            if sig != self.class_getitem_signature:
                raise TypeError(
                    f"the signature of from_string() must match __class_getitem__() "
                    f"exactly: {str(sig)} != {str(self.class_getitem_signature)}"
                )

        else:
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_string"] = classmethod(default)


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
    #   _required: dict[str, Callable[..., Any]]
    #   _fields: dict[str, Any]
    #   _methods: dict[str, Callable[..., Any]]
    #   _slug: str
    #   _hash: int
    #   _cache_size: int | None
    #   _flyweights: LinkedDict[str, DecoratorMeta]
    #   _parametrized: bool
    #   _params: tuple[str]

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
            # TODO

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
            typ = super().__new__(
                DecoratorMeta,
                cls.__name__,
                (cls,),
                {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_fields": cls._fields | dict(zip(cls._params, args)) | kwargs,
                }
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
    def methods(cls) -> Mapping[str, Callable[..., Any]]:
        """TODO"""
        return MappingProxyType(cls._methods)

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
    def wrapped(cls) -> None | TypeMeta | DecoratorMeta:
        """TODO"""
        return cls._fields["wrapped"]

    @property
    def unwrapped(cls) -> None | TypeMeta:
        """TODO"""
        result = cls.wrapped
        while result.wrapped is not None:
            result = result.wrapped
        return result

    @property
    def cache_size(cls) -> int:
        """TODO"""
        return cls._cache_size

    @property
    def flyweights(cls) -> Mapping[str, DecoratorMeta]:
        """TODO"""
        return MappingProxyType(cls._flyweights)

    @property
    def params(cls) -> Mapping[str, Any]:
        """TODO"""
        return MappingProxyType({k: cls._fields[k] for k in cls._params})

    @property
    def as_default(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.flyweight(cls.wrapped.as_default)  # NOTE: will recurse

    @property
    def as_nullable(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.flyweight(cls.wrapped.as_nullable)

    @property
    def root(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.flyweight(cls.wrapped.root)

    @property
    def supertype(cls) -> DecoratorMeta:
        """TODO"""
        if not cls.wrapped:
            return cls
        return cls.flyweight(cls.wrapped.supertype)

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.subtypes:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.implementations:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.children:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.leaves:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

    @property
    def larger(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.larger:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

    @property
    def smaller(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        if cls.wrapped:
            for typ in cls.wrapped.smaller:
                result.add(cls.flyweight(typ))
        return Union.from_types(result)

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
            "slug", "hash", "methods", "decorators", "wrapped", "unwrapped",
            "cache_size", "flyweights", "params", "as_default", "as_nullable",
            "root", "supertype", "subtypes", "implementations", "children", "leaves",
            "larger", "smaller"
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

    def __instancecheck__(cls, other: Any) -> bool:
        # TODO: should require exact match?  i.e. isinstance(1, Sparse[Int]) == False.
        # This starts getting complicated
        return isinstance(other, cls.wrapped)

    def __subclasscheck__(cls, other: type) -> bool:
        # TODO: same as instancecheck
        return super().__subclasscheck__(other)

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        return cls.transform(cls.wrapped(*args, **kwargs))

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        return cls.itemsize

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return issubclass(other, cls)
        return isinstance(other, cls)

    # TODO: all of these should be able to accept a decorator, type, or union

    def __or__(cls, other: TypeMeta | Iterable[TypeMeta]) -> Union:  # type: ignore
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return Union[cls, other]

        result = LinkedSet[TypeMeta](other)
        result.add_left(cls)
        return Union.from_types(result)

    def __ror__(cls, other: TypeMeta | Iterable[TypeMeta]) -> Union:  # type: ignore
        if isinstance(other, TypeMeta):
            return Union[other, cls]

        result = LinkedSet[TypeMeta](other)
        result.add(cls)
        return Union.from_types(result)

    def __lt__(cls, other: TypeMeta) -> bool:
        return cls is not other and features(cls) < features(other)

    def __le__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) <= features(other)

    def __eq__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) == features(other)

    def __ne__(cls, other: Any) -> bool:
        return cls is not other and features(cls) != features(other)

    def __ge__(cls, other: TypeMeta) -> bool:
        return cls is other or features(cls) >= features(other)

    def __gt__(cls, other: TypeMeta) -> bool:
        return cls is not other and features(cls) > features(other)

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
            elif inspect.isfunction(value) and name not in self.CONSTRUCTORS:
                self.methods[name] = self.namespace.pop(name)

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

        def insort(cls: DecoratorMeta, other: DecoratorMeta, *args: Any) -> DecoratorMeta:
            visited = []
            for dec in other.decorators:
                delta = REGISTRY.decorator_precedence.distance(cls, dec)  # TODO: dec won't be in the registry since it's parametrized
                if delta < 0:  # cls is lower priority than dec
                    pass

            return cls

        if "__class_getitem__" in self.namespace:
            original = self.namespace["__class_getitem__"]

            def wrapper(cls: type, val: Any | tuple[Any, ...]) -> DecoratorMeta | UnionMeta:
                """Unwrap tuples/unions and forward arguments to the wrapped method."""
                # NOTE: when inserting into the middle of the decorator stack, we need
                # to instantiate a whole new decorator stack, which refers to the same
                # trunk but with any prior decorators replaced.

                if isinstance(val, TypeMeta):
                    return original(cls, val)

                if isinstance(val, DecoratorMeta):
                    visited = []
                    for dec in val.decorators:
                        delta = REGISTRY.decorator_precedence.distance(cls, dec)
                        if delta < 0:  # cls is lower priority than dec
                            visited.append(dec)
                            continue
                        elif delta == 0:  # cls is identical to dec
                            raise NotImplementedError()
                        else:  # cls is higher priority than dec
                            raise NotImplementedError()

                    return original(cls, val)


                if isinstance(val, UnionMeta):
                    # TODO: insert into decorator stack
                    return Union.from_types(LinkedSet(original(cls, t) for t in val))

                if isinstance(val, tuple):
                    wrapped = val[0]
                    args = val[1:]
                    # TODO: apply same logic as above for inserting into a decorator stack
                    if isinstance(wrapped, TypeMeta):
                        return original(cls, wrapped, *args)
                    if isinstance(wrapped, DecoratorMeta):
                        visited = []
                        for dec in wrapped.decorators:
                            delta = REGISTRY.decorator_precedence.distance(cls, dec)
                            if delta < 0:
                                visited.append(dec)
                                continue
                            elif delta == 0:
                                raise NotImplementedError()
                            else:
                                raise NotImplementedError()


                    if isinstance(wrapped, UnionMeta):
                        # TODO: insert into decorator stack
                        return Union.from_types(
                            LinkedSet(original(cls, t, *args) for t in wrapped)
                        )

                raise TypeError(
                    f"Decorators must accept another bertrand type as a first "
                    f"argument, not {repr(val)}"
                )

            self.namespace["__class_getitem__"] = classmethod(wrapper)

            skip = True
            sig = inspect.signature(original)
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
            self.class_getitem_signature = sig

        else:
            def default(
                cls: type,
                wrapped: TypeMeta | DecoratorMeta | UnionMeta | None = None
            ) -> DecoratorMeta | UnionMeta:
                """Default to identity function."""
                if isinstance(wrapped, TypeMeta):
                    return cls.flyweight(wrapped)

                if isinstance(wrapped, DecoratorMeta):
                    # TODO: insert into decorator stack
                    raise NotImplementedError()

                if isinstance(wrapped, UnionMeta):
                    # TODO: insert into decorator stack
                    return Union.from_types(LinkedSet(cls.flyweight(t) for t in wrapped))

                raise TypeError(
                    f"Decorators must accept another bertrand type as a first "
                    f"argument, not {repr(wrapped)}"
                )

            parameters["wrapped"] = None
            self.fields["wrapped"] = None
            self.namespace["__class_getitem__"] = classmethod(default)
            self.class_getitem_signature = inspect.signature(default)

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

            sig = inspect.signature(method.__wrapped__)
            if sig != self.class_getitem_signature:
                raise TypeError(
                    f"the signature of from_string() must match __class_getitem__() "
                    f"exactly: {str(sig)} != {str(self.class_getitem_signature)}"
                )

        else:
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_string"] = classmethod(default)

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

        else:
            def default(cls: type, series: pd.Series[Any]) -> pd.Series[Any]:
                """Default to identity function."""
                return series

            self.namespace["transform"] = classmethod(default)

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

        else:
            def default(cls: type, series: pd.Series[Any]) -> pd.Series[Any]:
                """Default to identity function."""
                return series

            self.namespace["inverse_transform"] = classmethod(default)


###########################
####    UNION TYPES    ####
###########################


def union_getitem(cls: UnionMeta, key: int | slice) -> TypeMeta | DecoratorMeta | UnionMeta:
    """A parametrized replacement for the base Union's __class_getitem__() method that
    allows for integer-based indexing/slicing of a union's types.
    """
    if isinstance(key, slice):
        return cls.from_types(cls._types[key])
    return cls._types[key]


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
            "_hash": 42
        })

    def from_types(cls, types: LinkedSet[TypeMeta]) -> UnionMeta:
        """TODO"""
        hash_val = 42  # to avoid collisions at hash=0
        for t in types:
            hash_val = (hash_val * 31 + hash(t)) % (2**63 - 1)  # mod not necessary in C++

        return super().__new__(UnionMeta, cls.__name__, (cls,), {
            "_types": types,
            "_hash": hash_val,
            "__class_getitem__": union_getitem
        })

    ################################
    ####    UNION ATTRIBUTES    ####
    ################################

    @property
    def index(cls) -> NoReturn:
        """A 1D numpy array containing the observed type at every index of an iterable
        passed to `bertrand.detect()`.  This is stored internally as a run-length
        encoded array of flyweights for memory efficiency, and is expanded into a
        full array when accessed.
        """
        raise NotImplementedError()

    def collapse(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            if not any(t is not other and t in other for other in cls._types):
                result.add(t)
        return cls.from_types(result)

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    # TODO: wrapped, unwrapped, decorators, etc.

    @property
    def as_default(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.as_default for t in cls._types))

    @property
    def as_nullable(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.as_nullable for t in cls._types))

    @property
    def root(cls) -> UnionMeta:
        """TODO"""
        return cls.from_types(LinkedSet(t.root for t in cls._types))

    @property
    def supertype(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            result.add(t.supertype if t.supertype else t)
        return cls.from_types(result)

    @property
    def subtypes(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            result.update(t.subtypes)
        return cls.from_types(result)

    @property
    def implementations(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            result.update(t.implementations)
        return cls.from_types(result)

    @property
    def children(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            result.update(t.children)
        return cls.from_types(result)

    @property
    def leaves(cls) -> UnionMeta:
        """TODO"""
        result = LinkedSet()
        for t in cls._types:
            result.update(t.leaves)
        return cls.from_types(result)

    def sorted(
        cls,
        *,
        key: Callable[[TypeMeta], Any] = None,
        reverse: bool = False
    ) -> UnionMeta:
        """TODO"""
        result = cls._types.copy()
        result.sort(key=key, reverse=reverse)
        return cls.from_types(result)

    def __getattr__(cls, name: str) -> LinkedDict[TypeMeta, Any]:
        types = super().__getattribute__("_types")
        return LinkedDict((t, getattr(t, name)) for t in types)

    def __setattr__(cls, key: str, val: Any) -> NoReturn:
        raise TypeError("unions are immutable")

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        """TODO"""
        for t in cls._types:
            try:
                return t(*args, **kwargs)
            except Exception:
                continue

        raise TypeError(f"cannot convert to union type: {repr(cls)}")

    def __instancecheck__(cls, other: Any) -> bool:
        return any(isinstance(other, t) for t in cls._types)

    def __subclasscheck__(cls, other: type) -> bool:
        return any(issubclass(other, t) for t in cls._types)

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return cls.__subclasscheck__(other)
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

    def __or__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:  # type: ignore
        if isinstance(other, UnionMeta):
            result = cls._types | other._types
        elif isinstance(other, (TypeMeta, DecoratorMeta)):
            result = cls._types | (other,)
        else:
            result = cls._types | other
        return cls.from_types(result)

    def __and__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
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

    def __sub__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
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

    def __xor__(cls, other: Meta | Iterable[Meta]) -> UnionMeta:
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

    def __lt__(cls, other: Meta | Iterable[Meta]) -> bool:
        if isinstance(other, (TypeMeta, DecoratorMeta)):
            other = cls.from_types(LinkedSet([other]))
        elif not isinstance(other, UnionMeta):
            return NotImplemented

        return all(t in other for t in cls._types) and len(cls) < len(other)

    def __le__(cls, other: Meta | Iterable[Meta]) -> bool:
        if other is cls:
            return True

        if isinstance(other, (TypeMeta, DecoratorMeta)):
            other = cls.from_types(LinkedSet([other]))
        elif not isinstance(other, UnionMeta):
            return NotImplemented

        return all(t in other for t in cls._types) and len(cls) <= len(other)

    def __eq__(cls, other: Meta | Iterable[Meta]) -> bool:
        if isinstance(other, UnionMeta):
            return (
                cls is other or
                len(cls) == len(other) and
                all(t is u for t, u in zip(cls, other))
            )

        if isinstance(other, (TypeMeta, DecoratorMeta)):
            return len(cls) == 1 and cls._types[0] is other

        return NotImplemented

    def __ne__(cls, other: Meta | Iterable[Meta]) -> bool:
        return not cls.__eq__(other)

    def __ge__(cls, other: Meta | Iterable[Meta]) -> bool:
        if not isinstance(other, (TypeMeta, UnionMeta)):
            other = cls.from_types(other)
        return cls.children._types >= other.children._types

    def __gt__(cls, other: Meta | Iterable[Meta]) -> bool:
        if not isinstance(other, (TypeMeta, UnionMeta)):
            other = cls.from_types(other)
        return cls.children._types > other.children._types

    def __str__(cls) -> str:
        return f"{' | '.join(t.slug for t in cls._types)}"

    def __repr__(cls) -> str:
        return f"Union[{', '.join(t.slug for t in cls._types)}]"


##########################
####    BASE TYPES    ####
##########################


# TODO: Type/DecoratorType should go in a separate bases.py file so that they can be
# hosted verbatim in the docs.
# -> metaclasses/builders go in meta.py or meta.h + meta.cpp
# -> meta.py, type.py, decorator.py, union.py


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
        raise TypeError("bertrand types cannot be instantiated")

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

    # NOTE: Because bertrand types cannot be instantiated and their call operators map
    # to Series constructors, any method that is not marked with @classmethod will
    # automatically be converted into an equivalent dispatch function.  In this case,
    # `self` will always be a Series object containing elements of this type, exactly
    # as produced by calling the type directly.  The dispatch function will be chosen
    # whenever the named method is called on a series of this type.

    def round(self: Type, decimals: int = 0, *args: Any, **kwargs: Any) -> Type:
        """Example implementation showing how to attach a method to Series objects of
        this type.  This will be implicitly converted into a dispatch function of the
        following form:

        @virtual(pd.Series)
        @dispatch
        def round(series: Type, *args, **kwargs) -> Type:
            return series.round.original(self, *args, **kwargs)

        @round.overload
        def round(series: ThisType, decimals: int = 0, *args, **kwargs) -> ThisType:
            print("hello, world!")
            return series + decimals

        The conversion is performed automatically by the metaclass, and the overridden
        `round()` method is stored under the `original` attribute of the dispatch
        function for transparent access.  A trivial base implementation is provided for
        convenience, which simply redirects to the original method if no matching type
        is found.

        Note that the @virtual decorator is only executed once `bertrand.attach()` is
        invoked.  This is done to avoid muddying the pandas namespace with bertrand
        functions if the user decides not to use them.
        """
        print("hello, world!")
        return self + decimals

    # TODO: allow users to overload math operators for series objects.  They just can't
    # overload __init__, __new__, or any classmethod versions of the reserved slots.


class DecoratorType(object, metaclass=DecoratorMeta):
    """TODO
    """

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Apply the decorator to a series of the wrapped type."""
        return series

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Remove the decorator from a series of the wrapped type."""
        return series


class Union(metaclass=UnionMeta):
    """TODO
    """

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand unions cannot be instantiated")

    def __class_getitem__(cls, val: TypeMeta | tuple[TypeMeta, ...]) -> UnionMeta:
        if isinstance(val, tuple):
            return cls.from_types(LinkedSet(val))
        return cls.from_types(LinkedSet((val,)))








class Sparse(DecoratorType):
    aliases = {"sparse"}







class Int(Type):
    aliases = {"int", "integer"}


@Int.default
class Signed(Int):
    aliases = {"signed"}




@Signed.default
class Int64(Signed):
    aliases = {"int64", "long long"}
    max = 2**63 - 1
    min = -2**63


@Int64.default
class NumpyInt64(Int64, backend="numpy"):
    dtype = np.dtype(np.int64)
    is_nullable = False


@NumpyInt64.nullable
class PandasInt64(Int64, backend="pandas"):
    dtype = pd.Int64Dtype()
    is_nullable = True




class Int32(Signed):
    aliases = {"int32", "long"}
    max = 2**31 - 1
    min = -2**31


@Int32.default
class NumpyInt32(Int32, backend="numpy"):
    dtype = np.dtype(np.int32)
    is_nullable = False


@NumpyInt32.nullable
class PandasInt32(Int32, backend="pandas"):
    dtype = pd.Int32Dtype()
    is_nullable = True




class Int16(Signed):
    aliases = {"int16", "short"}
    max = 2**15 - 1
    min = -2**15


@Int16.default
class NumpyInt16(Int16, backend="numpy"):
    dtype = np.dtype(np.int16)
    is_nullable = False


@NumpyInt16.nullable
class PandasInt16(Int16, backend="pandas"):
    dtype = pd.Int16Dtype()
    is_nullable = True




class Int8(Signed):
    aliases = {"int8", "char"}
    max = 2**7 - 1
    min = -2**7


@Int8.default
class NumpyInt8(Int8, backend="numpy", cache_size=2):
    dtype = np.dtype(np.int8)
    is_nullable = False

    def __class_getitem__(cls, arg: Any = None) -> TypeMeta:  # type: ignore
        return cls.flyweight(arg, dtype=np.dtype(arg))


@NumpyInt8.nullable
class PandasInt8(Int8, backend="pandas"):
    dtype = pd.Int8Dtype()
    is_nullable = True




print("=" * 80)
print(f"aliases: {REGISTRY.aliases}")
print()
