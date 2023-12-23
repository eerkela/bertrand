from __future__ import annotations
from inspect import signature
from typing import Any, Callable, Iterable, Iterator, NoReturn

import numpy as np
import pandas as pd

from linked import *


class TypeBuilder:

    RESERVED_SLOTS: tuple[str, ...] = (
        "__init__",
        "__new__",
        # at C++ level, ensure that type does not implement number, sequence, or mapping
        # protocols, or __richcompare__.  These are all reserved for internal use, and
        # must be consistent across all types.
    )
    POINTER_SIZE = np.dtype(np.intp).itemsize

    def __init__(
        self,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str,
        cache_size: int | None,
        default: bool,
    ):
        if not isinstance(backend, str):
            raise TypeError(f"backend id must be a string, not {repr(backend)}")
        for slot in self.RESERVED_SLOTS:
            if slot in namespace:
                raise TypeError(f"type must not implement reserved slot: {slot}")

        self.name = name
        self.bases = bases
        self.namespace = namespace
        self.backend = backend
        self.default = default
        base = bases[0]
        if base is Type:
            self.supertype = None
        else:
            self.supertype = base
            if not base.backend:
                if backend in base.implementations:
                    raise TypeError(f"backend id must be unique: {repr(backend)}")
            elif backend != base.backend:
                raise TypeError(f"backend id must match its parent: {repr(backend)}")

        self.namespace["_slug"] = self.name
        self.namespace["_hash"] = hash(self.name)
        self.namespace["_backend"] = self.backend
        self.namespace["_supertype"] = self.supertype
        self.namespace["_subtypes"] = LinkedSet[TypeMeta]()
        self.namespace["_implementations"] = LinkedDict[str:TypeMeta]()
        self.namespace["_parametrized"] = False
        self.namespace["_cache_size"] = cache_size
        self.namespace["_flyweights"] = LinkedDict[str:TypeMeta](max_size=cache_size)
        self.namespace["_default"] = []  # TODO: at C++ level, we could just assign this directly

    def aliases(self) -> None:
        """TODO"""
        aliases = LinkedSet[str]()
        for alias in self.namespace.get("aliases", ()):
            if not isinstance(alias, str):
                raise TypeError(f"aliases must be strings: {repr(alias)}")
            if alias in Type.registry.aliases:
                raise TypeError(f"aliases must be unique: {repr(alias)}")
            aliases.add(alias)

        self.namespace.pop("aliases", None)
        self.namespace["_aliases"] = aliases

    def dtype(self) -> None:
        """TODO"""
        if not self.backend:
            return

        elif "dtype" in self.namespace:
            dtype = self.namespace["dtype"]
            if not isinstance(dtype, (np.dtype, pd.api.extensions.ExtensionDtype)):
                raise TypeError(
                    f"dtype must be a numpy/pandas dtype, not {repr(dtype)}"
                )

            # TODO: itemsize might be inherited from parent.

            scalar = self.namespace.get("scalar", dtype.type)
            itemsize = self.namespace.get("itemsize", dtype.itemsize)
            if scalar != dtype.type:
                raise TypeError("dtype and scalar must be consistent")
            if itemsize != dtype.itemsize:
                raise TypeError(
                    f"dtype.itemsize ({dtype.itemsize}) must match custom itemsize "
                    f"({self.namespace['itemsize']})"
                )

            self.namespace["_dtype"] = dtype
            self.namespace["_scalar"] = dtype.type
            self.namespace["_itemsize"] = dtype.itemsize

        elif "scalar" in self.namespace:
            scalar = self.namespace["scalar"]
            dtype = None  # TODO: synthesize dtype
            itemsize = self.namespace.get("itemsize", self.POINTER_SIZE)
            if itemsize != self.POINTER_SIZE:
                raise TypeError(
                    f"custom itemsize ({self.namespace['itemsize']}) must match size "
                    f"of single PyObject* pointer on this platform "
                    f"({self.POINTER_SIZE} bytes)"
                )

            self.namespace["_dtype"] = dtype
            self.namespace["_scalar"] = scalar
            self.namespace["_itemsize"] = itemsize

        else:
            raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")

        self.namespace.pop("dtype", None)
        self.namespace.pop("scalar", None)
        self.namespace.pop("itemsize", None)

    def bounds(self) -> None:
        """TODO"""
        has_max = "max" in self.namespace
        has_min = "min" in self.namespace
        if has_max or has_min:
            high = self.namespace["max"] if has_max else np.inf
            low = self.namespace["min"] if has_min else -np.inf
            if not np.isinf(high):
                high = int(high)
            if not np.isinf(low):
                low = int(low)
            if high < low:
                raise TypeError(f"max cannot be less than min ({high} < {low})")

            self.namespace["_numeric"] = True
            self.namespace["_max"] = high
            self.namespace["_min"] = low

        self.namespace.pop("numeric", None)
        self.namespace.pop("max", None)
        self.namespace.pop("min", None)

    def missing(self) -> None:
        """TODO"""
        if "nullable" in self.namespace:
            self.namespace["_nullable"] = bool(self.namespace["nullable"])

        if "missing" in self.namespace:
            missing = self.namespace["missing"]
            # TODO: if missing does not match dtype, throw an error.  This should also
            # guarantee that pd.isna(missing) is True.
            self.namespace["_nullable"] = True
            self.namespace["_missing"] = missing

        self.namespace.pop("nullable", None)
        self.namespace.pop("missing", None)

    def class_getitem(self) -> None:
        """TODO"""
        parameters = {}

        # type is abstract
        if not self.backend:
            if "__class_getitem__" in self.namespace:
                raise TypeError("abstract types must not implement __class_getitem__()")

        # type is concrete and implements __class_getitem__
        elif "__class_getitem__" in self.namespace:
            wrapped = self.namespace["__class_getitem__"]

            def wrapper(cls: type, val: Any | tuple[Any, ...]) -> TypeMeta:
                if isinstance(val, tuple):
                    return wrapped(cls, *val)
                return wrapped(cls, val)

            self.namespace["__class_getitem__"] = classmethod(wrapper)

            skip = True
            sig = signature(wrapped)
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

        # type is concrete, does not implement __class_getitem__, and parent is abstract
        elif self.supertype is not None and not self.supertype.backend:
            def default(cls: type) -> TypeMeta:
                """TODO"""
                return cls  # type: ignore

            self.namespace["__class_getitem__"] = classmethod(default)

        self.namespace["_params"] = parameters

    def from_scalar(self) -> None:
        """TODO"""
        # type is abstract
        if not self.backend:
            if "from_scalar" in self.namespace:
                raise TypeError("abstract types must not implement from_scalar()")

        # type is concrete and implements from_scalar
        elif "from_scalar" in self.namespace:
            sig = signature(self.namespace["from_scalar"])
            params = list(sig.parameters.values())
            if len(params) != 1 or not (
                params[0].kind == params[0].POSITIONAL_ONLY or
                params[0].kind == params[0].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_scalar() must accept a single positional argument"
                )

        # type is concrete, does not implement from_scalar, and parent is abstract
        elif self.supertype is not None and not self.supertype.backend:
            def default(cls: type, scalar: Any) -> TypeMeta:
                """TODO"""
                return cls  # type: ignore

            self.namespace["from_scalar"] = classmethod(default)

    def from_dtype(self) -> None:
        """TODO"""
        # type is abstract
        if not self.backend:
            if "from_dtype" in self.namespace:
                raise TypeError("abstract types must not implement from_dtype()")

        # type is concrete and implements from_dtype
        elif "from_dtype" in self.namespace:
            sig = signature(self.namespace["from_dtype"])
            params = list(sig.parameters.values())
            if len(params) != 1 or not (
                params[0].kind == params[0].POSITIONAL_ONLY or
                params[0].kind == params[0].POSITIONAL_OR_KEYWORD
            ):
                raise TypeError(
                    "from_dtype() must accept a single positional argument"
                )

        # type is concrete, does not implement from_dtype, and parent is abstract
        elif self.supertype is not None and not self.supertype.backend:
            def default(cls: type, dtype: Any) -> TypeMeta:
                """TODO"""
                return cls  # type: ignore

            self.namespace["from_dtype"] = classmethod(default)

    def register(self, typ: TypeMeta) -> TypeMeta:
        """TODO"""
        for alias in self.namespace["_aliases"]:
            Type.registry.aliases[alias] = typ

        if self.supertype is not None:
            if not self.backend:
                self.supertype._subtypes.add(typ)
            else:
                self.supertype._implementations[self.backend] = typ
            if self.default:
                self.supertype._default.append(typ)  # TODO: at C++ level, we could just assign this directly

        elif self.default:
            raise TypeError("`default` has no meaning for root types")

        return typ


