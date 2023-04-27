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

import pandas as pd

# import pdcast.convert as convert
import pdcast.detect as detect
import pdcast.resolve as resolve
import pdcast.types as base_types

from pdcast.util import wrapper
from pdcast.util.type_hints import type_specifier

from .base import BaseDecorator


# TODO: can probably support an ``operator`` argument that takes a string and
# broadcasts to a math operator.

# TODO: currently, the SeriesWrapper __enter__ statement is executed multiple
# times for composite data.  This is expensive and should only occur once.
# -> Maybe apply a different rule if SeriesWrapper is given another
# SeriesWrapper as input?


# TODO: check if output is series or serieswrapper and wrap.  otherwise return
# as-is.  Maybe include a replace NA flag in @dispatch itself?  If this is
# disabled, then name and index will be preserved, but NAs will be excluded.


# TODO: maybe @dispatch should take additional arguments, like wrap_adapters,
# remove_na, replace_na, etc.


# TODO: when dispatching to a method, have to account for self argument


# TODO: include optional targetable: bool argument to @dispatch
# this forces the function to accept a second argument, `dtype`, which must be
# a type specifier.  For these functions, implementations can be dispatched
# based on both the first and second arguments, rather than just the first.

# @to_datetime.overload("string", target="datetime[python]")
# @to_datetime.overload("int", target="datetime[pandas]")



# TODO: __getitem__ allows users to check which implementation will be used
# for a given data type
# cast["int"]["datetime"]
# round["int"]

# __getitem__ returns a TypeDict struct that automatically resolves its keys
# during lookup.


######################
####    PUBLIC    ####
######################


def dispatch(
    _func: Callable = None,
    *,
    depth: int = 1,
    wrap_adapters: bool = True
) -> Callable:
    """A decorator that allows a Python function to dispatch to multiple
    implementations based on the type of its first argument.

    Parameters
    ----------
    func : Callable
        A Python function or other callable to be decorated.  This serves as a
        generic implementation and is only called if no overloaded
        implementation is found for the dispatched data.

    Returns
    -------
    DispatchFunc
        A cooperative :class:`DispatchFunc` decorator, which manages dispatched
        implementations for the decorated callable.

    Raises
    ------
    TypeError
        If the decorated function does not accept at least one positional
        argument.

    Notes
    -----
    This decorator works just like
    :func:`@functools.singledispatch <python:functools.singledispatch>`,
    except that it is extended to handle vectorized data in the ``pdcast``
    :doc:`type system </content/types/types>`.
    """
    def decorator(func: Callable):
        """Convert a callable into a DispatchFunc object."""
        return DispatchFunc(func, depth=depth, wrap_adapters=wrap_adapters)

    if _func is None:
        return decorator
    return decorator(_func)


#######################
####    PRIVATE    ####
#######################


# TODO: contents of dispatch table:
# table[arg1][arg2][arg3]...

# each level but the last can hold the special value None as wildcard.
# @overload(None, "int") would take in any data so long as it outputs integers.


no_default = object()  # dummy for optional arguments in DispatchDict methods


