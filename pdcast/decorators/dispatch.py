"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
from collections import OrderedDict
import inspect
import itertools
import threading
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


# TODO: emit a warning whenever an implementation is replaced.

# TODO: None wildcard value?

# TODO: result is None -> fill with NA?


######################
####    PUBLIC    ####
######################


def dispatch(
    *args,
    drop_na: bool = True,
    cache_size: int = 64
) -> Callable:
    """A decorator that allows a Python function to dispatch to multiple
    implementations based on the type of its arguments.

    Parameters
    ----------
    *args : str
        Argument names to dispatch on.  Each of these must be reflected in the
        signature of the decorated function, and will be required for each of
        its overloaded implementations.
    drop_na : bool
        Indicates whether to drop missing values from input vectors.  If set to
        ``True``, then each vector will be stripped of missing values before
        being passed to the dispatched implementation.
    cache_size : int
        The maximum number of signatures to store in cache.

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
        key = resolve_key(key)
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
        key = resolve_key(key)
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
        for _edge in [(a, b) for a in sigs for b in sigs if edge(a, b)]:
            edges.setdefault(_edge[0], []).append(_edge[1])

        # add signatures not contained in edges
        for sig in sigs:
            if sig not in edges:
                edges[sig] = []

        # sort according to edges
        for sig in topological_sort(edges):
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
        key = resolve_key(key)

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
        key = resolve_key(key)
        self._validate_implementation(value)
        super().__setitem__(key, value)
        self._ordered = False

    def __delitem__(self, key: type_specifier) -> None:
        key = resolve_key(key)

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
        {"_args", "_dispatched", "_drop_na", "_flags", "_signature"}
    )

    def __init__(
        self,
        func: Callable,
        args: list[str],
        drop_na: bool,
        cache_size: int,
    ):
        super().__init__(func=func)
        self._drop_na = bool(drop_na)
        self._flags = threading.local()
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

    def generic(self, *args, **kwargs) -> Any:
        """A reference to the generic implementation of the decorated function.
        """
        return self.__wrapped__(*args, **kwargs)

    def _build_strategy(self, dispatched: dict[str, Any]) -> DispatchStrategy:
        """Normalize vectorized inputs to this :class:`DispatchFunc`.

        This method converts input vectors into pandas Series objects with
        homogenous indices, missing values, and element types.  Composite
        vectors will be grouped along with their homogenous counterparts and
        processed as independent frames.

        Notes
        -----
        Normalization is skipped if the dispatched function is called from a
        recursive context.
        """
        # extract vectors
        vectors = {}
        names = {}
        for arg, value in dispatched.items():
            if isinstance(value, pd.Series):
                vectors[arg] = value
                names[arg] = value.name
            elif isinstance(value, np.ndarray):
                vectors[arg] = value
            else:
                value = np.asarray(value, dtype=object)
                if value.shape:
                    vectors[arg] = value

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

        # detect type of each column
        detected = detect.detect_type(frame)

        # composite case
        if any(isinstance(v, types.CompositeType) for v in detected.values()):
            return CompositeDispatch(
                func=self,
                dispatched=dispatched,
                frame=frame,
                detected=detected,
                names=names,
                hasnans=hasnans,
                original_index=original_index
            )

        # homogenous case
        return HomogenousDispatch(
            func=self,
            dispatched=dispatched,
            frame=frame,
            detected=detected,
            names=names,
            hasnans=hasnans,
            original_index=original_index
        )

    def __call__(self, *args, **kwargs) -> Any:
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.

        Notes
        -----
        This automatically detects aggregations, transformations, and
        filtrations based on the return value.

            *   :data:`None <python:None>` signifies a filtration.  The passed
                values will be excluded from the resulting series.
            *   A pandas :class:`Series <pandas.Series>` or
                :class:`DataFrame <pandas.DataFrame>`signifies a
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

        # fastpath for recursive calls
        if getattr(self._flags, "recursive", False):
            strategy = DirectDispatch(func=self, dispatched=dispatched)
            return strategy(bound)

        # outermost call - extract/normalize vectors and finalize results
        self._flags.recursive = True
        try:
            strategy = self._build_strategy(dispatched)
            return strategy(bound)
        finally:
            self._flags.recursive = False

    def __getitem__(self, key: type_specifier) -> Callable:
        """Get the dispatched implementation for objects of a given type.

        This method searches the implementation space being managed by this
        :class:`DispatchFunc`.  It always returns the same implementation that
        is used when the function is invoked.
        """
        try:
            return self._dispatched[key]
        except KeyError:
            return self.__wrapped__

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an implementation from the pool.
        """
        self._dispatched.__delitem__(key)


#######################
####    PRIVATE    ####
#######################


class DispatchStrategy:
    """Base class for Strategy-Pattern dispatch pipelines."""

    def execute(self, bound: inspect.BoundArguments) -> Any:
        """Abstract method for executing a dispatched strategy."""
        raise NotImplementedError(
            f"strategy does not implement an `execute()` method: "
            f"{self.__qualname__}"
        )

    def finalize(self, result: Any) -> Any:
        """Abstract method to post-process the result of a dispatched strategy.
        """
        raise NotImplementedError(
            f"strategy does not implement a `finalize()` method: "
            f"{self.__qualname__}"
        )

    def __call__(self, bound: inspect.BoundArguments) -> Any:
        """A macro for invoking a strategy's `execute` and `finalize` methods
        in sequence.
        """
        return self.finalize(self.execute(bound))


