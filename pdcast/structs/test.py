
from inspect import signature
from typing import Any, Callable, NoReturn

import numpy as np
import pandas as pd


# TODO: len() should return itemsize


limit = int | float | None

RESERVED_SLOTS: tuple[str, ...] = (
    "__init__",
    "__new__",
    # at C++ level, ensure that type does not implement number, sequence, or mapping
    # protocols, or __richcompare__.  These are all reserved for internal use, and
    # must be consistent across all types.
)

EMPTY = object()  # sentinel value for fields that are not set on a type


def validate_name(name: str) -> None:
    """TODO"""
    if name in Type.registry.aliases:
        raise TypeError(f"class name must be unique: {repr(name)}")


def validate_bases(bases: tuple[type]) -> None:
    """TODO"""
    if len(bases) > 1:
        raise TypeError("bertrand types are limited to single inheritance")


def validate_namespace(namespace: dict[str, Any]) -> None:
    """TODO"""
    for slot in RESERVED_SLOTS:
        if slot in namespace:
            raise TypeError(f"type must not implement reserved slot: {slot}")


def get_supertype(typ: type, bases: tuple[type]) -> tuple[type | None, type, bool]:
    """TODO"""
    supertype = bases[0]
    if supertype is Type:
        return None, typ, True
    else:
        return supertype, getattr(supertype, "root"), False


def validate_backend(supertype: type | None, backend: str | None) -> str:
    """TODO"""
    if backend is None:
        return ""

    if not isinstance(backend, str):
        raise TypeError(f"backend must be a string, not {repr(backend)}")

    if supertype is not None:
        if getattr(supertype, "abstract"):
            if backend in getattr(supertype, "implementations"):
                raise TypeError(f"backend id must be unique: {repr(backend)}")
        elif backend != getattr(supertype, "backend"):
            raise TypeError(f"backend id must match its parent: {repr(backend)}")

    return backend


def validate_aliases(typ: type, name: str, namespace: dict[str, Any]) -> set[str]:
    """TODO"""
    Type.registry.aliases[name] = typ
    result = set()
    for alias in namespace.get("aliases", ()):
        if not isinstance(alias, str):
            raise TypeError(f"aliases must be strings: {repr(alias)}")
        if alias in Type.registry.aliases:
            raise TypeError(f"aliases must be unique: {repr(alias)}")
        Type.registry.aliases[alias] = typ

    return result


def validate_class_getitem(
    identifier: str,
    namespace: dict[str, Any],
    backend: str | None,
    args: dict[str, Any] | None,
    supertype: type | None,
) -> Callable[..., Any] | EMPTY:
    """TODO"""
    is_abstract = backend is None
    is_parametrized = args is not None

    # TODO: this doesn't work as expected.  __class_getitem__ always receives a tuple
    # of arguments if used with commas.  We need to find a way to compensate for this.
    # -> It might be possible to generate a wrapper that unpacks the tuple before
    # calling the original implementation.

    # abstract types always forward arguments to a concrete implementation
    if is_abstract:
        if "__class_getitem__" in namespace:
            raise TypeError(
                f"abstract types must not implement __class_getitem__: {identifier}"
            )

        def abstract(cls: type, backend: str, *args: Any, **kwargs: Any) -> type:
            """TODO"""
            return cls.implementations[backend][*args]

        return abstract

    # parametrized types always throw an error if re-parametrized
    if is_parametrized:
        if "__class_getitem__" in namespace:
            raise TypeError(
                f"parametrized types must not implement __class_getitem__: {identifier}"
            )

        def parametrized(cls: type, *args: Any, **kwargs: Any) -> NoReturn:
            """TODO"""
            raise TypeError(f"{identifier} cannot be re-parametrized")

        return parametrized

    # concrete types either inherit from parent or default to throwing an error
    inherit_from_parent = supertype is not None and not getattr(supertype, "abstract")
    if "__class_getitem__" in namespace or inherit_from_parent:
        return EMPTY

    def default(cls: type) -> type:
        """TODO"""
        return cls

    return default