class TypeMeta(type):
    """TODO"""

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
            print(f"aliases: {cls._aliases}")
            print(f"cache_size: {cls.cache_size}")
            print(f"supertype: {cls.supertype}")
            print(f"dtype: {getattr(cls, '_dtype', None)}")
            print(f"scalar: {getattr(cls, '_scalar', None)}")
            print(f"itemsize: {getattr(cls, '_itemsize', None)}")
            print(f"numeric: {cls._numeric}")
            print(f"max: {cls._max}")
            print(f"min: {cls._min}")
            print(f"nullable: {cls._nullable}")
            print(f"missing: {cls._missing}")
            print()

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[TypeMeta],
        namespace: dict[str, Any],
        backend: str = "",
        cache_size: int | None = None,
        default: bool = False
    ) -> TypeMeta:
        if len(bases) > 1:
            raise TypeError("bertrand types are limited to single inheritance")
        if len(bases) == 0 or bases[0] is object:
            return super().__new__(mcs, name, bases, namespace)

        build = TypeBuilder(name, bases, namespace, backend, cache_size, default)
        build.aliases()
        build.dtype()
        build.bounds()
        build.missing()
        build.class_getitem()
        build.from_scalar()
        build.from_dtype()

        return build.register(
            super().__new__(mcs, build.name, build.bases, build.namespace)
        )

    def flyweight(cls, **kwargs: Any) -> TypeMeta:
        """TODO"""
        slug = f"{cls.__name__}[{', '.join(repr(v) for v in kwargs.values())}]"
        typ = cls.flyweights.get(slug, None)

        if typ is None:
            def __class_getitem__(cls: type, *args: Any) -> NoReturn:
                raise TypeError(f"{slug} cannot be re-parametrized")

            typ = super().__new__(
                type(cls),
                cls.__name__,
                (cls,),
                cls.params | {
                    "_slug": slug,
                    "_hash": hash(slug),
                    "_params": kwargs,
                    "__class_getitem__": __class_getitem__
                }
            )
            cls.flyweights[slug] = typ

        return typ

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
    def cache_size(cls) -> int:
        """TODO"""
        return cls._cache_size

    @property
    def flyweights(cls) -> LinkedDict[str, TypeMeta]:
        """TODO"""
        return cls._flyweights  # TODO: make this read-only, but automatically LRU update

    @property
    def abstract(cls) -> TypeMeta | None:
        """TODO"""
        if not cls._backend:
            return cls
        if cls.supertype is None:
            return None
        return cls.supertype.abstract

    @property
    def parametrized(cls) -> bool:
        """TODO"""
        return cls._parametrized

    @property
    def params(cls) -> LinkedDict[str, Any]:
        """TODO"""
        return cls._params

    @property
    def default(cls) -> TypeMeta:
        """TODO"""
        return None if not cls._default else cls._default[0]  # TODO: easier at C++ level

    @property
    def aliases(self) -> LinkedSet[str]:
        """TODO"""
        # TODO: make this an AliasManager, such that aliases can be added and removed
        # dynamically.
        return self._aliases

    @property
    def dtype(cls) -> np.dtype | pd.api.extensions.ExtensionDtype | None:
        """TODO"""
        if cls.default:
            return cls.default.dtype
        return cls._dtype

    @property
    def scalar(cls) -> type | None:
        """TODO"""
        if cls.default:
            return cls.default.scalar
        return cls._scalar

    @property
    def itemsize(cls) -> int | None:
        """TODO"""
        if cls.default:
            return cls.default.itemsize
        return cls._itemsize

    @property
    def numeric(cls) -> bool:
        """TODO"""
        if cls.default:
            return cls.default.numeric
        return cls._numeric

    @property
    def max(cls) -> int | float:
        """TODO"""
        if cls.default:
            return cls.default.max
        return cls._max

    @property
    def min(cls) -> int | float:
        """TODO"""
        if cls.default:
            return cls.default.min
        return cls._min

    @property
    def nullable(cls) -> bool:
        """TODO"""
        if cls.default:
            return cls.default.nullable
        return cls._nullable

    @property
    def missing(cls) -> Any:
        """TODO"""
        if cls.default:
            return cls.default.missing
        return cls._missing

    @property
    def root(cls) -> TypeMeta:
        """TODO"""
        parent = cls
        while parent.supertype is not None:
            parent = parent.supertype
        return parent

    @property
    def is_root(cls) -> bool:
        """TODO"""
        return cls.supertype is None

    @property
    def supertype(cls) -> TypeMeta | None:
        """TODO"""
        return cls._supertype

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
        params = super().__getattribute__("_params")
        if name in params:
            return params[name]
        return super().__getattribute__(name)

    def __setattr__(cls, name: str, val: Any) -> NoReturn:
        raise TypeError("bertrand types are immutable")

    def __instancecheck__(cls, other: Any) -> bool:
        return isinstance(other, cls.scalar)

    def __subclasscheck__(cls, other: type) -> bool:
        return super().__subclasscheck__(other)

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls.default:
            return cls.default(*args, **kwargs)

        return pd.Series(*args, dtype=cls.dtype, **kwargs)  # type: ignore

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
            cls.nullable,  # nullability
            abs(cls.max + cls.min)  # bias away from zero
        )
        compare = (
            other.max - other.min,
            other.itemsize,
            other.nullable,
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
            cls.nullable,
            abs(cls.max + cls.min)
        )
        compare = (
            other.max - other.min,
            other.itemsize,
            other.nullable,
            abs(other.max + other.min)
        )
        return features > compare

    def __str__(cls) -> str:
        return cls._slug

    def __repr__(cls) -> str:
        return cls._slug