class DirectDispatch(DispatchStrategy):
    """Dispatch raw inputs to the appropriate implementation."""

    def __init__(
        self,
        func: DispatchFunc,
        dispatched: dict[str, Any]
    ):
        self.func = func
        self.dispatched = dispatched
        self.key = tuple(detect.detect_type(v) for v in dispatched.values())

    def execute(self, bound: inspect.BoundArguments) -> Any:
        """Call the dispatched function with the bound arguments."""
        bound.arguments = {**bound.arguments, **self.dispatched}
        return self.func[self.key](*bound.args, **bound.kwargs)

    def finalize(self, result: Any) -> Any:
        """Process the result returned by this strategy's `execute` method."""
        return result  # do nothing


class HomogenousDispatch(DispatchStrategy):
    """Dispatch homogenous inputs to the appropriate implementation."""

    def __init__(
        self,
        func: DispatchFunc,
        dispatched: dict[str, Any],
        frame: pd.DataFrame,
        detected: dict[str, types.ScalarType],
        names: dict[str, str],
        hasnans: bool,
        original_index: pd.Index
    ):
        self.index = frame.index
        self.original_index = original_index
        self.hasnans = hasnans

        # split frame into normalized columns
        for col, series in frame.items():
            series = series.rename(names.get(col, None))
            dispatched[col] = series.astype(detected[col].dtype, copy=False)

        # construct DispatchDirect wrapper
        self.direct = DirectDispatch(
            func=func,
            dispatched=dispatched
        )

    def execute(self, bound: inspect.BoundArguments) -> Any:
        """Call the dispatched implementation with the bound arguments."""
        return self.direct(bound)

    def finalize(self, result: Any) -> Any:
        """Infer mode of operation (filter/transform/aggregate) from return
        type and adjust result accordingly.
        """
        # TODO: transform needs to account for dataframe output

        # transform
        if isinstance(result, (pd.Series, pd.DataFrame)):
            hasnans = self.hasnans

            # warn if final index is not a subset of original
            if not result.index.difference(self.index).empty:
                warn_msg = (
                    f"index mismatch in {self.__qualname__}() with signature"
                    f"{tuple(str(x) for x in self.direct.key)}: dispatched "
                    f"implementation did not return a like-indexed Series/"
                    f"DataFrame"
                )
                warnings.warn(warn_msg, UserWarning, stacklevel=4)
                hasnans = True

            # replace missing values, aligning on index
            if hasnans or result.shape[0] < self.index.shape[0]:
                nullable = detect.detect_type(result).make_nullable()
                result = replace_na(
                    result.astype(nullable.dtype, copy=False),
                    index=pd.RangeIndex(0, self.original_index.shape[0]),
                    na_value=nullable.na_value
                )

            # replace original index
            result.index = self.original_index

        # aggregate
        return result


