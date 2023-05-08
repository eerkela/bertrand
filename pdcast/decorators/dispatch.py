"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
from collections import OrderedDict
import inspect
import itertools
from types import MappingProxyType
from typing import Any, Callable, Iterable
import warnings

import numpy as np
import pandas as pd

from pdcast import detect
from pdcast import resolve
from pdcast import types
from pdcast.util.structs import LRUDict
from pdcast.util.type_hints import type_specifier

from .base import BaseDecorator, no_default
from .wrapper import SeriesWrapper


# TODO: emit a warning whenever an implementation is replaced.


# TODO: None wildcard value?


# TODO: add a dropna flag to @dispatch


# TODO: series and dtype are managed by @extension_func.  If they are given as
# SeriesWrappers, then they will be added to an argument matrix (DataFrame)
# and distributed to the appropriate implementations.



# TODO: if dispatched implementation returns a scalar, treat it as an
# aggregation.  If it returns a SeriesWrapper, treat it as a
# transformation.  If it returns treat it as a filtration and remove
# the group from the pool.

# _dispatch_composite should just iterate through the GroupBy object directly,
# rather than passing through transform().  This means we aren't limited to
# a like-indexed result.


######################
####    PUBLIC    ####
######################


def dispatch(
    *args,
    drop_na: bool = True,
    cache_size: int = 64
) -> Callable:
    """A decorator that allows a Python function to dispatch to multiple
    implementations based on the type of its first argument.

    Parameters
    ----------
    *args : str
        Argument names to dispatch on.  Each of these must be reflected in the
        signature of the decorated function, and will be required for each of
        its overloaded implementations.
    drop_na : bool
        Indicates whether to drop missing values from dispatched arguments that
        are given as vectors.  If set to ``True``, then the input will be
        stripped of missing values before being passed to an overloaded
        implementation, which are safe to disregard them entirely.
    cache_size : int
        The number of signatures to store in the cache.

    Returns
    -------
    DispatchFunc
        A cooperative :class:`DispatchFunc` decorator, which manages dispatched
        implementations for the decorated callable.

    Raises
    ------
    TypeError
        If the decorated function does not accept the named arguments, or if no
        arguments are given.

    Notes
    -----
    :meth:`overloaded <pdcast.dispatch.overload>` implementations are searched
    from most specific to least specific, with ties broken from left to right.
    If no specific implementation can be found for the observed input, then the
    decorated function itself will be called as a generic implementation,
    similar to :func:`@functools.singledispatch <functools.singledispatch>`.

    If any of the dispatched arguments are to be vectorized, then they should
    be passed to this function as :class:`SeriesWrapper <pdcast.SeriesWrapper>`
    objects.  This can be handled automatically via the
    :func:`@extension_func <pdcast.extension_func>` decorator, which should
    always be placed above this one.  If any of these arguments contain mixed
    data, they will be grouped by type and dispatched independently via the
    ``pdcast`` :doc:`type system </content/types/types>`.
    """
    def decorator(func: Callable):
        """Convert a callable into a DispatchFunc object."""
        return DispatchFunc(
            func,
            args=args,
            drop_na=drop_na,
            cache_size=cache_size
        )

    return decorator


#######################
####    PRIVATE    ####
#######################