# TODO: static analysis of decorator types
class DecoratorBuilder:
    pass



class DecoratorMeta(type):
    pass

    def __new__(
        mcs: type,
        name: str,
        bases: tuple[DecoratorMeta],
        namespace: dict[str, Any],
        backend: str = "",
        cache_size: int | None = None,
        default: bool = False
    ) -> DecoratorMeta:
        if len(bases) > 1:
            raise TypeError("bertrand decorators are limited to single inheritance")
        if len(bases) == 0 or bases[0] is object:
            return super().__new__(mcs, name, bases, namespace)

        build = DecoratorBuilder(name, bases, namespace, backend, cache_size, default)
        build.aliases()
        build.dtype()
        build.missing()
        build.class_getitem()
        build.from_scalar()
        build.from_dtype()

        return build.register(
            super().__new__(mcs, build.name, build.bases, build.namespace)
        )



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


class Union:

    def __init__(self, *args: TypeMeta) -> None:
        self.types = LinkedSet[TypeMeta](args)

    @classmethod
    def from_set(cls, types: LinkedSet[TypeMeta]) -> Union:
        """TODO"""
        result = cls.__new__(cls)
        result.types = types
        return result

    @property
    def index(self) -> NoReturn:
        """TODO"""
        raise NotImplementedError()

    @property
    def root(self) -> Union:
        """TODO"""
        return self.from_set(LinkedSet[TypeMeta](t.root for t in self.types))

    @property
    def supertype(self) -> Union:
        """TODO"""
        return self.from_set(LinkedSet[TypeMeta](t.supertype for t in self.types))

    @property
    def subtypes(self) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        for typ in self.types:
            result.update(typ.subtypes)
        return self.from_set(result)

    @property
    def abstract(self) -> Union:
        """TODO"""
        return self.from_set(LinkedSet[TypeMeta](t.abstract for t in self.types))

    @property
    def implementations(self) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        for typ in self.types:
            result.update(typ.implementations)
        return self.from_set(result)

    @property
    def children(self) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        for typ in self.types:
            result.update(typ.children)
        return self.from_set(result)

    @property
    def leaves(self) -> Union:
        """TODO"""
        result = LinkedSet[TypeMeta]()
        for typ in self.types:
            result.update(typ.leaves)
        return self.from_set(result)

    def sorted(
        self,
        *,
        key: Callable[[TypeMeta], Any] | None = None,
        reverse: bool = False
    ) -> Union:
        """TODO"""
        result = self.types.copy()
        result.sort(key=key, reverse=reverse)
        return self.from_set(result)

    def __getattr__(self, name: str) -> LinkedDict[TypeMeta, Any]:
        result = LinkedDict[TypeMeta, Any]()
        for typ in self.types:
            result[typ] = getattr(typ, name)
        return result

    def __getitem__(self, key: TypeMeta | slice) -> Union:
        if isinstance(key, slice):
            return self.from_set(self.types[key])
        return self.types[key]

    def __call__(self, *args: Any, **kwargs: Any) -> pd.Series[Any]:
        for typ in self.types:
            try:
                return typ(*args, **kwargs)
            except Exception:
                continue
        raise TypeError(f"cannot convert to union type: {self}")

    def __contains__(self, other: type | Any) -> bool:
        if isinstance(other, type):
            return any(issubclass(other, typ) for typ in self.types)
        return any(isinstance(other, typ) for typ in self.types)

    def __len__(self) -> int:
        return len(self.types)

    def __iter__(self) -> Iterator[TypeMeta]:
        return iter(self.types)

    def __reversed__(self) -> Iterator[TypeMeta]:
        return reversed(self.types)

    def __or__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
        return self.from_set(self.types | other)

    def __sub__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
        return self.from_set(self.types - other)

    def __and__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
        return self.from_set(self.types & other)

    def __xor__(self, other: TypeMeta | Iterable[TypeMeta]) -> Union:
        return self.from_set(self.types ^ other)

    # TODO: this has to do hierarchical checking, not just set comparison

    def __lt__(self, other: Union) -> bool:
        return self.types < other.types

    def __le__(self, other: Union) -> bool:
        return self.types <= other.types

    def __eq__(self, other: Union) -> bool:
        return self.types == other.types

    def __ne__(self, other: Union) -> bool:
        return self.types != other.types

    def __ge__(self, other: Union) -> bool:
        return self.types >= other.types

    def __gt__(self, other: Union) -> bool:
        return self.types > other.types

    def __str__(self) -> str:
        return f"{{{', '.join(str(t) for t in self.types)}}}"

    def __repr__(self) -> str:
        return f"Union({', '.join(repr(t) for t in self.types)})"