def validate_from_scalar(
    identifier: str,
    namespace: dict[str, Any],
    backend: str | None,
    args: dict[str, Any] | None,
    supertype: type | None,
) -> Callable[[Any], type] | EMPTY:
    """TODO"""
    is_abstract = backend is None
    is_parametrized = args is not None

    # abstract types cannot be constructed from scalars
    if is_abstract:
        if "from_scalar" in namespace:
            raise TypeError(
                f"abstract types must not implement from_scalar(): {identifier}"
            )

        def abstract(cls: type, scalar: Any) -> NoReturn:
            """TODO"""
            raise TypeError(
                f"({identifier}) cannot construct an abstract type from a concrete "
                f"scalar"
            )

        return abstract

    # parametrized types always inherit from parent
    if is_parametrized:
        if "from_scalar" in namespace:
            raise TypeError(
                f"({identifier}) parametrized type must not implement from_scalar()"
            )

        return EMPTY

    # concrete types either inherit from parent or default to identity function
    if "from_scalar" in namespace:
        sig = signature(namespace["from_scalar"])
        params = list(sig.parameters.values())
        if len(params) != 1 or not (
            params[0].kind == params[0].POSITIONAL_ONLY or
            params[0].kind == params[0].POSITIONAL_OR_KEYWORD
        ):
            raise TypeError(
                f"({identifier}) from_scalar() must accept a single positional argument"
            )
        return EMPTY

    if supertype is not None and getattr(supertype, "abstract"):
        def default(cls: type, scalar: Any) -> type:
            """TODO"""
            return cls

        return default

    return EMPTY


def validate_from_dtype(
    identifier: str,
    namespace: dict[str, Any],
    backend: str | None,
    args: dict[str, Any] | None,
    supertype: type | None,
) -> Callable[[Any], type] | EMPTY:
    """TODO"""
    is_abstract = backend is None
    is_parametrized = args is not None

    # abstract types cannot be constructed from scalars
    if is_abstract:
        if "from_dtype" in namespace:
            raise TypeError(
                f"abstract types must not implement from_dtype(): {identifier}"
            )

        def abstract(cls: type, scalar: Any) -> NoReturn:
            """TODO"""
            raise TypeError(
                f"cannot construct an abstract type from a concrete dtype: {identifier}"
            )

        return abstract

    # parametrized types always inherit from parent
    if is_parametrized:
        if "from_dtype" in namespace:
            raise TypeError(
                f"parametrized types must not implement from_dtype(): {identifier}"
            )

        return EMPTY

    # concrete types either inherit from parent or default to identity function
    if "from_dtype" in namespace:
        sig = signature(namespace["from_dtype"])
        params = list(sig.parameters.values())
        if len(params) != 1 or not (
            params[0].kind == params[0].POSITIONAL_ONLY or
            params[0].kind == params[0].POSITIONAL_OR_KEYWORD
        ):
            raise TypeError(
                f"({identifier}) from_dtype() must accept a single positional argument"
            )
        return EMPTY

    if supertype is not None and getattr(supertype, "abstract"):
        def default(cls: type, dtype: Any) -> type:
            """TODO"""
            return cls

        return default

    return EMPTY


def validate_bounds(namespace: dict[str, Any]) -> tuple[limit, limit] | Any:
    """TODO"""
    has_max = "max" in namespace
    has_min = "min" in namespace
    if has_max or has_min:
        high = int(namespace["max"]) if has_max else float("inf")
        low = int(namespace["min"]) if has_min else float("-inf")
        if high < low:
            raise TypeError(f"max cannot be less than min ({high} < {low})")

        return high, low

    return EMPTY


def validate_dtype(namespace: dict[str, Any]) -> tuple[Any, type, int]:
    """TODO"""
    if "dtype" in namespace:
        dtype = namespace["dtype"]
        if "scalar" in namespace and namespace["scalar"] != dtype.type:
            raise TypeError("dtype and scalar must be consistent")
        if "itemsize" in namespace and namespace["itemsize"] != dtype.itemsize:
            raise TypeError(
                f"dtype.itemsize ({dtype.itemsize}) must match custom itemsize "
                f"({namespace['itemsize']})"
            )

        return dtype, dtype.type, dtype.itemsize

    if "scalar" in namespace:
        scalar = namespace["scalar"]
        # dtype = synthesize_dtype(scalar)
        dtype = None
        if "itemsize" in namespace and namespace["itemsize"] != 8:
            raise TypeError(
                f"custom itemsize ({namespace['itemsize']}) must match size of single "
                f"PyObject* pointer on this platform (8 bytes)"
            )

        return dtype, scalar, 8

    raise TypeError("type must define at least one of 'dtype' and/or 'scalar'")


def validate_missing(dtype: Any, namespace: dict[str, Any]) -> tuple[Any, bool] | Any:
    """TODO"""
    if "missing" in namespace:
        missing = namespace["missing"]
        # TODO: if missing does not match dtype, throw an error.  This should guarantee
        # that pd.isna(missing) is True.
        return missing

    return EMPTY