class DispatchDict(OrderedDict):
    """An :class:`OrderedDict <python:collections.OrderedDict>` that stores
    types and their dispatched implementations for :class:`DispatchFunc`
    operations.
    """

    def __init__(
        self,
        mapping: dict | None = None,
        cache_size: int = 64
    ):
        super().__init__()
        if mapping:
            for key, val in mapping.items():
                self[key] = val

        self._cache = LRUDict(maxsize=cache_size)
        self._ordered = False

    @classmethod
    def fromkeys(cls, iterable: Iterable, value: Any = None) -> DispatchDict:
        """Implement :meth:`dict.fromkeys` for DispatchDict objects."""
        return DispatchDict(super().fromkeys(iterable, value))

    def get(self, key: type_specifier, default: Any = None) -> Any:
        """Implement :meth:`dict.get` for DispatchDict objects."""
        try:
            return self[key]
        except KeyError:
            return default

    def pop(self, key: type_specifier, default: Any = no_default) -> Any:
        """Implement :meth:`dict.pop` for DispatchDict objects."""
        key = _resolve_key(key)
        try:
            result = self[key]
            del self[key]
            return result
        except KeyError as err:
            if default is no_default:
                raise err
            return default

    def setdefault(self, key: type_specifier, default: Callable = None) -> Any:
        """Implement :meth:`dict.setdefault` for DispatchDict objects."""
        key = _resolve_key(key)

        try:
            return self[key]
        except KeyError:
            self[key] = default
            return default

    def update(self, other) -> None:
        """Implement :meth:`dict.update` for DispatchDict objects."""
        super().update(DispatchDict(other))
        self._ordered = False

    def _validate_implementation(self, call: Callable) -> None:
        """Ensure that an overloaded implementation is valid."""
        if not callable(call):
            raise TypeError(
                f"overloaded implementation must be callable: {repr(call)}"
            )

    def reorder(self) -> None:
        """Sort the dictionary into topological order, with most specific
        keys first.
        """
        sigs = list(self)
        edges = {}

        # group edges by first node
        for edge in [(a, b) for a in sigs for b in sigs if _edge(a, b)]:
            edges.setdefault(edge[0], []).append(edge[1])

        # add signatures not contained in edges
        for sig in sigs:
            if sig not in edges:
                edges[sig] = []

        # sort according to edges
        for sig in _sort(edges):
            super().move_to_end(sig)

        self._ordered = True

    def __or__(self, other) -> DispatchDict:
        result = self.copy()
        result.update(other)
        return result

    def __ior__(self, other) -> DispatchDict:
        self.update(other)
        return self

    def __getitem__(self, key: type_specifier | tuple[type_specifier]) -> Any:
        key = _resolve_key(key)

        # trivial case: key has exact match
        if super().__contains__(key):
            return super().__getitem__(key)

        # sort dict
        if not self._ordered:
            self._cache.clear()
            self.reorder()

        # check for cached result
        if key in self._cache:
            return self._cache[key]

        # search for first (sorted) match that fully contains key
        for sig, val in self.items():
            if all(x.contains(y) for x, y in zip(sig, key)):
                self._cache[key] = val
                return val

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __setitem__(self, key: type_specifier, value: Callable) -> None:
        key = _resolve_key(key)
        self._validate_implementation(value)
        super().__setitem__(key, value)
        self._ordered = False

    def __delitem__(self, key: type_specifier) -> None:
        key = _resolve_key(key)

        # require exact match
        if super().__contains__(key):
            return super().__delitem__(key)

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __contains__(self, key: type_specifier):
        try:
            self.__getitem__(key)
            return True
        except KeyError:
            return False


