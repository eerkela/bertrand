from __future__ import annotations
from functools import partial, update_wrapper
import inspect
from typing import Any, Callable

import pandas as pd

from pdcast.convert import SeriesWrapper
import pdcast.convert.standalone as standalone
from pdcast.resolve import resolve_type
from pdcast.types import (
    CompositeType, ObjectType, PandasUnsignedIntegerType, SignedIntegerType,
    UnsignedIntegerType
)


# TODO: SeriesExtensionMethod seems to have an argument mismatch.
# -> the issue is passing self to self._ext_func in __call__.  This doesn't
# give a pandas Series, it gives the ExtensionMethod itself.
# -> probably have to use a decorator function here rather than a class.  Just
# attach __getattr__, etc to the function.
# -> maybe inherit from functools.partial?


# ignore this file when doing string-based object lookups in resolve_type()
_ignore_frame_objects = True


class Namespace:
    """A mock accessor in the style of `pd.Series.dt`/`pd.Series.str` that
    holds @dispatch methods that are being applied to pd.Series objects.

    These objects are created dynamically through the optional `namespace`
    keyword argument of the @dispatch decorator.  
    """

    def __init__(
        self,
        data: pd.Series,
        name: str,
        submap: dict
    ):
        self.data = data
        self.name = name
        self.dispatched = submap

    @property
    def original(self):
        return getattr(pd.Series, self.name)(self.data)

    def __getattr__(self, name: str) -> Any:
        # check if attribute is being dispatched from this namespace
        if name in self.dispatched:
            return DispatchMethod(
                self.data,
                name=name,
                submap = self.dispatched[name],
                namespace=self
            )

        # fall back to pandas
        original = super().__getattribute__("original")
        return getattr(original, name)