class Type(metaclass=TypeMeta):

    class registry:
        aliases: dict[str, TypeMeta] = {}

    _numeric: bool = False
    _max: int | float = np.inf
    _min: int | float = -np.inf
    _nullable: bool = True
    _missing: Any = pd.NA

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand types cannot be instantiated")

    def __class_getitem__(cls, val: str | tuple[Any, ...]) -> TypeMeta:
        """TODO"""
        if isinstance(val, str):
            return cls._implementations[val]
        return cls._implementations[val[0]][*(val[1:])]

    @classmethod
    def from_scalar(cls, scalar: Any) -> TypeMeta:
        """TODO"""
        if cls.default:
            return cls.default.from_scalar(scalar)

        raise TypeError(f"abstract type has no default implementation: {cls.slug}")

    @classmethod
    def from_dtype(cls, dtype: Any) -> TypeMeta:
        """TODO"""
        if cls.default:
            return cls.default.from_dtype(dtype)

        raise TypeError("abstract types cannot be constructed from a concrete dtype")











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






class Int(Type):
    aliases = {"int", "integer"}




class Signed(Int, default=True):
    aliases = {"signed"}




class Int8(Signed):
    aliases = {"int8", "char"}
    itemsize = 1
    max = 2**7 - 1
    min = -2**7