class CompositeDispatch(DispatchStrategy):
    """Dispatch composite inputs to the appropriate implementations."""

    def __init__(
        self,
        func: DispatchFunc,
        dispatched: dict[str, Any],
        frame: pd.DataFrame,
        detected: dict[str, types.ScalarType | types.CompositeType],
        names: dict[str, str],
        hasnans: bool,
        original_index: pd.Index
    ):
        self.func = func
        self.dispatched = dispatched
        self.frame = frame
        self.names = names
        self.hasnans = hasnans
        self.original_index = original_index

        # generate type frame
        type_frame = pd.DataFrame({
            k: getattr(v, "index", v) for k, v in detected.items()
        })

        # group by type
        grouped = type_frame.groupby(list(detected), sort=False)
        self.groups = grouped.groups

    def __iter__(self):
        """Sequentially extract each group from the parent frame"""
        for key, indices in self.groups.items():
            # extract group
            group = self.frame.iloc[indices]

            # bind names to key
            if not isinstance(key, tuple):
                key = (key,)
            detected = dict(zip(group.columns, key))

            # yield to __call__()
            yield detected, group

    def execute(self, bound: inspect.BoundArguments) -> list:
        """For each group in the input, call the dispatched implementation
        with the bound arguments.
        """
        results = []

        # process each group independently
        for detected, group in self:
            strategy = HomogenousDispatch(
                func=self.func,
                dispatched=self.dispatched,
                frame=group,
                detected=detected,
                names=self.names,
                hasnans=self.hasnans,
                original_index=self.original_index
            )
            result = (detected, strategy.execute(bound))
            results.append(result)

        return results

    def finalize(self, result: list) -> Any:
        computed = [group_result for _, group_result in result]

        # dataframe transform
        if all(isinstance(comp, pd.DataFrame) for comp in computed):
            columns = {tuple(df.columns) for df in computed}
            if len(columns) > 1:
                raise ValueError(f"column mismatch: {columns}")

            computed = {
                col: [df[col] for df in computed]
                for col in columns.pop()
            }
            return pd.DataFrame({
                col: self._combine_series(vals)
                for col, vals in computed.items()
            })

        # series transform
        if all(isinstance(comp, pd.Series) for comp in computed):
            return self._combine_series(computed)

        # aggregate
        computed = [
            arg_types | {f"{self.func.__name__}()": pd.Series([comp])}
            for arg_types, comp in result
        ]
        return pd.concat(
            [pd.DataFrame(row, index=[0]) for row in computed],
            ignore_index=True
        )

    def _combine_series(self, computed: list) -> pd.Series:
        """Merge the computed series results by index."""
        observed = {detect.detect_type(series) for series in computed}
        # final_size = self._check_indices([series.index for series in computed])

        # if results are composite but in same family, attempt to standardize
        roots = {typ.root for typ in observed}
        if (
            len(observed) > 1 and
            any(all(typ.is_subtype(root) for typ in observed) for root in roots)
        ):
            from pdcast.convert import cast

            max_range = max(typ.max - typ.min for typ in observed)
            candidates = [
                typ for typ in observed if typ.max - typ.min == max_range
            ]
            candidates = sorted(candidates, key=lambda typ: typ.min + typ.max)
            for candidate in candidates:
                try:
                    computed = [
                        cast(series, candidate, errors="raise")
                        for series in computed
                    ]
                    observed = {candidate}
                    break
                except:
                    continue

        # results are homogenous
        if len(observed) == 1:
            final = pd.concat(computed)
            final.sort_index()
            if self.hasnans or final.shape[0] < self.frame.shape[0]:
                nullable = observed.pop().make_nullable()
                final = replace_na(
                    final.astype(nullable.dtype, copy=False),
                    index=pd.RangeIndex(0, self.original_index.shape[0]),
                    na_value=nullable.na_value
                )
            final.index = self.original_index
            return final

        # results are composite
        # NOTE: we can't use pd.concat() because it tends to coerce mixed-type
        # results in undesirable ways.  Instead, we manually fold them into a
        # `dtype: object` series to preserve the actual values.
        final = pd.Series(
            np.full(self.original_index.shape[0], pd.NA, dtype=object),
            index=self.frame.index
        )
        for group_result in computed:
            final.update(group_result)
        final.index = self.original_index
        return final


    def _check_indices(self, group_indices: list) -> int:
        """Validate and merge a collection of transformed Series/DataFrame indices.
        """
        # TODO: can't use pd.concat with indices.  Have to use index.append or
        # index.union

        # check that indices do not overlap with each other
        try:
            index = pd.concat(group_indices, verify_integrity=True)
        except SomeError:
            # warn that groups had an index collision
            index = pd.concat(group_indices)

        # check that indices are subset of starting index
        if not index.difference(self.index).empty:
            # warn that groups did not produce a like-indexed series
            pass

        return index.shape[0]


def resolve_key(key: type_specifier | tuple[type_specifier]) -> tuple:
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


def supercedes(sig1: tuple, sig2: tuple) -> bool:
    """Check if sig1 is consistent with and strictly more specific than sig2.
    """
    return all(x.contains(y) for x, y in zip(sig2, sig1))


def edge(sig1: tuple, sig2: tuple) -> bool:
    """If ``True``, check sig1 before sig2.

    Ties are broken by recursively backing off the last element of both
    signatures.  As a result, whichever one is more specific in its earlier
    elements will always be preferred.
    """
    # pylint: disable=arguments-out-of-order
    if not sig1 or not sig2:
        return False

    return (
        supercedes(sig1, sig2) and
        not supercedes(sig2, sig1) or edge(sig1[:-1], sig2[:-1])
    )


def topological_sort(edges: dict) -> list:
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
    >>> topological_sort({1: (2, 3), 2: (3, )})
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


def replace_na(series: pd.Series, index: pd.Index, na_value: Any) -> pd.Series:
    """Replace any index that is not present in ``result`` with a missing value.
    """
    result = pd.Series(
        np.full(index.shape[0], na_value, dtype=object),
        index=index,
        dtype=object
    )
    result.update(series)
    return result.astype(series.dtype)




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
#         not (supercedes(sig1, sig2) or supercedes(sig2, sig1))
#     )


# def ambiguities(signatures):
#     """ All signature pairs such that A is ambiguous with B """
#     signatures = list(map(tuple, signatures))
#     return set((a, b) for a in signatures for b in signatures
#                if hash(a) < hash(b)
#                and ambiguous(a, b)
#                and not any(supercedes(c, a) and supercedes(c, b)
#                            for c in signatures))








# from .attachable import attachable


# @attachable
# @dispatch("self", "other")
# def __add__(self, other):
#     return getattr(self.__add__, "original", self.__add__)(other)


# @__add__.overload("int", "int")
# def add_integer(self, other):
#     return self - other


# __add__.attach_to(pd.Series)
# print(pd.Series([1, 2, 3]) + 1)
# print(pd.Series([1, 2, 3]) + True)
# print(pd.Series([1, 2, 3]) + [1, True, 1.0])
