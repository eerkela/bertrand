from __future__ import annotations
from inspect import signature
from typing import Any, Iterator, NoReturn

import numpy as np
import pandas as pd


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
        bases: tuple[type],
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

        self.namespace["slug"] = self.name
        self.namespace["hash"] = hash(self.name)
        self.namespace["backend"] = self.backend
        self.namespace["supertype"] = self.supertype
        self.namespace["subtypes"] = set()
        self.namespace["implementations"] = {}
        self.namespace["parametrized"] = False
        self.namespace["cache_size"] = cache_size
        self.namespace["flyweights"] = {}
        self.namespace["default"] = []  # TODO: at C++ level, we could just assign this directly

    def aliases(self) -> None:
        """TODO"""
        aliases = set()
        for alias in self.namespace.get("aliases", ()):
            if not isinstance(alias, str):
                raise TypeError(f"aliases must be strings: {repr(alias)}")
            if alias in Type.registry.aliases:
                raise TypeError(f"aliases must be unique: {repr(alias)}")
            aliases.add(alias)

        self.namespace["aliases"] = aliases

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

            def wrapper(cls: type, val: Any | tuple[Any, ...]) -> type:
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
            def default(cls: type) -> type:
                """TODO"""
                return cls

            self.namespace["__class_getitem__"] = classmethod(default)

        self.namespace["params"] = parameters

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
            def default(cls: type, scalar: Any) -> type:
                """TODO"""
                return cls

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
            def default(cls: type, dtype: Any) -> type:
                """TODO"""
                return cls

            self.namespace["from_dtype"] = classmethod(default)

    def register(self, typ: TypeMeta) -> TypeMeta:
        """TODO"""
        for alias in self.namespace["aliases"]:
            Type.registry.aliases[alias] = typ

        if self.supertype is not None:
            if not self.backend:
                self.supertype.subtypes.add(typ)
            else:
                self.supertype.implementations[self.backend] = typ
            if self.default:
                self.supertype.default.append(typ)

        elif self.default:
            raise TypeError("`default` has no meaning for root types")

        return typ




def bias(t: TypeMeta) -> float:
    """TODO"""
    return abs(t.max + t.min) / (abs(t.max) + abs(t.min))


def explode_tree(t: TypeMeta, result: list[TypeMeta]) -> None:
    """TODO"""
    result.append(t)
    for sub in t.subtypes:
        explode_tree(sub, result)
    for impl in t.implementations.values():
        explode_tree(impl, result)


def explode_leaves(t: TypeMeta, result: list[TypeMeta]) -> None:
    """TODO"""
    if t.is_leaf:
        result.append(t)
    for sub in t.subtypes:
        explode_leaves(sub, result)
    for impl in t.implementations.values():
        explode_leaves(impl, result)



