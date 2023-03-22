from types import MappingProxyType
from typing import Any, Iterator

cimport numpy as np
import numpy as np
import pandas as pd

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

cimport pdcast.types.base.atomic as atomic
cimport pdcast.types.base.composite as composite

from pdcast.util.type_hints import type_specifier


cdef class AdapterType(atomic.ScalarType):
    """Special case for AtomicTypes that modify other AtomicTypes.

    These can be nested to form a singly-linked list that can be used to apply
    multiple transformations at once, provided they are supported by pandas
    (which is not a guarantee).  Sparse types and categorical types may be
    well-supported individually, but may not work in combination, for instance.
    """

    _priority = 0  # controls the order of nested adapters.  Higher comes first
    # NOTE: CategoricalType has priority 5, SparseType has priority 10.

    def __init__(self, wrapped: atomic.ScalarType, **kwargs):
        self._wrapped = wrapped
        self.kwargs = MappingProxyType({"wrapped": wrapped} | kwargs)
        self.slug = self.slugify(wrapped, **kwargs)
        self.hash = hash(self.slug)

    #############################
    ####    CLASS METHODS    ####
    #############################

    @classmethod
    def resolve(cls, wrapped: str = None, *args: str) -> AdapterType:
        """An alternate constructor used to parse input in the type
        specification mini-language.

        Override this if your AdapterType implements custom parsing rules for
        any arguments that are supplied to this type.

        .. Note: The inputs to each argument will always be strings.
        """
        if wrapped is None:
            return cls()

        cdef ScalarType instance = resolve.resolve_type(wrapped)

        # insert into sorted adapter stack according to priority
        for x in instance.adapters:
            if x._priority <= cls._priority:  # initial
                break
            if getattr(x.wrapped, "_priority", -np.inf) <= cls._priority:
                x.wrapped = cls(x.wrapped, *args)
                return instance

        # add to front of stack
        return cls(instance, *args)

    @classmethod
    def from_dtype(cls, dtype: pd.api.extension.ExtensionDtype) -> AdapterType:
        """Construct an AtomicType from a corresponding numpy/pandas ``dtype``
        object.
        """
        return cls.instance()  # NOTE: most types disregard dtype fields

    @classmethod
    def slugify(cls, wrapped: atomic.ScalarType) -> str:
        return f"{cls.name}[{str(wrapped)}]"

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def adapters(self) -> Iterator[AdapterType]:
        """Iterate through every AdapterType that is attached to the wrapped
        AtomicType.
        """
        frame = self
        while isinstance(frame, AdapterType):
            yield frame
            frame = frame.wrapped

    @property
    def atomic_type(self) -> atomic.AtomicType:
        """Access the underlying AtomicType instance with every adapter removed
        from it.
        """
        result = self.wrapped
        while isinstance(result, AdapterType):
            result = result.wrapped
        return result

    @atomic_type.setter
    def atomic_type(self, val: atomic.ScalarType) -> None:
        lowest = self
        while isinstance(lowest.wrapped, AdapterType):
            lowest = lowest.wrapped
        lowest.wrapped = val

    @property
    def backends(self) -> MappingProxyType:
        return {
            k: self.replace(wrapped=v)
            for k, v in self.wrapped.backends.items()
        }

    @property
    def wrapped(self) -> atomic.ScalarType:
        """Access the type object that this AdapterType modifies."""
        return self._wrapped

    @wrapped.setter
    def wrapped(self, val: atomic.ScalarType) -> None:
        """Change the type object that this AdapterType modifies."""
        self._wrapped = val
        self.kwargs = self.kwargs | {"wrapped": val}
        self.slug = self.slugify(**self.kwargs)
        self.hash = hash(self.slug)

    #######################
    ####    METHODS    ####
    #######################

    def contains(self, other: type_specifier, exact: bool = False) -> bool:
        """Test whether `other` is a subtype of the given AtomicType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        For AdapterTypes, this merely delegates to AtomicType.contains().
        """
        other = resolve.resolve_type(other)
        if isinstance(other, composite.CompositeType):
            return all(self.contains(o, exact=exact) for o in other)

        return (
            isinstance(other, type(self)) and
            self.wrapped.contains(other.wrapped, exact=exact)
        )

    def inverse_transform(
        self,
        series: convert.SeriesWrapper
    ) -> convert.SeriesWrapper:
        """Remove an adapter from an example series."""
        series.element_type = self.wrapped
        return series.rectify()

    def replace(self, **kwargs) -> AdapterType:
        # extract kwargs pertaining to AdapterType
        adapter_kwargs = {}
        atomic_kwargs = {}
        for k, v in kwargs.items():
            if k in self.kwargs:
                adapter_kwargs[k] = v
            else:
                atomic_kwargs[k] = v

        # merge adapter_kwargs with self.kwargs and get wrapped type
        adapter_kwargs = {**self.kwargs, **adapter_kwargs}
        wrapped = adapter_kwargs.pop("wrapped")

        # pass non-adapter kwargs down to wrapped.replace()
        wrapped = wrapped.replace(**atomic_kwargs)

        # construct new AdapterType
        return type(self)(wrapped=wrapped, **adapter_kwargs)

    def transform(
        self,
        series: convert.SeriesWrapper
    ) -> convert.SeriesWrapper:
        """Given an unwrapped conversion result, apply all the necessary logic
        to bring it into alignment with this AdapterType and all its children.

        This is a recursive method that traverses the `adapters` linked list
        in reverse order (from the inside out).  At the first level, the
        unwrapped series is passed as input to that adapter's
        `transform()` method, which may be overridden as needed.  That
        method must return a properly-wrapped copy of the original, which is
        passed to the next adapter and so on.  Thus, if an AdapterType seeks to
        change any aspect of the series it adapts (as is the case with
        sparse/categorical types), then it must override this method and invoke
        it *before* applying its own logic, like so:

        ```
        series = super().apply_adapters(series)
        ```

        This pattern maintains the inside-out resolution order of this method.
        """
        return series

    def unwrap(self) -> atomic.AtomicType:
        """Strip any AdapterTypes that have been attached to this AtomicType.
        """
        return self.atomic_type

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, other: type_specifier) -> bool:
        return self.contains(other)

    def __dir__(self) -> list:
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.wrapped) if x not in result]
        return result

    def __eq__(self, other: type_specifier) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AdapterType) and self.hash == other.hash

    def __getattr__(self, name: str) -> Any:
        try:
            return self.kwargs[name]
        except KeyError as err:
            val = getattr(self.wrapped, name)

        # decorate callables to return AdapterTypes
        if callable(val):
            def sticky_wrapper(*args, **kwargs):
                result = val(*args, **kwargs)
                if isinstance(result, atomic.ScalarType):
                    result = self.replace(wrapped=result)
                elif isinstance(result, composite.CompositeType):
                    result = composite.CompositeType(
                        {self.replace(wrapped=t) for t in result}
                    )
                return result

            return sticky_wrapper

        # wrap properties as AdapterTypes
        if isinstance(val, atomic.ScalarType):
            val = self.replace(wrapped=val)
        elif isinstance(val, composite.CompositeType):
            val = composite.CompositeType({self.replace(wrapped=t) for t in val})

        return val

    def __hash__(self) -> int:
        return self.hash

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        valid = AdapterType.__subclasses__()
        if cls not in valid:
            raise TypeError(
                f"{cls.__name__} cannot inherit from another AdapterType "
                f"definition"
            )

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"

    def __str__(self) -> str:
        return self.slug