class TypeMeta(type):
    """TODO"""

    def __new__(
        cls: type,
        name: str,
        bases: tuple[type],
        namespace: dict[str, Any],
        backend: str | None = None,
        args: dict[str, Any] | None = None,
        **kwargs: Any
    ) -> type:
        identifier = name
        if args is None:
            typ = None
        else:
            identifier += f"[{', '.join(repr(v) for v in args.values())}]"
            typ = getattr(cls, "flyweights").get(identifier, None)

        if typ is None:
            typ = super().__new__(cls, name, bases, namespace)
            if len(bases) == 0 or bases[0] is object:
                return typ

            print(f"initializing: {identifier}")
            super().__setattr__(typ, "immutable", False)

            validate_name(name)
            validate_bases(bases)
            validate_namespace(namespace)

            setattr(typ, "parameters", args or dict())
            setattr(typ, "name", name)
            setattr(typ, "slug", identifier)
            setattr(typ, "abstract", backend is None)
            setattr(typ, "parametrized", args is not None)
            setattr(typ, "aliases", validate_aliases(typ, name, namespace))
            # validate_aliases() updates the registry, so we need to either stage
            # changes or have a way to revert it if an error occurs later on.

            supertype, root, is_root = get_supertype(typ, bases)
            setattr(typ, "supertype", supertype)
            setattr(typ, "root", root)
            setattr(typ, "is_root", is_root)
            setattr(typ, "backend", validate_backend(supertype, backend))
            setattr(typ, "implementations", {})
            setattr(typ, "subtypes", set())
            # leaves  <- computed dynamically via @property
            # is_leaf

            dtype, scalar, itemsize = validate_dtype(namespace)
            setattr(typ, "dtype", dtype)
            setattr(typ, "scalar", scalar)
            setattr(typ, "itemsize", itemsize)

            missing = validate_missing(dtype, namespace)
            if missing is not EMPTY:
                setattr(typ, "missing", missing)
                setattr(typ, "nullable", True)

            bounds = validate_bounds(namespace)
            if bounds is not EMPTY:
                high, low = bounds
                setattr(typ, "max", high)
                setattr(typ, "min", low)
                setattr(typ, "numeric", True)

            class_getitem = validate_class_getitem(
                identifier, namespace, backend, args, supertype
            )
            if class_getitem is not EMPTY:
                setattr(typ, "__class_getitem__", classmethod(class_getitem))

            from_scalar = validate_from_scalar(
                identifier, namespace, backend, args, supertype
            )
            if from_scalar is not EMPTY:
                setattr(typ, "from_scalar", classmethod(from_scalar))

            from_dtype = validate_from_dtype(
                identifier, namespace, backend, args, supertype
            )
            if from_dtype is not EMPTY:
                setattr(typ, "from_dtype", classmethod(from_dtype))

            # update supertype attributes
            if args is None and supertype is not None:
                if backend is None:
                    getattr(supertype, "subtypes").add(typ)
                else:
                    getattr(supertype, "implementations")[backend] = typ

            setattr(typ, "immutable", True)
            if not hasattr(cls, "flyweights"):
                setattr(cls, "flyweights", dict())
            getattr(cls, "flyweights")[identifier] = typ

        return typ

    def __getattr__(cls, name: str) -> Any:
        params = super().__getattribute__("parameters")
        if name in params:
            return params[name]

        return super().__getattribute__(name)

    def __setattr__(cls, name: str, val: Any) -> None:
        if getattr(cls, "immutable", False):
            raise TypeError("type objects are immutable")
        super().__setattr__(name, val)

    def __instancecheck__(cls, other: Any) -> bool:
        return super().__instancecheck__(other)

    def __subclasscheck__(cls, other: type) -> bool:
        return super().__subclasscheck__(other)



class Type(metaclass=TypeMeta):

    class registry:
        aliases = dict()
        # roots = set()

    max = float("inf")
    min = float("-inf")

    # @property
    # def is_leaf(cls) -> bool:
    #     """TODO"""
    #     return not cls.subtypes

    # @property
    # def leaves(cls) -> set[type]:
    #     """TODO"""
    #     if not cls.subtypes:
    #         return {cls}

    #     return {t.leaves for t in cls.subtypes}

    def __new__(cls):
        raise TypeError("bertrand types cannot be instantiated")



class Int(Type):
    scalar = int
    # aliases = {"int", "integer"}
    # max = float("inf")


class Signed(Int, backend="numpy"):
    scalar = float
    # aliases = {"Int"}


t = Int

print(f"aliases: {t.registry.aliases}")
print(f"type: {t.name}{'' if t.abstract else '[' + t.backend + ']'}")
print(f"supertype: {t.supertype}")
print(f"subtypes: {t.subtypes}")
print(f"implementations: {t.implementations}")
print(f"max: {t.max}")
print(f"min: {t.min}")