class TypeMeta(type):
    """TODO"""

    def __init__(
        cls: type,
        name: str,
        bases: tuple[type],
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
            print(f"dtype: {getattr(cls, 'dtype', None)}")
            print(f"scalar: {getattr(cls, 'scalar', None)}")
            print(f"itemsize: {getattr(cls, 'itemsize', None)}")
            print(f"numeric: {cls.numeric}")
            print(f"max: {cls.max}")
            print(f"min: {cls.min}")
            print(f"nullable: {cls.nullable}")
            print(f"missing: {cls.missing}")
            print()

    def __new__(
        cls: type,
        name: str,
        bases: tuple[type],
        namespace: dict[str, Any],
        backend: str = "",
        cache_size: int | None = None,
        default: bool = False
    ) -> TypeMeta:
        if len(bases) > 1:
            raise TypeError("bertrand types are limited to single inheritance")
        if len(bases) == 0 or bases[0] is object:
            return super().__new__(cls, name, bases, namespace)

        # TODO: if marked as default, parent must be abstract, and the abstract type
        # will delegate to the default implementation.

        build = TypeBuilder(name, bases, namespace, backend, cache_size, default)
        build.aliases()
        build.dtype()
        build.bounds()
        build.missing()
        build.class_getitem()
        build.from_scalar()
        build.from_dtype()

        return build.register(
            super().__new__(cls, build.name, build.bases, build.namespace)
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
                    "slug": slug,
                    "hash": hash(slug),
                    "params": kwargs,
                    "__class_getitem__": __class_getitem__
                }
            )
            cls.flyweights[slug] = typ

        return typ

    @property
    def dtype(cls) -> np.dtype | pd.api.extensions.ExtensionDtype | None:
        """TODO"""
        if cls.default:
            return cls.default[0].dtype
        return cls._dtype

    @property
    def scalar(cls) -> type | None:
        """TODO"""
        if cls.default:
            return cls.default[0].scalar
        return cls._scalar

    @property
    def itemsize(cls) -> int | None:
        """TODO"""
        if cls.default:
            return cls.default[0].itemsize
        return cls._itemsize

    @property
    def numeric(cls) -> bool:
        """TODO"""
        if cls.default:
            return cls.default[0].numeric
        return cls._numeric

    @property
    def max(cls) -> int | float:
        """TODO"""
        if cls.default:
            return cls.default[0].max
        return cls._max

    @property
    def min(cls) -> int | float:
        """TODO"""
        if cls.default:
            return cls.default[0].min
        return cls._min

    @property
    def nullable(cls) -> bool:
        """TODO"""
        if cls.default:
            return cls.default[0].nullable
        return cls._nullable

    @property
    def missing(cls) -> Any:
        """TODO"""
        if cls.default:
            return cls.default[0].missing
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
    def children(cls) -> list[TypeMeta]:
        """TODO"""
        result: list[TypeMeta] = []
        explode_tree(cls, result)
        return result[1:]

    @property
    def leaves(cls) -> list[TypeMeta]:
        """TODO"""
        result: list[TypeMeta] = []
        explode_leaves(cls, result)
        return result

    @property
    def is_leaf(cls) -> bool:
        """TODO"""
        return not cls.subtypes and not cls.implementations

    @property
    def larger(cls) -> list[TypeMeta]:
        """TODO"""
        return sorted([t for t in cls.root.leaves if t > cls])

    @property
    def smaller(cls) -> list[TypeMeta]:
        """TODO"""
        return sorted([t for t in cls.root.leaves if t < cls])

    def __getattr__(cls, name: str) -> Any:
        params = super().__getattribute__("params")
        if name in params:
            return params[name]
        return super().__getattribute__(name)

    def __setattr__(cls, name: str, val: Any) -> NoReturn:
        raise TypeError("bertrand types are immutable")

    def __instancecheck__(cls, other: Any) -> bool:
        return isinstance(other, cls.scalar)

    def __subclasscheck__(cls, other: type) -> bool:
        return super().__subclasscheck__(other)

    def __call__(cls, *args: Any, **kwargs: Any) -> pd.Series:
        # TODO: Even cooler, this could just call cast() on the input, which would
        # enable lossless conversions
        if cls.default:
            return cls.default[0](*args, **kwargs)

        return pd.Series(*args, dtype=cls.dtype, **kwargs)

    def __hash__(cls) -> int:
        return cls.hash

    def __len__(cls) -> int:
        return len(cls.leaves)

    def __iter__(cls) -> Iterator[TypeMeta]:
        return iter(cls.leaves)

    def __contains__(cls, other: type | Any) -> bool:
        if isinstance(other, type):
            return issubclass(other, cls)
        return isinstance(other, cls)

    def __or__(cls, other: TypeMeta | list[TypeMeta]) -> list[TypeMeta]:
        if isinstance(other, list):
            return [cls, *other]
        return [cls, other]

    def __ror__(cls, other: TypeMeta | list[TypeMeta]) -> list[TypeMeta]:
        if isinstance(other, list):
            return [*other, cls]
        return [other, cls]

    def __lt__(cls, other: TypeMeta) -> bool:
        features = (cls.max - cls.min, cls.itemsize, cls.nullable, bias(cls))
        compare = (other.max - other.min, other.itemsize, other.nullable, bias(other))
        return features < compare

    def __gt__(cls, other: TypeMeta) -> bool:
        features = (cls.max - cls.min, cls.itemsize, cls.nullable, bias(cls))
        compare = (other.max - other.min, other.itemsize, other.nullable, bias(other))
        return features > compare

    def __str__(cls) -> str:
        return cls.slug

    def __repr__(cls) -> str:
        return cls.slug





class Type(metaclass=TypeMeta):

    class registry:
        aliases: dict[str, type] = {}

    _numeric: bool = False
    _max: int | float = np.inf
    _min: int | float = -np.inf
    _nullable: bool = True
    _missing: Any = pd.NA

    def __new__(cls) -> NoReturn:
        raise TypeError("bertrand types cannot be instantiated")

    def __class_getitem__(cls, val: str | tuple[Any, ...]) -> type:
        """TODO"""
        if isinstance(val, str):
            return cls.implementations[val]
        return cls.implementations[val[0]][*(val[1:])]

    @classmethod
    def from_scalar(cls, scalar: Any) -> NoReturn:
        """TODO"""
        if cls.default:
            return cls.default[0].from_scalar(scalar)

        raise TypeError(f"abstract type has no default implementation: {cls.slug}")

    @classmethod
    def from_dtype(cls, dtype: Any) -> NoReturn:
        """TODO"""
        if cls.default:
            return cls.default[0].from_dtype(dtype)

        raise TypeError("abstract types cannot be constructed from a concrete dtype")





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

    def __class_getitem__(cls, x: int = 1, y: int = 2, z: int = 3) -> NumpyInt8:
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