class NumpyInt8(Int8, backend="numpy", default=True):
    dtype = np.dtype(np.int8)
    nullable = False

    def __class_getitem__(cls, x = 1, y = 2, z = 3) -> NumpyInt8:
        return cls.flyweight(x=x, y=y, z=z)


class PandasInt8(Int8, backend="pandas"):
    dtype = pd.Int8Dtype()
    nullable = True




class Int16(Signed):
    aliases = {"int16", "short"}
    itemsize = 2
    max = 2**15 - 1
    min = -2**15


class NumpyInt16(Int16, backend="numpy", default=True):
    dtype = np.dtype(np.int16)
    nullable = False


class PandasInt16(Int16, backend="pandas"):
    dtype = pd.Int16Dtype()
    nullable = True




class Int32(Signed):
    aliases = {"int32", "long"}
    itemsize = 4
    max = 2**31 - 1
    min = -2**31


class NumpyInt32(Int32, backend="numpy", default=True):
    dtype = np.dtype(np.int32)
    nullable = False


class PandasInt32(Int32, backend="pandas"):
    dtype = pd.Int32Dtype()
    nullable = True




class Int64(Signed, default=True):
    aliases = {"int64", "long long"}
    itemsize = 8
    max = 2**63 - 1
    min = -2**63


class NumpyInt64(Int64, backend="numpy", default=True):
    dtype = np.dtype(np.int64)
    nullable = False


class PandasInt64(Int64, backend="pandas"):
    dtype = pd.Int64Dtype()
    nullable = True




print("=" * 80)
print(f"aliases: {Type.registry.aliases}")
print()