class DispatchDict(OrderedDict):
    """An :class:`OrderedDict <python:collections.OrderedDict>` that stores
    types and their dispatched implementations for :class:`DispatchFunc`
    operations.
    """

    def __init__(self, mapping: dict | None = None):
        super().__init__()
        if mapping:
            for key, val in mapping.items():
                self.__setitem__(key, val)

    @classmethod
    def fromkeys(cls, iterable: Iterable, value: Any = None) -> DispatchDict:
        """Implement :meth:`dict.fromkeys` for DispatchDict objects."""
        result = DispatchDict()
        for key in iterable:
            result[key] = value
        return result

    def get(self, key: type_specifier, default: Any = None) -> Any:
        """Implement :meth:`dict.get` for DispatchDict objects."""
        try:
            return self[key]
        except KeyError:
            return default

    def pop(self, key: type_specifier, default: Any = no_default) -> Any:
        """Implement :meth:`dict.pop` for DispatchDict objects."""
        key_type = resolve.resolve_type(key)
        try:
            result = self[key_type]
            del self[key_type]
            return result
        except KeyError as err:
            if default is no_default:
                raise err
            return default

    def setdefault(self, key: type_specifier, default: Any = None) -> Any:
        """Implement :meth:`dict.setdefault` for DispatchDict objects."""
        key_type = resolve.resolve_type(key)
        try:
            return self[key_type]
        except KeyError:
            self[key_type] = default
            return default

    def update(self, other) -> None:
        """Implement :meth:`dict.update` for DispatchDict objects."""
        for key, val in DispatchDict(other).items():
            self[key] = val

    def reorder(self) -> None:
        """Sort the dictionary into topological order, with most specific
        keys first.
        """
        # get edges
        signatures = list(self)
        edges = [(a, b) for a in signatures for b in signatures if _edge(a, b)]

        # group edges by first node
        edges = groupby(lambda x: x[0], edges)

        # drop first node
        edges = {k: [b for _, b in v] for k, v in edges.items()}

        # add signatures not contained in edges
        for sig in signatures:
            if sig not in edges:
                edges[sig] = []

        # sort according to edges
        for sig in _sort(edges):
            super().move_to_end(sig)

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

        # search for first (sorted) match that fully contains key
        while key:
            # search for match of same length
            for sig, val in self.items():
                if (
                    len(sig) == len(key) and
                    all(x.contains(y) for x, y in zip(sig, key))
                ):
                    return val

            # back off and retry
            key = key[:-1]

        # no match found
        raise KeyError(str(key))

    def __setitem__(self, key: type_specifier, value: Any) -> None:
        key = _resolve_key(key)
        super().__setitem__(key, value)
        self.reorder()

    def __delitem__(self, key: type_specifier) -> None:
        # resolve key type
        key_type = resolve.resolve_type(key)
        if isinstance(key_type, base_types.CompositeType):
            raise TypeError(f"key must not be composite: {repr(key)}")

        # delete first match that contains key
        for typ in self:
            if typ.contains(key_type):
                super().__delitem__(typ)
                break
        else:  # no match found
            raise KeyError(str(key_type))

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
        {"_signature", "_dispatched", "_depth", "_wrap_adapters"}
    )

    def __init__(
        self,
        func: Callable,
        depth: int,
        wrap_adapters: bool
    ):
        super().__init__(func=func)
        self._signature = inspect.signature(func)

        # validate depth
        if depth < 1:
            raise ValueError(
                f"@dispatch depth must be >= 1 for function: "
                f"'{func.__qualname__}'"
            )
        if len(self._signature.parameters) < depth:
            err_msg = f"'{func.__qualname__}' must accept at least "
            if depth == 1:  # singular
                err_msg += f"{depth} argument"
            else:
                err_msg += f"{depth} arguments"
            raise TypeError(err_msg)

        self._dispatched = DispatchDict()
        self._wrap_adapters = bool(wrap_adapters)
        self._depth = depth

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

    def overload(self, *args) -> Callable:
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
            # ensure call is callable
            if not callable(call):
                raise TypeError(
                    f"overloaded implementation must be callable: {repr(call)}"
                )

            # ensure at least one arg is given
            if not args:
                raise TypeError(
                    f"'{call.__qualname__}()' must dispatch on at least one "
                    f"argument"
                )

            # broadcast to selected types
            composites = [resolve.resolve_type([spec]) for spec in args]
            paths = list(itertools.product(*composites))
            for path in paths:
                self._dispatched[path] = call

            return call

        return implementation

    def generic(
        self,
        *args,
        **kwargs
    ) -> pd.Series | wrapper.SeriesWrapper:
        """A reference to the generic implementation of the decorated function.
        """
        return self.__wrapped__(*args, **kwargs)

    def _dispatch_scalar(
        self,
        series: wrapper.SeriesWrapper,
        *args,
        **kwargs
    ) -> wrapper.SeriesWrapper:
        """Dispatch a homogenous series to the correct implementation."""
        # search for a dispatched implementation
        try:
            implementation = self._dispatched[series.element_type]
            result = implementation(series, *args, **kwargs)
        except KeyError:
            result = None

        # recursively unwrap adapters and retry.
        if result is None:
            # NOTE: This operates like a recursive stack.  Adapters are popped
            # off the stack in FIFO order before recurring, and then each
            # adapter is pushed back onto the stack in the same order.
            for before in getattr(series.element_type, "adapters", ()):
                series = series.element_type.inverse_transform(series)
                series = self._dispatch_scalar(series, *args, **kwargs)
                if (
                    self._wrap_adapters and
                    series.element_type == before.wrapped
                ):
                    series = series.element_type.transform(series)
                return series

        # fall back to generic implementation
        if result is None:
            result = self.__wrapped__(series, *args, **kwargs)

        # ensure result is a SeriesWrapper
        if not isinstance(result, wrapper.SeriesWrapper):
            raise TypeError(
                f"dispatched implementation of {self.__wrapped__.__name__}() "
                f"did not return a SeriesWrapper for type: "
                f"{series.element_type}"
            )

        # ensure final index is a subset of original index
        if not series.index.difference(result.index).empty:
            raise RuntimeError(
                f"index mismatch in {self.__wrapped__.__name__}(): dispatched "
                f"implementation for type {series.element_type} must return "
                f"a series with the same index as the original"
            )

        return result.rectify()

    def _dispatch_composite(
        self,
        series: wrapper.SeriesWrapper,
        *args,
        **kwargs
    ) -> wrapper.SeriesWrapper:
        """Dispatch a mixed-type series to the appropriate implementation."""
        groups = series.series.groupby(series.element_type.index, sort=False)

        # NOTE: SeriesGroupBy.transform() cannot reconcile mixed int64/uint64
        # arrays, and will attempt to convert them to float.  To avoid this, we
        # keep track of result.dtype.  If it is signed/unsigned and opposite
        # has been observed, we convert the result to dtype=object and
        # reconsider afterwards.
        observed = set()
        check_uint = [False]  # using a list avoids UnboundLocalError
        signed = base_types.SignedIntegerType
        unsigned = base_types.UnsignedIntegerType

        def transform(grp) -> pd.Series:
            """Groupwise transformation."""
            grp = wrapper.SeriesWrapper(
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
                result = convert.to_integer(
                    result,
                    dtype=target,
                    downcast=kwargs.get("downcast", None),
                    errors="raise"
                )
            except OverflowError:
                pass  # keep as dtype: object

        # re-wrap result
        return wrapper.SeriesWrapper(result, hasnans=series.hasnans)

    def __call__(
        self,
        data: Any,
        *args,
        **kwargs
    ) -> pd.Series | wrapper.SeriesWrapper:
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.
        """
        # fastpath for pre-wrapped data (internal use)
        if isinstance(data, wrapper.SeriesWrapper):
            if isinstance(data.element_type, base_types.CompositeType):
                return self._dispatch_composite(data, *args, **kwargs)
            return self._dispatch_scalar(data, *args, **kwargs)

        # enter SeriesWrapper context block
        with wrapper.SeriesWrapper(detect.as_series(data)) as series:

            # dispatch based on inferred type
            if isinstance(series.element_type, base_types.CompositeType):
                result = self._dispatch_composite(series, *args, **kwargs)
            else:
                result = self._dispatch_scalar(series, *args, **kwargs)

            # finalize
            series.series = result.series
            series.element_type = result.element_type
            series.hasnans = result.hasnans

        # return as pandas Series
        return series.series

    def __getitem__(self, key: type_specifier) -> Callable:
        """Get the dispatched implementation for objects of a given type.

        This method searches the implementation space being managed by this
        :class:`DispatchFunc`.  It always returns the same implementation that
        is used when the function is invoked.
        """
        # resolve key
        key_type = resolve.resolve_type(key)

        # search for a dispatched implementation
        try:
            return self._dispatched[key_type]
        except KeyError:
            pass

        # unwrap adapters
        for _ in key_type.adapters:
            return self[key_type.wrapped]  # recur

        # return generic
        return self.__wrapped__

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an implementation from the pool.
        """
        # resolve key
        key_type = resolve.resolve_type(key)

        # pass to TypeDict
        if key_type in self._dispatched:
            self._dispatched.__delitem__(key_type)


#######################
####    PRIVATE    ####
#######################


def _resolve_key(key: type_specifier | tuple[type_specifier]) -> tuple:
    """Convert arbitrary type specifiers into a valid key for DispatchDict
    lookups.
    """
    if not isinstance(key, tuple):
        key = (key,)

    key_type = []
    for spec in key:
        spec_type = resolve.resolve_type(spec)
        if isinstance(spec_type, base_types.CompositeType):
            raise TypeError(f"key must not be composite: {repr(spec)}")
        key_type.append(spec_type)

    return tuple(key_type)


def _consistent(sig1: tuple, sig2: tuple) -> bool:
    """Check for an overlap between two signatures.

    If this returns ``True``, then it is possible for a signature to satisfy
    both sig1 and sig2.
    """
    # check for empty signatures
    if not sig1:
        return not sig2
    if not sig2:
        return not sig1

    # lengths match
    if len(sig1) == len(sig2):
        return all(x.contains(y) or y.contains(x) for x, y in zip(sig1, sig2))

    return False  # not considering variadics

    # lengths do not match
    # idx1 = idx2 = 0
    # while idx1 < len(sig1) and idx2 < len(sig2):
    #     type1 = sig1[idx1]
    #     type2 = sig2[idx2]
    #     if not type1.contains(type2) and not type2.contains(type1):
    #         return False
    #     idx1 += 1
    #     idx2 += 1

    # return True


def _supercedes(sig1: tuple, sig2: tuple) -> bool:
    """Check if sig1 is consistent with and strictly more specific than sig2.
    """
    # same length
    if len(sig1) == len(sig2):
        return all(x.contains(y) for x, y in zip(sig2, sig1))

    return False  # not considering variadics

    # # longer
    # idx1 = idx2 = 0
    # while idx1 < len(sig1) and idx2 < len(sig2):
    #     type1 = sig1[idx1]
    #     type2 = sig2[idx2]
    #     if not type2.contains(type1):
    #         return False
    #     idx1 += 1
    #     idx2 += 1

    # return True


def _ambiguous(sig1: tuple, sig2: tuple) -> bool:
    """Signatures are consistent, but neither is strictly more specific.
    """
    return (
        _consistent(sig1, sig2) and
        not (_supercedes(sig1, sig2) or _supercedes(sig2, sig1))
    )


def _edge(sig1: tuple, sig2: tuple, tie_breaker: Callable = hash) -> bool:
    """If ``True``, check sig1 before sig2.

    Ties are broken by the ``tie_breaker``, which defaults to ``hash()``
    (psuedo-random).
    """
    return _supercedes(sig1, sig2) and (
        not _supercedes(sig2, sig1) or tie_breaker(sig1) > tie_breaker(sig2)
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







def groupby(func, seq):
    """ Group a collection by a key function

    >>> names = ['Alice', 'Bob', 'Charlie', 'Dan', 'Edith', 'Frank']
    >>> groupby(len, names)  # doctest: +SKIP
    {3: ['Bob', 'Dan'], 5: ['Alice', 'Edith', 'Frank'], 7: ['Charlie']}
    >>> iseven = lambda x: x % 2 == 0
    >>> groupby(iseven, [1, 2, 3, 4, 5, 6, 7, 8])  # doctest: +SKIP
    {False: [1, 3, 5, 7], True: [2, 4, 6, 8]}

    See Also:
        ``countby``
    """
    d = OrderedDict()
    for item in seq:
        key = func(item)
        if key not in d:
            d[key] = []
        d[key].append(item)
    return d





# def ambiguities(signatures):
#     """ All signature pairs such that A is ambiguous with B """
#     signatures = list(map(tuple, signatures))
#     return set((a, b) for a in signatures for b in signatures
#                if hash(a) < hash(b)
#                and ambiguous(a, b)
#                and not any(supercedes(c, a) and supercedes(c, b)
#                            for c in signatures))




@dispatch
def foo(bar, baz):
    """doc for foo()"""
    print("generic")
    return bar


@foo.overload("int", "int")
def integer_foo(bar, baz):
    print("int")
    return bar


@foo.overload("bool", "bool")
def boolean_foo(bar, baz):
    print("boolean")
    return bar


@foo.overload("int8", "int")
def int8_foo(bar, baz):
    print("int8")
    return bar


@foo.overload("int8[numpy]")
def numpy_int8_foo(bar, baz):
    print("np.int8")
    return bar
