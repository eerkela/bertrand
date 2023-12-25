from __future__ import annotations
import inspect
from typing import Any, Callable, Iterable, Iterator, NoReturn

import numpy as np
import pandas as pd

from linked import *


POINTER_SIZE = np.dtype(np.intp).itemsize


###########################
####    METACLASSES    ####
###########################


def explode_children(t: TypeMeta, result: LinkedSet[TypeMeta]) -> None:  # lol
    """Explode a type's hierarchy into a flat list of subtypes and implementations."""
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


class TypeMeta(type):
    """Metaclass for all scalar bertrand types (those that inherit from bertrand.Type).

    This metaclass is responsible for parsing the namespace of a new type, validating
    its configuration, and registering it in the global type registry.  It also defines
    the basic behavior of all bertrand types, including the establishment of
    hierarchical relationships, parametrization, and operator overloading.

    See the documentation for the `Type` class for more information on how these work.
    """

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
        if not (len(bases) == 0 or bases[0] is object):
            print("-" * 80)
            print(f"name: {cls.__name__}")
            print(f"slug: {cls.slug}")
            print(f"aliases: {cls.aliases}")
            print(f"cache_size: {cls.cache_size}")
            print(f"supertype: {cls.supertype}")
            print(f"dtype: {getattr(cls, 'dtype', '...')}")
            print(f"scalar: {getattr(cls, 'scalar', '...')}")
            print(f"itemsize: {getattr(cls, 'itemsize', '...')}")
            print(f"max: {getattr(cls, 'max', '...')}")
            print(f"min: {getattr(cls, 'min', '...')}")
            print(f"is_nullable: {getattr(cls, 'is_nullable', '...')}")
            print(f"missing: {getattr(cls, 'missing', '...')}")
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
            super().__new__(mcs, build.name, build.bases, build.namespace)
        )

    def flyweight(cls, *args: Any, **kwargs: Any) -> TypeMeta:
        """TODO"""
        slug = f"{cls.__name__}[{', '.join(repr(a) for a in args)}]"
        typ = cls.flyweights.get(slug, None)

        if typ is None:
            def __class_getitem__(cls: type, *args: Any) -> NoReturn:
                raise TypeError(f"{slug} cannot be re-parametrized")

            typ = super().__new__(
                TypeMeta,
                cls.__name__,
                (cls,),
                cls.params | {"_" + k: v for k, v in kwargs.items()} | {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "__class_getitem__": __class_getitem__
                }
            )
            cls.flyweights[slug] = typ

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

    @property
    def is_default(cls) -> bool:
        """Indicates whether this type redirects to a default implementation."""
        return not cls._default

    @property
    def as_default(cls) -> TypeMeta:
        """Convert an abstract type to its default implementation, if it has one."""
        return cls if cls.is_default else cls._default[0]  # NOTE: easier at C++ level

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
        cls._nullable.append(implementation)  # NOTE: easier at C++ level
        return implementation

    @property
    def as_nullable(cls) -> TypeMeta:
        """Convert a concrete type to its nullable implementation, if it has one."""
        return cls if not cls.is_nullable else cls._nullable[0]

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
        return cls._backend

    @property
    def itemsize(cls) -> int:
        """TODO"""
        return cls.dtype.itemsize

    @property
    def cache_size(cls) -> int:
        """TODO"""
        return cls._cache_size

    @property
    def flyweights(cls) -> LinkedDict[str, TypeMeta]:
        """TODO"""
        return cls._flyweights  # TODO: make this read-only, but automatically LRU update

    @property
    def params(cls) -> LinkedDict[str, Any]:
        """TODO"""
        return {k: cls._fields[k] for k in cls._params}

    @property
    def parametrized(cls) -> bool:
        """TODO"""
        return cls._parametrized

    @property
    def is_root(cls) -> bool:
        """TODO"""
        return cls.supertype is None

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
    def is_abstract(cls) -> bool:
        """TODO"""
        return not cls._backend

    @property
    def abstract(cls) -> TypeMeta | None:
        """TODO"""
        if cls.is_abstract:
            return cls
        if cls.supertype is None:
            return None
        return cls.supertype.abstract

    @property
    def subtypes(cls) -> Union:
        """TODO"""
        return Union.from_set(cls._subtypes)  # TODO: make this immutable

    @property
    def implementations(cls) -> Union:
        """TODO"""
        return Union(*cls._implementations.values())

    @property
    def children(cls) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        explode_children(cls, result)
        return Union.from_set(result[1:])

    @property
    def leaves(cls) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        explode_leaves(cls, result)
        return Union.from_set(result)

    @property
    def is_leaf(cls) -> bool:
        """TODO"""
        return not cls.subtypes and not cls.implementations

    @property
    def larger(cls) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta](sorted(t for t in cls.root.leaves if t > cls))
        return Union.from_set(result)

    @property
    def smaller(cls) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta](sorted(t for t in cls.root.leaves if t < cls))
        return Union.from_set(result)

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

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __instancecheck__(cls, other: Any) -> bool:
        return isinstance(other, cls.scalar)

    def __subclasscheck__(cls, other: type) -> bool:
        return super().__subclasscheck__(other)

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls.is_default:
            return pd.Series(*args, dtype=cls.dtype, **kwargs)  # type: ignore
        return cls.as_default(*args, **kwargs)

    def __hash__(cls) -> int:
        return cls._hash

    def __len__(cls) -> int:
        return len(cls.leaves)

    def __iter__(cls) -> Iterator[TypeMeta]:
        return iter(cls.leaves)

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return issubclass(other, cls)
        return isinstance(other, cls)

    def __or__(cls, other: TypeMeta | Iterable[TypeMeta]) -> Union:  # type: ignore
        if isinstance(other, TypeMeta):
            return Union(cls, other)

        result = LinkedSet[TypeMeta](other)
        result.add_left(cls)
        return Union.from_set(result)

    def __ror__(cls, other: TypeMeta | Iterable[TypeMeta]) -> Union:  # type: ignore
        if isinstance(other, TypeMeta):
            return Union(other, cls)

        result = LinkedSet[TypeMeta](other)
        result.add(cls)
        return Union.from_set(result)

    def __lt__(cls, other: TypeMeta) -> bool:
        features = (
            cls.max - cls.min,  # total range
            cls.itemsize,  # memory footprint
            cls.is_nullable,  # nullability
            abs(cls.max + cls.min)  # bias away from zero
        )
        compare = (
            other.max - other.min,
            other.itemsize,
            other.is_nullable,
            abs(other.max + other.min)
        )
        return features < compare

    def __le__(cls, other: TypeMeta) -> bool:
        return cls == other or cls < other

    def __eq__(cls, other: TypeMeta) -> bool:
        return cls is other

    def __ne__(cls, other: TypeMeta) -> bool:
        return cls is not other

    def __ge__(cls, other: TypeMeta) -> bool:
        return cls == other or cls > other

    def __gt__(cls, other: TypeMeta) -> bool:
        features = (
            cls.max - cls.min,
            cls.itemsize,
            cls.is_nullable,
            abs(cls.max + cls.min)
        )
        compare = (
            other.max - other.min,
            other.itemsize,
            other.is_nullable,
            abs(other.max + other.min)
        )
        return features > compare

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