class DispatchMethod:
    """This can be invoked in one of two ways.  1) naked, without a namespace.
    2) from a namespace
    """

    def __init__(
        self,
        data: pd.Series,
        name: str,
        submap: dict,
        namespace: Namespace = None,
        wrap_adapters: bool = True
    ):
        self.data = data
        self.name = name
        self.dispatched = submap
        self.namespace = namespace
        self.wrap_adapters = wrap_adapters

        # create appropriate docstring
        docmap = {k.__name__: v.__qualname__ for k, v in submap.items()}
        docmap = dict(sorted(docmap.items()))
        if self.namespace:
            docmap["..."] = f"Series.{self.namespace.name}.{self.name}"
        else:
            docmap["..."] = f"Series.{self.name}"
        sep = "\n    "
        self.__doc__ = (
            f"A wrapper that applies custom logic for one or more data "
            f"types:\n"
            f"{{{sep}{sep.join(f'{k}: {v}' for k, v in docmap.items())}\n}}"
            f"\n\n"
            f"The above map is available under this method's `.dispatched`\n"
            f"attribute, and the original pandas implementation (if one "
            f"exists)\n can be recovered under its `.original` attribute."
        )

    def __call__(self, *args, **kwargs) -> pd.Series:
        """Call the DispatchMethod, invoking the appropriate implementation for
        the observed data.
        """
        # normalize index, remove NAs, and infer type via SeriesWrapper
        with SeriesWrapper(self.data) as series:
            if isinstance(series.element_type, CompositeType):
                result = self._dispatch_composite(series, *args, **kwargs)
            else:
                result = self._dispatch_scalar(series, *args, **kwargs)

            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type

        # return modified series with original index, NAs
        return series.series

    @property
    def original(self) -> Callable:
        """Retrieve the original pandas implementation for this method, if one
        exists.
        """
        # namespace holds data for us, so all we need to do is getattr
        if self.namespace:
            return getattr(self.namespace.original, self.name)

        # we can get original implementation off the pd.Series class itself,
        # though this requires us to bind data manually.
        return partial(getattr(pd.Series, self.name), self.data)

    def _dispatch_scalar(
        self,
        series: SeriesWrapper,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        """Apply the dispatched method over a homogenous input series.

        Parameters
        ----------
        series : SeriesWrapper
            A homogenous input series, wrapped as a :class:`SeriesWrapper`.
            This is required as the first argument of any method that is
            decorated with :func:`@dispatch() <dispatch>`.
        *args, **kwargs
            Arbitrary arguments to be passed to the dispatched implementation.

        Returns
        -------
        SeriesWrapper
            The result of the dispatched method's specific implementation,
            wrapped as a :class:`SeriesWrapper`.

        Notes
        -----
        The way this works is somewhat complicated.  In general, there are 3
        steps to a dispatching problem:

            #)  Using the inferred type of the series, check for a
                corresponding :func:`@dispatch() <dispatch>` implementation in
                this method's ``submap``.  If one is found, use it and stop
                here.
            #)  If the inferred type has adapters, progressively strip them and
                repeat the above check.  This is done recursively, down to the
                base :class:`AtomicType` if necessary, stopping at the first
                match.
            #)  If no match is found for this type or any of its adapters,
                default to the pandas implementation.

        There is an optional 4th step if adapters were detected in step 2),
        which is to progressively re-wrap the dispatched result with any
        adapters that are above it in the stack.  This step is automatically
        skipped if :attr:`DispatchMethod.wrap_adapters` is set to ``False``, or
        if the resulting series has a different type than the starting one.
        """
        series_type = series.element_type

        # 1) check for dispatched implementation.
        dispatched = self.dispatched.get(type(series_type), None)
        if dispatched is not None:
            result = dispatched(series_type, series, *args, **kwargs)
            return result.rectify()

        # 2) recursively unwrap adapters and retry.  NOTE: This resembles a
        # stack.  An adapter is popped off the stack in order before recurring,
        # and then each adapter is pushed back onto the stack in the same order.
        for _ in getattr(series_type, "adapters", ()):
            series = series_type.inverse_transform(series)
            series = self._dispatch_scalar(series, *args, **kwargs)
            if (
                self.wrap_adapters and
                series.element_type == series_type.wrapped
            ):
                series = series_type.transform(series)
            return series

        # 3) fall back to pandas.  NOTE: we filter off any keyword arguments
        # that do not appear in the original implementation's signature/
        pars = inspect.signature(self.original).parameters
        kwargs = {k: v for k, v in kwargs.items() if k in pars}
        result = SeriesWrapper(
            self.original(*args, **kwargs),
            hasnans=series._hasnans
        )
        return result.rectify()

    def _dispatch_composite(
        self,
        series: SeriesWrapper,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        groups = series.series.groupby(series.element_type.index, sort=False)

        # NOTE: SeriesGroupBy.transform() cannot reconcile mixed int64/uint64
        # arrays, and will attempt to convert them to float.  To avoid this, we
        # keep track of result.dtype.  If it is int64/uint64-like and opposite
        # has been observed, convert to dtype=object and reconsider afterwards.
        observed = []
        check_uint = [False]  # UnboundLocalError if this is a scalar

        def transform(grp):
            scalar = DispatchMethod(
                grp,
                name=self.name,
                submap=self.dispatched,
                namespace=self.namespace
            )
            grp = SeriesWrapper(
                grp,
                hasnans=series.hasnans,
                element_type=grp.name
            )
            result = scalar._dispatch_scalar(grp, *args, **kwargs)

            # ensure final index is a subset of original index
            if not result.series.index.difference(grp.series.index).empty:
                raise RuntimeError(
                    f"index mismatch: output index must be a subset of input "
                    f"index for group {repr(str(grp.element_type))}"
                )

            # check for int64/uint64 conflict
            obs = resolve_type(result.series.dtype)
            if obs.is_subtype(SignedIntegerType):
                if any(x.is_subtype(UnsignedIntegerType) for x in observed):
                    obs = resolve_type(ObjectType)
                    result.series = result.series.astype("O")
                    check_uint[0] = None if check_uint[0] is None else True
            elif obs.is_subtype(UnsignedIntegerType):
                if any(x.is_subtype(SignedIntegerType) for x in observed):
                    obs = resolve_type(ObjectType)
                    result.series = result.series.astype("O")
                    check_uint[0] = None if check_uint[0] is None else True
            else:
                check_uint[0] = None
            observed.append(obs)

            return result.series

        result = groups.transform(transform)
        if check_uint[0]:
            if series.hasnans:
                target = resolve_type(PandasUnsignedIntegerType)
            else:
                target = resolve_type(UnsignedIntegerType)
            try:
                result = standalone.to_integer(
                    result,
                    dtype=target,
                    downcast=kwargs.get("downcast", None),
                    errors="raise"
                )
            except OverflowError:
                pass
        return SeriesWrapper(result, hasnans=series._hasnans)


class ExtensionMethod:
    """An interface for a :class:`ExtensionFunc` object that implicitly inserts
    a class instance as the first argument while retaining full attribute
    access.

    Parameters
    ----------
    instance : Any
        An instance to be inserted.  This is typically inserted from
        :meth:`VirtualMethod.__get__`, using Python's descriptor protocol.
    ext_func : Callable
        Usually an :class:`ExtensionFunc` object, but can be any Python
        callable.
    ext_name : str
        The name to use when ``repr()`` is called on this object.  Setting this
        to something like ``"pandas.Series.cast"`` makes the output a bit
        cleaner.

    Notes
    -----
    This is meant to be used in combination with a :class:`VirtualMethod`
    descriptor.  Users should never need to instantiate one themselves.
    """

    def __init__(self, instance: Any, ext_func: Callable, ext_name: str):
        self._ext_func = ext_func
        self._ext_name = ext_name
        self._instance = instance
        update_wrapper(self, ext_func)

    def __getattr__(self, name: str) -> Any:
        return getattr(self.__dict__["_ext_func"], name)

    def __setattr__(self, name: str, value: Any) -> None:
        if name in {"_ext_func", "_ext_name", "_instance"}:
            self.__dict__[name] = value
        else:
            setattr(self.__dict__["_ext_func"], name, value)

    def __delattr__(self, name: str) -> None:
        delattr(self.__dict__["_ext_func"], name)

    def __iter__(self):
        return iter(self._ext_func)

    def __len__(self) -> int:
        return len(self._ext_func)

    def __call__(self, *args, **kwargs):
        return self._ext_func(self._instance, *args, **kwargs)

    def __repr__(self) -> str:
        sig = self._ext_func._reconstruct_signature()
        return f"{self._ext_name}({', '.join(sig[1:])})"


class VirtualMethod:
    """A descriptor that can be added to an arbitrary Python class to enable
    :class:`ExtensionFuncs <ExtensionFunc>` to act as instance methods.

    Parameters
    ----------
    ext_func : Callable
    """

    def __init__(self, ext_func: ExtensionFunc, ext_name: str = None):
        update_wrapper(self, ext_func)
        self._ext_func = ext_func
        if ext_name is None:
            ext_name = self._ext_func.__name__
        self._ext_name = ext_name

    def __get__(self, instance, owner=None) -> Callable:
        return VirtualMethod(instance, self._ext_func, self._ext_name)
