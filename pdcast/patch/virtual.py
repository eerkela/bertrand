from __future__ import annotations
from functools import partial
import inspect
import json
from typing import Any, Callable

import numpy as np
import pandas as pd

from pdcast.convert import SeriesWrapper
import pdcast.convert.standalone as standalone
from pdcast.resolve import resolve_type
from pdcast.types import (
    CompositeType, ObjectType, PandasUnsignedIntegerType, SignedIntegerType,
    UnsignedIntegerType
)

import pdcast.util.array as array


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
        docmap = {str(k): v.__qualname__ for k, v in submap.items()}
        docmap = dict(sorted(docmap.items()))
        if self.namespace:
            docmap["..."] = "Series.{self.namespace.name}.{self.name}"
        else:
            docmap["..."] = "Series.{self.name}"
        self.__doc__ = (
            f"A wrapper that applies custom logic for one or more data "
            f"types:\n"
            f"{json.dumps(docmap, indent=4)}\n\n"
            f"The above map is available under this method's `.dispatched`\n"
            f"attribute, and the original pandas implementation (if one "
            f"exists)\n can be recovered under its `.original` attribute."
        )

    @property
    def original(self) -> Callable:
        """Retrieve the original pandas implementation."""
        if self.namespace:
            return getattr(self.namespace.original, self.name)
        return partial(getattr(pd.Series, self.name), self.data)

    def _dispatch(self, series: SeriesWrapper, *args, **kwargs):
        if isinstance(series.element_type, CompositeType):
            return self._dispatch_composite(series, *args, **kwargs)
        return self._dispatch_scalar(series, *args, **kwargs)

    def _dispatch_scalar(
        self,
        series: SeriesWrapper,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        # use dispatched implementation if it is defined
        dispatched = self.dispatched.get(type(series.element_type), None)
        if dispatched is not None:
            result = dispatched(series.element_type, series, *args, **kwargs)
            target = result.element_type.dtype
            if (
                result.dtype == np.dtype("O") and
                isinstance(target, array.AbstractDtype)
            ):
                result.series = result.series.astype(target)
            return result

        # unwrap adapters and retry.  NOTE: this is recursive
        for _ in series.element_type.adapters:
            orig_type = series.element_type
            series = orig_type.unwrap(series)
            series = self._dispatch_scalar(series, *args, **kwargs)
            if self.wrap_adapters and orig_type.wrapped == series.element_type:
                series = orig_type.wrap(series)
            return series

        # fall back to pandas
        pars = inspect.signature(self.original).parameters
        kwargs = {k: v for k, v in kwargs.items() if k in pars}
        result = SeriesWrapper(
            self.original(*args, **kwargs),
            hasnans=series._hasnans
        )
        target = result.element_type.dtype
        if (
            result.dtype == np.dtype("O") and
            isinstance(target, array.AbstractDtype)
        ):
            result.series = result.series.astype(target)
        return result

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

    def __call__(self, *args, **kwargs) -> pd.Series:
        with SeriesWrapper(self.data) as series:
            result = self._dispatch(series, *args, **kwargs)
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type
        return series.series