class DispatchFunc(BaseDecorator):
    """A wrapper for the decorated callable that manages its dispatched
    implementations.

    Parameters
    ----------
    func : Callable
        The decorated function or other callable.

    Examples
    --------
    See the docs for :func:`@dispatch <pdcast.dispatch>` for example usage.
    """

    _reserved = (
        BaseDecorator._reserved |
        {"_args", "_dispatched", "_drop_na", "_signature"}
    )

    def __init__(
        self,
        func: Callable,
        args: list[str],
        drop_na: bool,
        cache_size: int
    ):
        super().__init__(func=func)
        self._drop_na = bool(drop_na)
        if not args:
            raise TypeError(
                f"'{func.__qualname__}' must dispatch on at least one argument"
            )

        # validate args
        self._signature = inspect.signature(func)
        bad = [a for a in args if a not in self._signature.parameters]
        if bad:
            raise TypeError(f"argument not recognized: {bad}")
        self._args = args

        # init dispatch table
        self._dispatched = DispatchDict(cache_size=cache_size)

    @property
    def dispatched(self) -> MappingProxyType:
        """A mapping from :doc:`types </content/types/types>` to their
        dispatched implementations.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping types to their associated dispatch
            functions.

        Notes
        -----
        The mapping that is returned by this method is sorted according to the
        order in which implementations are searched when dispatching is
        performed.  If no match is found for any of the 

        Examples
        --------
        .. doctest::

            >>> @dispatch
            ... def foo(bar):
            ...     print("generic implementation")
            ...     return bar

            >>> @foo.overload("int")
            ... def integer_foo(bar):
            ...     print("integer implementation")
            ...     return bar

            >>> foo.dispatched

        .. TODO: check
        """
        return MappingProxyType(self._dispatched)

    def overload(self, *args, **kwargs) -> Callable:
        """A decorator that transforms a naked function into a dispatched
        implementation for this :class:`DispatchFunc`.

        Parameters
        ----------
        types : type_specifier | Iterable[type_specifier] | None, default None
            The type(s) to dispatch to this implementation.  See notes for
            handling of :data:`None <python:None>`.

        Returns
        -------
        Callable
            The original undecorated function.

        Raises
        ------
        TypeError
            If the decorated function is not callable with at least one
            argument.

        Notes
        -----
        This decorator works just like the :meth:`register` method of
        :func:`singledispatch <python:functools.singledispatch>` objects,
        except that it does not interact with type annotations in any way.
        Instead, if a type is not provided as an argument to this method, it
        will be disregarded during dispatch lookups, unless the decorated
        callable is a method of an :class:`AtomicType <pdcast.AtomicType>` or
        :class:`AdapterType <pdcast.AdapterType>` subclass.  In that case, the
        attached type will be automatically bound to the dispatched
        implementation during
        :meth:`__init_subclass__() <AtomicType.__init_subclass__>`.

        Examples
        --------
        See the :func:`dispatch` :ref:`API docs <dispatch.dispatched>` for
        example usage.
        """

        def implementation(call: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            # validate func accepts dispatched arguments
            sig = inspect.signature(call)
            missing = [a for a in self._args if a not in sig.parameters]
            if missing:
                call_name = f"'{call.__module__}.{call.__qualname__}()'"
                raise TypeError(
                    f"{call_name} must accept dispatched arguments: {missing}"
                )

            # bind signature
            try:
                bound = sig.bind_partial(*args, **kwargs).arguments
            except TypeError as err:
                call_name = f"'{call.__module__}.{call.__qualname__}()'"
                reconstructed = [
                    ", ".join(repr(v) for v in args),
                    ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
                ]
                reconstructed = ", ".join(s for s in reconstructed if s)
                err_msg = (
                    f"invalid signature for {call_name}: ({reconstructed})"
                )
                raise TypeError(err_msg) from err

            # parse overloaded signature
            paths = []
            missing = []
            for name in self._args:
                if name in bound:
                    paths.append(bound.pop(name))
                else:
                    missing.append(name)
            if missing:
                raise TypeError(f"no signature given for argument: {missing}")
            if bound:
                raise TypeError(
                    f"signature contains non-dispatched arguments: "
                    f"{list(bound)}"
                )

            # generate dispatch keys
            paths = [resolve.resolve_type([spec]) for spec in paths]
            paths = list(itertools.product(*paths))

            # merge with dispatch table
            existing = dict(self._dispatched)
            for path in paths:
                previous = existing.get(path, None)
                # if previous:
                #     warn_msg = (
                #         f"Replacing '{previous.__qualname__}()' with "
                #         f"'{call.__qualname__}()' for signature "
                #         f"{tuple(str(x) for x in path)}"
                #     )
                #     warnings.warn(warn_msg, UserWarning, stacklevel=2)
                self._dispatched[path] = call

            return call

        return implementation

    def generic(
        self,
        *args,
        **kwargs
    ) -> pd.Series | SeriesWrapper:
        """A reference to the generic implementation of the decorated function.
        """
        return self.__wrapped__(*args, **kwargs)

    def _dispatch_scalar(
        self,
        series: SeriesWrapper,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        """Dispatch a homogenous series to the correct implementation."""
        series_type = series.element_type

        # rectify series
        series = series.rectify()

        # bind *args, **kwargs
        bound = self._signature.bind(series, *args, **kwargs)
        bound.apply_defaults()
        key = (series_type,)
        key += tuple(
            detect.detect_type(bound.arguments[param])
            for param in self._args[1:]
        )

        # search for a dispatched implementation
        try:
            implementation = self._dispatched[key]
            result = implementation(*bound.args, **bound.kwargs)
        except KeyError:
            result = None

        # recursively unwrap adapters and retry.
        if result is None:
            # NOTE: This operates like a recursive stack.  Adapters are popped
            # off the stack in FIFO order before recurring, and then each
            # adapter is pushed back onto the stack in the same order.
            for before in getattr(series_type, "adapters", ()):
                series = before.inverse_transform(series)
                series = self._dispatch_scalar(series, *args, **kwargs)
                if (
                    self._wrap_adapters and
                    series.element_type == before.wrapped
                ):
                    series = before.transform(series)
                return series

        # fall back to generic implementation
        if result is None:
            result = self.__wrapped__(**bound.arguments)

        # ensure result is a SeriesWrapper
        if not isinstance(result, SeriesWrapper):
            raise TypeError(
                f"dispatched implementation of {self.__wrapped__.__name__}() "
                f"did not return a SeriesWrapper for type: "
                f"{series_type}"
            )

        # ensure final index is a subset of original index
        if not result.index.difference(series.index).empty:
            raise RuntimeError(
                f"index mismatch in {self.__wrapped__.__name__}(): dispatched "
                f"implementation for type {series_type} must return a series "
                f"with the same index as the original"
            )

        return result.rectify()

    def _dispatch_composite(
        self,
        series: SeriesWrapper,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        """Dispatch a mixed-type series to the appropriate implementation."""
        from pdcast import convert

        groups = series.series.groupby(
            series.element_type.index,
            dropna=False,  # no NAs to drop
            sort=False
        )

        # NOTE: SeriesGroupBy.transform() cannot reconcile mixed int64/uint64
        # arrays, and will attempt to convert them to float.  To avoid this, we
        # keep track of result.dtype.  If it is signed/unsigned and opposite
        # has been observed, we convert the result to dtype=object and
        # reconsider afterwards.
        observed = set()
        check_uint = [False]  # using a list avoids UnboundLocalError
        signed = types.SignedIntegerType
        unsigned = types.UnsignedIntegerType

        def transform(grp) -> pd.Series:
            """Groupwise transformation."""
            grp = SeriesWrapper(
                grp,
                hasnans=series.hasnans,
                element_type=grp.name
            )
            result = self._dispatch_scalar(grp, *args, **kwargs)

            # check for int64/uint64 conflict
            # NOTE: This is a bit complicated, but it effectively invalidates
            # the check_uint flag if any type other than pure signed/unsigned
            # integers are detected as results.  In these cases, our final
            # result will be dtype: object anyway, so there's no point
            # following through with the check.
            if result.element_type.is_subtype(signed):
                if any(o.is_subtype(unsigned) for o in observed):
                    result.series = result.series.astype(object, copy=False)
                    check_uint[0] = None if check_uint[0] is None else True
            elif result.element_type.is_subtype(unsigned):
                if any(x.is_subtype(signed) for x in observed):
                    result.series = result.series.astype(object, copy=False)
                    check_uint[0] = None if check_uint[0] is None else True
            else:
                check_uint[0] = None

            observed.add(result.element_type)
            return result.series  # transform() expects a Series output

        # apply transformation
        result = groups.transform(transform)

        # resolve signed/unsigned conflict
        if check_uint[0]:
            # attempt conversion to uint64
            target = unsigned.make_nullable() if series.hasnans else unsigned
            try:
                result = convert.cast(
                    result,
                    dtype=target,
                    downcast=kwargs.get("downcast", None),
                    errors="raise"
                )
            except OverflowError:
                pass  # keep as dtype: object

        # re-wrap result
        return SeriesWrapper(result, hasnans=series.hasnans)





    def _normalize(
        self,
        dispatched: dict[str, Any]
    ) -> tuple[CompositeInput | dict, pd.Index]:
        """Normalize vectorized inputs to this :class:`DispatchFunc`.

        This method converts input vectors into :class:`SeriesWrappers` with
        homogenous indices, missing values, and element types.  Composite
        vectors will be grouped along with their homogenous counterparts and
        processed as independent frames.

        Notes
        -----
        Normalization is skipped if pre-wrapped :class:`SeriesWrappers` are
        provided as input.  This is intended to short-circuit the normalization
        machinery for internal (recursive) calls.
        """
        normalized = True

        # extract vectors
        vectors = {}
        names = {}
        for arg, value in dispatched.items():
            if isinstance(value, SeriesWrapper):
                vectors[arg] = value.series
                names[arg] = value.name
            elif isinstance(value, pd.Series):
                normalized = False
                vectors[arg] = value
                names[arg] = value.name
            elif isinstance(value, np.ndarray):
                normalized = False
                vectors[arg] = value
            else:
                value = np.asarray(value, dtype=object)
                if value.shape:
                    normalized = False
                    vectors[arg] = value

        # fastpath for pre-normalized/scalar inputs
        if normalized:
            return dispatched, None

        # bind vectors into DataFrame
        frame = pd.DataFrame(vectors)

        # normalize indices
        original_index = frame.index
        if not isinstance(original_index, pd.RangeIndex):
            frame.index = pd.RangeIndex(0, frame.shape[0])
        if any(v.shape[0] != original_index.shape[0] for v in vectors.values()):
            warn_msg = f"index mismatch in {self.__qualname__}()"
            warnings.warn(warn_msg, UserWarning, stacklevel=2)

        # drop missing values
        hasnans = None
        if self._drop_na:
            frame = frame.dropna(how="any")
            hasnans = frame.shape[0] < original_index.shape[0]

        # group composites
        detected = detect.detect_type(frame)
        if any(isinstance(v, types.CompositeType) for v in detected.values()):
            frame_generator = CompositeInput(
                dispatched=dispatched,
                frame=frame,
                detected=detected,
                hasnans=hasnans
            )
            return frame_generator, original_index

        # split into SeriesWrappers
        for col, series in frame.items():
            dispatched[col] = SeriesWrapper(
                series.rename(names.get(col, None)),
                hasnans=hasnans,
                element_type=detected[col]
            )

        return dispatched, original_index

    def _mixed_data(
        self,
        bound: inspect.BoundArguments,
        frame_generator: CompositeInput,
        original_index: pd.Index,
    ) -> Any:
        """Dispatch mixed data to the correct implementation and combine
        results.
        """
        mode = None

        results = []
        for dispatched, group_index in frame_generator:
            bound.arguments = {**bound.arguments, **dispatched}

            # generate dispatch key
            detected = {k: detect.detect_type(v) for k, v in dispatched.items()}
            key = tuple(detected[k] for k in self._args)

            # search for dispatched implementation
            try:
                implementation = self._dispatched[key]
                result = implementation(*bound.args, **bound.kwargs)
            except KeyError:  # fall back to generic
                result = self.__wrapped__(*bound.args, **bound.kwargs)

            # infer mode of operation from return type
            if result is None or isinstance(result, SeriesWrapper):
                op = "transform"
            else:
                op = "aggregate"
            if mode is not None and op != mode:
                # TODO: make error message clearer
                raise RuntimeError(
                    f"Mixed signals for composite {self.__qualname__}()"
                    f" operation: {key}"
                )
            mode = op

            # finalize result
            result = self._finalize(
                result,
                original_index=group_index,
                key=key
            )

            # remember result
            results.append((detected, result))

        # concatenate results
        if mode == "transform":
            # TODO: handle index gaps created by filtration
            final = pd.concat([res for _, res in results if res is not None])
            final.sort_index()
            final.index = original_index
            return final

        # mode = "aggregate"
        results = [
            grp | {f"{self.__qualname__}()": res} for grp, res in results
        ]
        final = pd.concat([pd.DataFrame(res, index=[0]) for res in results])
        return final

    def _finalize(
        self,
        result: Any,
        original_index: pd.Index,
        key: tuple
    ) -> Any:
        """Infer operation mode based on return type of dispatched
        implementation.
        """
        # transform
        if isinstance(result, SeriesWrapper):
            # warn if final index is not a subset of original
            if not result.index.difference(original_index).empty:
                warn_msg = (
                    f"index mismatch in {self.__qualname__}() with signature"
                    f"{tuple(str(x) for x in key)}: dispatched implementation "
                    f"did not return a like-indexed SeriesWrapper"
                )
                warnings.warn(warn_msg, UserWarning, stacklevel=4)

            # replace missing values, aligning on index
            final = pd.Series(
                np.full(
                    original_index.shape[0],
                    getattr(result.element_type, "na_value", pd.NA),
                    dtype=object
                )
            )
            final.update(result.series)
            final = final.astype(result.dtype)

            # replace original index
            final.index = original_index
            return final

        # filter/aggregate
        return result

    def __call__(self, *args, **kwargs) -> Any:
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.

        Notes
        -----
        This automatically detects aggregations, transformations, and
        filtrations based on the return value.

            *   :data:`None <python:None>` signifies a filtration.  The passed
                values will be excluded from the resulting series.
            *   A :class:`SeriesWrapper <pdcast.SeriesWrapper>` signifies a
                transformation.  Any missing indices will be replaced with NA.
            *   Anything else signifies an aggreggation.  Its result will be
                returned as-is if data is homogenous.  If mixed data is given,
                This will be a DataFrame with rows for each group.

        """
        # bind signature
        bound = self._signature.bind(*args, **kwargs)
        bound.apply_defaults()

        # extract dispatched args
        dispatched = {arg: bound.arguments[arg] for arg in self._args}

        # normalize vectors
        dispatched, original_index = self._normalize(dispatched)

        # handle composite data
        if isinstance(dispatched, CompositeInput):
            return self._mixed_data(
                bound=bound,
                frame_generator=dispatched,
                original_index=original_index
            )

        # continue with homogenous inputs
        bound.arguments = {**bound.arguments, **dispatched}

        # generate dispatch key
        detected = {k: detect.detect_type(v) for k, v in dispatched.items()}
        key = tuple(detected[k] for k in self._args)

        # search for dispatched implementation
        try:
            implementation = self._dispatched[key]
            result = implementation(*bound.args, **bound.kwargs)
        except KeyError:  # fall back to generic
            result = self.__wrapped__(*bound.args, **bound.kwargs)

        # if outermost call, finalize result
        if original_index is not None:
            return self._finalize(
                result,
                original_index=original_index,
                key=key
            )

        # pass back to internal context
        return result

    def __getitem__(self, key: type_specifier) -> Callable:
        """Get the dispatched implementation for objects of a given type.

        This method searches the implementation space being managed by this
        :class:`DispatchFunc`.  It always returns the same implementation that
        is used when the function is invoked.
        """
        # search for a dispatched implementation
        try:
            return self._dispatched[key]
        except KeyError:
            return self.__wrapped__

        # TODO: maybe adapter unwrapping should be implemented in generic
        # implementation?  Either that or convert @dispatch wrap_adapters into
        # ignore_adapters.  Or just use **kwargs for contains() operations.

        # # unwrap adapters  # TODO: breaks multiple dispatch
        # for _ in key_type.adapters:
        #     return self[key_type.wrapped]  # recur

        # return generic
        return self.__wrapped__

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an implementation from the pool.
        """
        self._dispatched.__delitem__(key)

    # @property
    # def __doc__(self) -> str:
    #     """Run time docstring."""
    #     # header
    #     doc = (
    #         f"A wrapper for {self.__module__}.{self.__qualname__}() that\n"
    #         f"dispatches to one of the following virtual methods based on the\n"
    #         f"type of its "
    #     )
    #     if self._depth == 1:
    #         doc += f"'{self._args[0]}' argument (in order):\n\n"
    #     else:
    #         doc += f"{self._args} arguments (in order):\n\n"

    #     # dispatch table
    #     if self._depth == 1:
    #         docmap = {
    #             str(k[0]): f"{v.__module__}.{v.__qualname__}()"
    #             for k, v in self.dispatched.items()
    #         }
    #     else:
    #         docmap = {
    #             tuple(str(x) for x in k): f"{v.__module__}.{v.__qualname__}()"
    #             for k, v in self.dispatched.items()
    #         }
    #     docmap["..."] = f"{self.__module__}.{self.__qualname__}.generic()"
    #     sep = "\n    "
    #     doc += (
    #         f"{{"
    #         f"{sep}{sep.join(f'{k}: {v}' for k, v in docmap.items())}\n"
    #         f"}}\n\n"
    #     )

    #     # footer
    #     doc += (
    #         "The map above is available under this function's ``.dispatched``\n"
    #         "attribute, and individual implementations can be queried using\n"
    #         "normal dictionary indexing:\n\n"
    #     )
    #     query = ", ".join("int" for _ in range(self._depth))
    #     doc += (
    #         f">>> {self.__qualname__}[{query}]\n"
    #         f"{self[query]}\n"
    #     )
    #     return doc


#######################
####    PRIVATE    ####
#######################


class CompositeInput:
    """An iterator that yields successive groups in the case of mixed-type
    data.
    """

    def __init__(
        self,
        dispatched: dict,
        frame: pd.DataFrame,
        detected: dict,
        hasnans: bool
    ):
        self.dispatched = dispatched
        type_frame = pd.DataFrame({
            k: getattr(v, "index", v) for k, v in detected.items()
        })
        grouped = type_frame.groupby(list(detected), sort=False)
        self.groups = grouped.groups
        self.frame = frame
        self.hasnans = hasnans

    def __iter__(self):
        for key, indices in self.groups.items():
            # extract group
            group = self.frame.iloc[indices]

            # split into normalized vectors
            vectors = {}
            for idx, col in enumerate(group.columns):
                vectors[col] = SeriesWrapper(
                    group[col],
                    hasnans=self.hasnans,
                    element_type=key[idx]
                )

            # yield successive subsets
            yield ({**self.dispatched, **vectors}, indices)


def _resolve_key(key: type_specifier | tuple[type_specifier]) -> tuple:
    """Convert arbitrary type specifiers into a valid key for DispatchDict
    lookups.
    """
    if not isinstance(key, tuple):
        key = (key,)

    key_type = []
    for spec in key:
        spec_type = resolve.resolve_type(spec)
        if isinstance(spec_type, types.CompositeType):
            raise TypeError(f"key must not be composite: {repr(spec)}")
        key_type.append(spec_type)

    return tuple(key_type)


def _supercedes(sig1: tuple, sig2: tuple) -> bool:
    """Check if sig1 is consistent with and strictly more specific than sig2.
    """
    return all(x.contains(y) for x, y in zip(sig2, sig1))


def _edge(sig1: tuple, sig2: tuple) -> bool:
    """If ``True``, check sig1 before sig2.

    Ties are broken by recursively backing off the last element of both
    signatures.  As a result, whichever one is more specific in its earlier
    elements will always be preferred.
    """
    # pylint: disable=arguments-out-of-order
    if not sig1 or not sig2:
        return False

    return (
        _supercedes(sig1, sig2) and
        not _supercedes(sig2, sig1) or _edge(sig1[:-1], sig2[:-1])
    )


def _sort(edges: dict) -> list:
    """Topological sort algorithm by Kahn (1962).

    Parameters
    ----------
    edges : dict
        a dict of the form {A: {B, C}} where B and C depend on A

    Returns
    -------
    list
        an ordered list of nodes that satisfy the dependencies of edges

    Examples
    --------
    >>> _sort({1: (2, 3), 2: (3, )})
    [1, 2, 3]

    References
    ----------    
    Kahn, Arthur B. (1962), "Topological sorting of large networks",
    Communications of the ACM
    """
    # invert edges: {A: {B, C}} -> {B: {A}, C: {A}}
    inverted = OrderedDict()
    for key, val in edges.items():
        for item in val:
            inverted[item] = inverted.get(item, set()) | {key}

    # Proceed with Kahn topological sort algorithm
    S = OrderedDict.fromkeys(v for v in edges if v not in inverted)
    L = []
    while S:
        n, _ = S.popitem()
        L.append(n)
        for m in edges.get(n, ()):
            assert n in inverted[m]
            inverted[m].remove(n)
            if not inverted[m]:
                S[m] = None

    # check for cycles
    if any(inverted.get(v, None) for v in edges):
        cycles = [v for v in edges if inverted.get(v, None)]
        raise ValueError(f"edges are cyclic: {cycles}")

    return L




# def _consistent(sig1: tuple, sig2: tuple) -> bool:
#     """Check for an overlap between two signatures.

#     If this returns ``True``, then it is possible for a signature to satisfy
#     both sig1 and sig2.
#     """
#     # check for empty signatures
#     if not sig1:
#         return not sig2
#     if not sig2:
#         return not sig1

#     # lengths match
#     return all(x.contains(y) or y.contains(x) for x, y in zip(sig1, sig2))


# def _ambiguous(sig1: tuple, sig2: tuple) -> bool:
#     """Signatures are consistent, but neither is strictly more specific.
#     """
#     return (
#         _consistent(sig1, sig2) and
#         not (_supercedes(sig1, sig2) or _supercedes(sig2, sig1))
#     )


# def ambiguities(signatures):
#     """ All signature pairs such that A is ambiguous with B """
#     signatures = list(map(tuple, signatures))
#     return set((a, b) for a in signatures for b in signatures
#                if hash(a) < hash(b)
#                and ambiguous(a, b)
#                and not any(supercedes(c, a) and supercedes(c, b)
#                            for c in signatures))




# TODO: this only sort of works for composite data.  There appears to be an
# index mismatch or something else that injects missing values.


@dispatch("bar", "baz")
def add(bar, baz):
    """doc for foo()"""
    return bar + baz


@add.overload("int", "int")
def integer_add(bar, baz):
    print("integer case")
    return bar - baz



print(add([1, 2, 3], [1, True, 1.0]))






# @foo.overload("int8", "int")
# def int8_foo(bar, baz):
#     print("int8")
#     return bar


# @foo.overload("int", "int8")
# def integer_foo(bar, baz):
#     print("int")
#     return bar



# @foo.overload("bool", "bool")
# def boolean_foo(bar, baz):
#     print("boolean")
#     return bar


# @foo.overload("int8[numpy]")
# def numpy_int8_foo(bar, baz):
#     print("np.int8")
#     return bar