# TODO: create separate metaclass for decorator types, then implement CompositeType
# as a concrete class.

# decorator meta adds
# .decorators: list[DecoratorMeta]
# .wrapper: DecoratorMeta
# .wrapped: TypeMeta | DecoratorMeta
# .naked: TypeMeta
# .transform  <- applies the decorator to a series of the wrapped type.
# .inverse_transform  <- removes the decorator from a series of the wrapped type.


# TODO: if Sparse[] gets a UnionType, then we should broadcast over the union.


#############################
####    TYPE BUILDERS    ####
#############################


def get_from_calling_context(name: str) -> Any:
    """Get an object from the calling context given its fully-qualified (dotted) name.
    This is useful for extracting named objects from type hints when
    `from __future__ import annotations` is enabled, which converts all type hints to
    strings, or for parsing strings in the type specification mini-language.
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


class TypeBuilder:
    """Base class for all namespace analyzers.  These are executed during
    inheritance, just before instantiating a new type.  They thus have full
    access to the inheriting class's namespace, and can parse it however they
    see fit.
    """

    RESERVED_SLOTS: set[str] = set(dir(TypeMeta)) ^ {
        "__init__",
        "__module__",
        "__qualname__",
        "__annotations__",
    }
    CONSTRUCTORS: set[str] = {
        "__class_getitem__",
        "from_scalar",
        "from_dtype",
        "from_string"
    }

    def __init__(self, name: str, bases: tuple[TypeMeta], namespace: dict[str, Any]):
        nbases = len(bases)
        if nbases != 1:
            raise TypeError("bertrand types must inherit from a single bertrand type")

        self.parent = bases[0]
        self.name = name
        self.bases = bases
        self.namespace = namespace
        self.annotations = namespace.get("__annotations__", {})
        self.required = {} if self.parent is object else self.parent._required.copy()
        self.fields = {}
        self.methods = {}

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
        if self.parent is object or self.parent is Type:
            self.namespace["_supertype"] = None
        else:
            self.namespace["_supertype"] = self.parent
        self.namespace["_subtypes"] = LinkedSet[TypeMeta]()
        self.namespace["_implementations"] = LinkedDict[str:TypeMeta]()
        self.namespace["_parametrized"] = False
        self.namespace["_default"] = []
        self.namespace["_nullable"] = []
        # self.namespace["_backend"]  # <- handled in subclasses
        # self.namespace["_cache_size"]
        # self.namespace["_flyweights"]
        # self.namespace["__class_getitem__"]
        # self.namespace["from_scalar"]
        # self.namespace["from_dtype"]
        # self.namespace["from_string"]

        # static fields (disables redirects)
        self.namespace["aliases"] = self.fields.get("aliases", LinkedSet[str | type]())

        # default fields
        self.namespace.setdefault("__annotations__", {}).update(self.annotations)
        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """Push a newly-created type into the global registry, registering any aliases
        provided in its namespace.
        """
        for alias in self.namespace["aliases"]:
            Type.registry.aliases[alias] = typ

        return typ


class AbstractBuilder(TypeBuilder):
    """A strategy for analyzing abstract (backend = None) namespaces prior to
    instantiating a new type.
    """

    RESERVED_SLOTS = TypeBuilder.RESERVED_SLOTS | {
        "__class_getitem__",
        "from_scalar",
        "from_dtype"
        "from_string"
    }

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
            elif name in self.RESERVED_SLOTS and self.parent is not object:
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
        self.namespace["_backend"] = ""
        self.namespace["_cache_size"] = None
        self.namespace["_flyweights"] = LinkedDict[str:TypeMeta]()

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
    """A strategy for analyzing concrete (backend != None) namespaces prior to
    instantiating a new type.
    """

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str,
        cache_size: int | None,
    ):
        super().__init__(name, bases, namespace)

        if not isinstance(backend, str):
            raise TypeError(f"backend id must be a string, not {repr(backend)}")

        if self.parent is not object and self.parent is not Type:
            if not self.parent._backend:
                if backend in self.parent._implementations:
                    raise TypeError(f"backend id must be unique: {repr(backend)}")
            elif backend != self.parent._backend:
                raise TypeError(f"backend id must match its parent: {repr(backend)}")

        self.backend = backend
        self.cache_size = cache_size

    def parse(self) -> ConcreteBuilder:
        """Analyze the namespace and execute any relevant helper functions to validate
        its configuration.
        """
        for name, value in self.namespace.copy().items():
            if value is Ellipsis:
                raise TypeError(
                    f"concrete types must not have required fields: {self.name}.{name}"
                )
            elif name in self.required:
                self.validate(name, self.namespace.pop(name))
            elif name in self.RESERVED_SLOTS:
                raise TypeError(
                    f"type must not implement reserved attribute: {repr(name)}"
                )
            elif inspect.isfunction(value):
                self.methods[name] = self.namespace.pop(name)

        for name in list(self.required):
            self.validate(name, Ellipsis)

        return self

    def fill(self) -> ConcreteBuilder:
        """Fill in any missing slots in the namespace with default values, and evaluate
        any derived attributes.
        """
        super().fill()
        self.namespace["_backend"] = self.backend
        self.namespace["_cache_size"] = self.cache_size
        self.namespace["_flyweights"] = LinkedDict[str:TypeMeta](max_size=self.cache_size)

        self.class_getitem()
        self.from_scalar()
        self.from_dtype()
        self.from_string()

        return self

    def register(self, typ: TypeMeta) -> TypeMeta:
        """TODO"""
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
            wrapped = self.namespace["__class_getitem__"]

            def wrapper(cls: type, val: Any | tuple[Any, ...]) -> TypeMeta:
                """Unwrap tuples and forward all arguments to the wrapped function."""
                if isinstance(val, tuple):
                    return wrapped(cls, *val)
                return wrapped(cls, val)

            self.namespace["__class_getitem__"] = classmethod(wrapper)

            skip = True
            sig = inspect.signature(wrapped)
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

            self.features.update((k, v) for k, v in parameters.items())

        else:
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["__class_getitem__"] = classmethod(default)

        self.namespace["_params"] = tuple(parameters.keys())

    def from_scalar(self) -> None:
        """Parse a type's from_scalar() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_scalar" in self.namespace:
            sig = inspect.signature(self.namespace["from_scalar"])
            params = list(sig.parameters.values())
            if len(params) != 1 or not (
                params[0].kind == params[0].POSITIONAL_ONLY or
                params[0].kind == params[0].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_scalar() must accept a single positional argument"
                )

        else:
            def default(cls: type, scalar: Any) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            # TODO: in future, optimize this away entirely using a special bit flag
            # during detect() loop

            self.namespace["from_scalar"] = classmethod(default)

    def from_dtype(self) -> None:
        """Parse a type's from_dtype() method (if it has one), ensuring that it is
        callable with a single positional argument.
        """
        if "from_dtype" in self.namespace:
            sig = inspect.signature(self.namespace["from_dtype"])
            params = list(sig.parameters.values())
            if len(params) != 1 or not (
                params[0].kind == params[0].POSITIONAL_ONLY or
                params[0].kind == params[0].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_dtype() must accept a single positional argument"
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
            sig = inspect.signature(self.namespace["from_string"])
            observed = list(sig.parameters.values())
            expected = self.namespace["_params"]
            n = len(expected)
            if len(observed) == n:
                err_cond = False
                for i in range(n):
                    obs = observed[i]
                    exp = expected[i]
                    if obs.name != exp.name or obs.kind != exp.kind:
                        err_cond = True
                        break
            else:
                err_cond = True

            if err_cond:
                raise TypeError(
                    "the signature of from_string() must match __class_getitem__(), "
                    "ignoring default values"
                )

        else:
            def default(cls: type) -> TypeMeta:
                """Default to identity function."""
                return cls  # type: ignore

            self.namespace["from_string"] = classmethod(default)


class DecoratorBuilder(TypeBuilder):
    """A strategy for analyzing decorator namespaces (subclasses of TypeDecorator)
    prior to instantiating a new type.
    """
    pass


##########################
####    BASE TYPES    ####
##########################


def check_aliases(
    value: Any,
    namespace: dict[str, Any],
    processed: dict[str, Any],
) -> LinkedSet[str | type]:
    """Validate any aliases provided in a type's namespace, ensuring that they are
    unique strings and/or python-level type objects.
    """
    result = LinkedSet[str | type]()
    for alias in value:
        if alias in Type.registry.aliases:
            raise TypeError(f"aliases must be unique: {repr(alias)}")
        result.add(alias)

    return result


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

    class registry:
        aliases: dict[str, TypeMeta] = {}

    aliases: check_aliases = ...  # TODO: required by every type (not inherited)
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
        method will not be inherited by any subclasses, but can be overridden to
        provide support for parametrization using a flyweight cache.

        Positional arguments to the flyweight() helper function should be supplied in
        the same order as they appear in the signature, and keywords are piped directly
        into the resulting class's namespace, overriding any existing values.  In the
        interest of speed, no checks are performed on the arguments, so it is up to the
        user to ensure that they are valid.
        """
        return cls.flyweight(x, y, dtype=np.dtype("i4"), max=42)

    @classmethod
    def from_scalar(cls, scalar: Any) -> TypeMeta:
        """Example implementation showing how to parse a scalar value into a
        parametrized type.  This method will not be inherited by any subclasses, and
        will be called automatically at every iteration of the detect() loop.  If left
        blank, it will default to the identity function, and will be optimized away
        during type detection.
        """
        foo = scalar.foo
        bar = scalar.do_stuff(1, 2, 3)
        return cls.flyweight(foo, bar, dtype=pd.DatetimeTZDtype(tz="UTC"))

    @classmethod
    def from_dtype(cls, dtype: Any) -> TypeMeta:
        """Example implementation showing how to parse a numpy/pandas dtype into a
        parametrized type.  This method will not be inherited by any subclasses, and
        will be called automatically whenever detect() encounters array-like data
        labeled with this type.  If left blank, it will default to the identity
        function, and will be optimized away during type detection.
        """
        unit = dtype.unit
        step_size = dtype.step_size
        return cls.flyweight(unit, step_size, dtype=dtype)

    @classmethod
    def from_string(cls, spam: str, eggs: str) -> TypeMeta:
        """Example implementation showing how to parse a string in the
        type-specification mini-language into a parametrized type.  This method will
        not be inherited by any subclasses, and will be called automatically whenever
        resolve() encounters a type identifier that matches one of this type's aliases.
        The arguments are the comma-separated tokens parsed from the type's argument
        list.  They are always provided as strings with no further processing other
        than stripping leading/trailing whitespace.  It is up to the user to parse
        them into the appropriate values.

        If left blank, this method will default to a zero-argument implementation,
        which effectively disables argument lists and can be optimized away during type
        resolution.
        """
        return cls.flyweight(int(spam), pd.Timestamp(eggs))


# class Foo(Type):
#     pass


# @Foo.default
# class Bar(Foo, backend="bar"):
#     aliases = {"x", "y", int}
#     dtype = np.dtype("i4")

#     def foo(self):
#         pass


###########################
####    UNION TYPES    ####
###########################


# class Union:

#     def __init__(self, *args: TypeMeta) -> None:
#         self.types = LinkedSet[TypeMeta](args)

#     @classmethod
#     def from_set(cls, types: LinkedSet[TypeMeta]) -> Union:
#         """TODO"""
#         result = cls.__new__(cls)
#         result.types = types
#         return result

#     @property
#     def index(self) -> NoReturn:
#         """TODO"""
#         raise NotImplementedError()

#     @property
#     def root(self) -> Union:
#         """TODO"""
#         return self.from_set(LinkedSet[TypeMeta](t.root for t in self.types))

#     @property
#     def supertype(self) -> Union:
#         """TODO"""
#         return self.from_set(LinkedSet[TypeMeta](t.supertype for t in self.types))

#     @property
#     def subtypes(self) -> Union:
#         """TODO"""
#         result = LinkedSet[TypeMeta]()
#         for typ in self.types:
#             result.update(typ.subtypes)
#         return self.from_set(result)

#     @property
#     def abstract(self) -> Union:
#         """TODO"""
#         return self.from_set(LinkedSet[TypeMeta](t.abstract for t in self.types))

#     @property
#     def implementations(self) -> Union:
#         """TODO"""
#         result = LinkedSet[TypeMeta]()
#         for typ in self.types:
#             result.update(typ.implementations)
#         return self.from_set(result)

#     @property
#     def children(self) -> Union:
#         """TODO"""
#         result = LinkedSet[TypeMeta]()
#         for typ in self.types:
#             result.update(typ.children)
#         return self.from_set(result)

#     @property
#     def leaves(self) -> Union:
#         """TODO"""
#         result = LinkedSet[TypeMeta]()
#         for typ in self.types:
#             result.update(typ.leaves)
#         return self.from_set(result)

#     def sorted(
#         self,
#         *,
#         key: Callable[[TypeMeta], Any] | None = None,
#         reverse: bool = False
#     ) -> Union:
#         """TODO"""
#         result = self.types.copy()
#         result.sort(key=key, reverse=reverse)
#         return self.from_set(result)

#     def __getattr__(self, name: str) -> LinkedDict[TypeMeta, Any]:
#         result = LinkedDict[TypeMeta, Any]()
#         for typ in self.types:
#             result[typ] = getattr(typ, name)
#         return result

#     def __getitem__(self, key: TypeMeta | slice) -> Union:
#         if isinstance(key, slice):
#             return self.from_set(self.types[key])
#         return self.types[key]

#     def __call__(self, *args: Any, **kwargs: Any) -> pd.Series[Any]:
#         for typ in self.types:
#             try:
#                 return typ(*args, **kwargs)
#             except Exception:
#                 continue
#         raise TypeError(f"cannot convert to union type: {self}")

#     def __contains__(self, other: type | Any) -> bool:
#         if isinstance(other, type):
#             return any(issubclass(other, typ) for typ in self.types)
#         return any(isinstance(other, typ) for typ in self.types)

#     def __len__(self) -> int:
#         return len(self.types)

#     def __iter__(self) -> Iterator[TypeMeta]:
#         return iter(self.types)

#     def __reversed__(self) -> Iterator[TypeMeta]:
#         return reversed(self.types)

#     def __or__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
#         return self.from_set(self.types | other)

#     def __sub__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
#         return self.from_set(self.types - other)

#     def __and__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
#         return self.from_set(self.types & other)

#     def __xor__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
#         return self.from_set(self.types ^ other)

#     # TODO: this has to do hierarchical checking, not just set comparison

#     def __lt__(self, other: Union) -> bool:
#         return self.types < other.types

#     def __le__(self, other: Union) -> bool:
#         return self.types <= other.types

#     def __eq__(self, other: Union) -> bool:
#         return self.types == other.types

#     def __ne__(self, other: Union) -> bool:
#         return self.types != other.types

#     def __ge__(self, other: Union) -> bool:
#         return self.types >= other.types

#     def __gt__(self, other: Union) -> bool:
#         return self.types > other.types

#     def __str__(self) -> str:
#         return f"{{{', '.join(str(t) for t in self.types)}}}"

#     def __repr__(self) -> str:
#         return f"Union({', '.join(repr(t) for t in self.types)})"











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
class NumpyInt8(Int8, backend="numpy"):
    dtype = np.dtype(np.int8)
    is_nullable = False

    def __class_getitem__(cls, arg: Any = None) -> TypeMeta:  # type: ignore
        return cls.flyweight(arg, dtype=np.dtype(arg))


@NumpyInt8.nullable
class PandasInt8(Int8, backend="pandas"):
    dtype = pd.Int8Dtype()
    is_nullable = True




print("=" * 80)
print(f"aliases: {Type.registry.aliases}")
print()
