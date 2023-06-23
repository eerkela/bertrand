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
from typing import Any, Callable, Iterable, Iterator
import warnings

import numpy as np
import pandas as pd

from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast import types
from pdcast.util.structs import LRUDict
from pdcast.util.type_hints import type_specifier

from .base import FunctionDecorator, no_default


# TODO: emit a warning whenever an implementation is replaced.
# -> do this.

# TODO: None wildcard value?
# -> use registry wildcards instead

# TODO: result is None -> fill with NA?
# -> probably not.  Just return an empty series if filtering.


# TODO: appears to be an index mismatch in composite add() example from
# README.features

# TODO: DispatchStrategies should reference the DispatchDict, not the
# DispatchFunc.  This is a more direct code path.

# TODO: DispatchDict -> DispatchTable


# TODO: DispatchSignature takes over DispatchDict's role as a container for
# signatures + implementations.
# It creates DispatchArguments, which are referenced in every DispatchStrategy.



# TODO: pdcast.cast("today", "datetime") uses python datetimes rather than
# pandas


######################
####    PUBLIC    ####
######################


def dispatch(
    *args,
    drop_na: bool = True,
    cache_size: int = 64,
    convert_mixed: bool = False
) -> Callable:
    """A decorator that allows a Python function to dispatch to multiple
    implementations based on the type of one or more of its arguments.

    Parameters
    ----------
    *args : str
        Argument names to dispatch on.  Each of these must be reflected in the
        signature of the decorated function, and will be required in each of
        its overloaded implementations.
    drop_na : bool, default True
        Indicates whether to drop missing values from input vectors before
        forwarding to a dispatched implementation.
    cache_size : int, default 64
        The maximum number of signatures to store in cache.
    convert_mixed_output : bool, default False
        Controls whether to attempt standardization of mixed-type results
        during composite dispatch.  This is only applied if the output type for
        each group belongs to the same family (i.e. multiple variations of int,
        float, datetime, etc.).  This argument is primarily used to allow
        dynamic upcasting for each group during data conversions.

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
            dispatched=args,
            drop_na=drop_na,
            cache_size=cache_size,
            convert_mixed=convert_mixed
        )

    return decorator


#######################
####    PRIVATE    ####
#######################


class DispatchFunc(FunctionDecorator):
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

    # TODO: remember to update _reserved after refactor

    _reserved = (
        FunctionDecorator._reserved |
        {"_args", "_drop_na", "_flags", "_signature"}
    )

    def __init__(
        self,
        func: Callable,
        dispatched: tuple[str, ...],
        drop_na: bool,
        cache_size: int,
        convert_mixed: bool
    ):
        super().__init__(func=func)
        if not dispatched:
            raise TypeError(
                f"'{func.__qualname__}' must dispatch on at least one argument"
            )

        self._signature = DispatchSignature(
            func,
            dispatched=dispatched,
            cache_size=cache_size
        )


        self._args = dispatched
        self._drop_na = drop_na
        self._convert_mixed = convert_mixed
        self._flags = threading.local()

    @property
    def overloaded(self) -> MappingProxyType:
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

            >>> foo.overloaded

        .. TODO: check
        """
        return MappingProxyType(self._signature.map)

    def overload(self, *args, **kwargs) -> Callable:
        """A decorator that transforms a naked function into a dispatched
        implementation for this :class:`DispatchFunc`.

        TODO: update this

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
        callable is a method of an :class:`ScalarType <pdcast.ScalarType>` or
        :class:`DecoratorType <pdcast.DecoratorType>` subclass.  In that case, the
        attached type will be automatically bound to the dispatched
        implementation during
        :meth:`__init_subclass__() <ScalarType.__init_subclass__>`.

        Examples
        --------
        See the :func:`dispatch` :ref:`API docs <dispatch.dispatched>` for
        example usage.
        """

        def implementation(func: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            self._signature.register(func, args, kwargs, warn=True)
            return func

        return implementation

    def fallback(self, *args, **kwargs) -> Any:
        """A reference to the default implementation of the decorated function.
        """
        return self.__wrapped__(*args, **kwargs)

    def _build_strategy(self, dispatch_args: dict[str, Any]) -> DispatchStrategy:
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
        for arg, value in dispatch_args.items():
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
        detected = detect_type(frame)

        # composite case
        if any(isinstance(v, types.CompositeType) for v in detected.values()):
            return CompositeDispatch(
                func=self,
                dispatch_args=dispatch_args,
                frame=frame,
                detected=detected,
                names=names,
                hasnans=hasnans,
                original_index=original_index,
                convert_mixed=self._convert_mixed
            )

        # homogenous case
        return HomogenousDispatch(
            func=self,
            dispatch_args=dispatch_args,
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
        # arguments = self._signature(*args, **kwargs)

        # if getattr(self._flags, "recursive", False):
        #   strategy = DirectDispatch(func=self, dispatch_args=arguments)
        #   return strategy(arguments)

        # arguments.normalize()
        # if arguments.is_composite:
        #   strategy = CompositeDispatch(
        #       func=self,
        #       arguments=arguments,
        #       standardize_dtype=self._standardize_dtype,
        # )
        # else:
        #   strategy = HomogenousDispatch(
        #       func=self,
        #       arguments=arguments,
        #   )

        # self._flags.recursive = True
        # try:
        #   return strategy(arguments)
        # finally:
        #   self._flags.recursive = False

        # bind signature
        bound = self._signature.bind(*args, **kwargs)
        bound.apply_defaults()

        # extract dispatched arguments
        dispatch_args = {arg: bound.arguments[arg] for arg in self._args}

        # call detect_type() on each argument
        

        # fastpath for recursive calls
        if getattr(self._flags, "recursive", False):
            strategy = DirectDispatch(func=self, dispatch_args=dispatch_args)
            return strategy(bound)

        # outermost call - extract/normalize vectors and finalize results
        self._flags.recursive = True
        try:
            strategy = self._build_strategy(dispatch_args)
            return strategy(bound)
        finally:
            self._flags.recursive = False

    def __getitem__(
        self,
        key: type_specifier
    ) -> Callable:
        """Get the dispatched implementation for objects of a given type.

        This method searches the implementation space being managed by this
        :class:`DispatchFunc`.  It always returns the same implementation that
        is used when the function is invoked.
        """
        try:
            return self._signature[key]
        except KeyError:
            return self.__wrapped__

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an implementation from the pool.
        """
        del self._signature[key]


class DispatchSignature:
    """An ordered dictionary that stores types and their dispatched
    implementations for :class:`DispatchFunc` operations.
    """

    def __init__(
        self,
        func: Callable,
        dispatched: tuple[str, ...],
        cache_size: int = 128
    ):
        self.signature = inspect.signature(func)
        self.dispatched = dispatched
        bad = [name for name in dispatched if name not in self.parameters]
        if bad:
            raise TypeError(f"argument not recognized: {bad}")

        self.map = {}
        self.cache = LRUDict(maxsize=cache_size)
        self.ordered = False

    @property
    def parameters(self) -> dict[str, inspect.Parameter]:
        """TODO: take from ExtensionSignature
        """
        return self.signature.parameters

    def get(self, key: type_specifier, default: Any = None) -> Any:
        """Implement :meth:`dict.get` for DispatchSignature objects."""
        try:
            return self[key]
        except KeyError:
            return default

    def pop(self, key: type_specifier, default: Any = no_default) -> Any:
        """Implement :meth:`dict.pop` for DispatchSignature objects."""
        key = self.resolve_key(key)
        try:
            result = self[key]
            del self[key]
            return result
        except KeyError as err:
            if default is no_default:
                raise err
            return default

    def setdefault(self, key: type_specifier, default: Callable = None) -> Any:
        """Implement :meth:`dict.setdefault` for DispatchSignature objects."""
        key = self.resolve_key(key)
        try:
            return self[key]
        except KeyError:
            self[key] = default
            return default

    def update(self, other: dict) -> None:
        """Implement :meth:`dict.update` for DispatchSignature objects."""
        for key, value in other.items():
            self[key] = value

    def sort(self) -> None:
        """Sort the dictionary into topological order, with most specific
        keys first.
        """
        keys = list(self)

        # draw edges between keys according to their specificity
        edges = dict.fromkeys(keys, set())
        for key1 in keys:
            for key2 in keys:
                if edge(key1, key2):
                    edges[key1].add(key2)

        # sort according to edges
        for key in topological_sort(edges):
            item = self.map.pop(key)
            self.map[key] = item

        self.ordered = True

    def resolve_key(
        self,
        key: type_specifier | tuple[type_specifier]
    ) -> tuple[types.VectorType]:
        """Convert arbitrary type specifiers into a valid key for
        `DispatchSignature` lookups.
        """
        if not isinstance(key, tuple):
            key = (key,)

        result = []
        for typespec in key:
            resolved = resolve_type(typespec)
            if isinstance(resolved, types.CompositeType):
                raise TypeError(f"key must not be composite: {repr(typespec)}")
            result.append(resolved)

        return tuple(result)

    def parse_overload(
        self,
        func: Callable,
        signature: inspect.Signature,
        args: tuple[type_specifier],
        kwargs: dict[str, type_specifier]
    ):
        """Bind the arguments to the function and sort their values into the
        expected order.

        Raises
        ------
        TypeError
            If the arguments could not be bound to the function's signature or
            if they do not exactly match the dispatched arguments specified in
            this :class:`DispatchSignature`.
        """
        # bind *args, **kwargs
        try:
            bound = signature.bind_partial(*args, **kwargs).arguments
        except TypeError as err:
            func_name = f"'{func.__module__}.{func.__qualname__}()'"
            reconstructed = [repr(value) for value in args]
            reconstructed.extend(
                f"{name}={repr(value)}" for name, value in kwargs.items()
            )
            err_msg = (
                f"invalid signature for {func_name}: "
                f"({', '.join(reconstructed)})"
            )
            raise TypeError(err_msg) from err

        # translate into the expected order
        result = []
        missing = []
        for name in self.dispatched:
            if name in bound:
                result.append(bound.pop(name))
            else:
                missing.append(name)

        # confirm all arguments were matched
        if missing:
            raise TypeError(f"no signature given for argument: {missing}")

        # confirm no extra arguments were given
        if bound:
            raise TypeError(
                f"signature contains non-dispatched arguments: "
                f"{list(bound)}"
            )

        return result

    def register(
        self,
        func: Callable,
        args: tuple[type_specifier],
        kwargs: dict[str, type_specifier],
        warn: bool = True
    ) -> None:
        """Ensure that an overloaded implementation is valid."""
        if not callable(func):
            raise TypeError(
                f"overloaded implementation must be callable: {repr(func)}"
            )

        # confirm func accepts named arguments
        sig = inspect.signature(func)
        missing = [arg for arg in self.dispatched if arg not in sig.parameters]
        if missing:
            func_name = f"'{func.__module__}.{func.__qualname__}()'"
            raise TypeError(
                f"{func_name} must accept dispatched arguments: {missing}"
            )

        # bind *args, **kwargs into correct key order
        key = self.parse_overload(func, sig, args, kwargs)

        # expand key into cartesian product
        key = [resolve_type([typespec]) for typespec in key]
        key = list(itertools.product(*key))

        # register implementation under each key
        for path in key:
            previous = self.get(path, None)
            if warn and previous:
                warn_msg = (
                    f"Replacing '{previous.__qualname__}()' with "
                    f"'{func.__qualname__}()' for signature "
                    f"{tuple(str(x) for x in path)}"
                )
                warnings.warn(warn_msg, UserWarning, stacklevel=2)

            self[path] = func

    def __call__(self, *args, **kwargs) -> DispatchArguments:
        """Bind this signature to create a `DispatchArguments` object."""
        parameters = tuple(self.parameters.values())

        # bind *args, **kwargs
        bound = self.signature.bind(*args, **kwargs)
        bound.apply_defaults()

        # extract dispatched arguments
        dispatched = {arg: bound.arguments[arg] for arg in self.dispatched}

        # call detect_type() on each argument
        detected = {arg: detect_type(val) for arg, val in dispatched.items()}

        # build signature
        return DispatchArguments(
            arguments=bound.arguments,
            parameters=parameters,
            positional=parameters[:len(args)],
            dispatched=dispatched,
            detected=detected
        )

    def __getitem__(
        self,
        key: type_specifier | tuple[type_specifier]
    ) -> Callable:
        """Search the :class:`DispatchSignature <pdcast.DispatchSignature>` for
        a particular implementation.
        """
        key = self.resolve_key(key)

        # trivial case: key has exact match
        if key in self.map:
            return self.map[key]

        # check for cached result
        if key in self.cache:
            return self.cache[key]

        # sort map
        if not self.ordered:
            self.cache.clear()
            self.sort()

        # search for first (sorted) match that fully contains key
        for comp_key, implementation in self.map.items():
            if all(x.contains(y) for x, y in zip(comp_key, key)):
                self.cache[key] = implementation
                return implementation

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __setitem__(self, key: type_specifier, value: Callable) -> None:
        """Add an overloaded implementation to the
        :class:`DispatchSignature <pdcast.DispatchSignature>`.
        """
        key = self.resolve_key(key)
        self.check_implementation(value)
        self.map[key] = value
        self.ordered = False

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an overloaded implementation from the
        :class:`DispatchSignature <pdcast.DispatchSignature>`.
        """
        key = self.resolve_key(key)

        # require exact match
        if key in self.map:
            del self.map[key]
        else:
            raise KeyError(tuple(str(x) for x in key))

    def __contains__(self, key: type_specifier):
        """Check if a particular implementation is present in the
        :class:`DispatchSignature <pdcast.DispatchSignature>`.
        """
        try:
            self.__getitem__(key)
            return True
        except KeyError:
            return False


class DispatchArguments:
    """A simple wrapper for an `inspect.BoundArguments` object with extra
    context for dispatched arguments and their detected types.
    """

    def __init__(
        self,
        arguments: dict[str, Any],
        parameters: tuple[inspect.Parameter, ...],
        positional: tuple[inspect.Parameter, ...],
        dispatched: dict[str, Any],
        detected: dict[str, types.Type]
    ):
        self.arguments = arguments
        self.parameters = parameters
        self.positional = positional
        self.dispatched = dispatched
        self.detected = detected
        self.names = {}
        self.frame = None
        self.original_index = None
        self.hasnans = None

    @property
    def args(self) -> tuple[Any, ...]:
        """Get an *args tuple from the bound signature."""
        return tuple(self.arguments[par.name] for par in self.positional)

    @property
    def kwargs(self) -> dict[str, Any]:
        """Get a **kwargs dict from the bound signature."""
        pos = {par.name for par in self.positional}
        return {k: v for k, v in self.arguments.items() if k not in pos}

    @property
    def key(self) -> tuple[types.Type, ...]:
        """Form a key for indexing a DispatchFunc."""
        return tuple(self.detected.values())

    @property
    def is_composite(self) -> bool:
        """Indicates whether any dispatched arguments are of mixed type."""
        return any(
            isinstance(val, types.CompositeType)
            for val in self.detected.values()
        )

    @property
    def vectors(self) -> dict[str, pd.Series]:
        """Extract vectors from dispatched arguments."""
        result = {}
        for arg, series in self.frame.items():
            # NOTE: Each column is stored under its argument name by default.
            # If a named series was supplied as that argument's value, then we
            # will have lost the original name.  To fix this, we store a
            # parallel dict of argument names to series names and rename them
            # here.
            if arg in self.names:
                series = series.rename(self.names[arg], copy=True)
            result[arg] = series

        return result

    @property
    def groups(self) -> dict[str, pd.Index]:
        """Group vectors by type.

        This only applies if `is_composite=True`.
        """
        type_frame = pd.DataFrame({
            arg: getattr(typ, "index", typ)
            for arg, typ in self.detected.items()
        })
        groupby = type_frame.groupby(list(type_frame.columns), sort=False)
        return groupby.groups

    def normalize(self) -> DispatchArguments:
        """Extract vectors from dispatched arguments and normalize them."""
        # extract vectors
        vectors = {}
        for arg, value in self.arguments:
            if isinstance(value, pd.Series):
                vectors[arg] = value
                self.names[arg] = value.name
            elif isinstance(value, np.ndarray):
                vectors[arg] = value
            else:
                value = np.asarray(value, dtype=object)
                if value.shape:
                    vectors[arg] = value

        # bind vectors into DataFrame
        self.frame = pd.DataFrame(vectors)

        # normalize indices
        original_index = self.frame.index
        if not isinstance(original_index, pd.RangeIndex):
            self.frame.index = pd.RangeIndex(0, self.frame.shape[0])

        # NOTE: the DataFrame constructor will happily accept vectors with
        # misaligned indices, filling any absent values with NaNs.  Since this
        # is a likely source of bugs, we always warn the user when it happens.

        original_length = original_index.shape[0]
        if any(vec.shape[0] != original_length for vec in vectors.values()):
            warn_msg = (
                f"{self.__qualname__}() - vectors have misaligned indices"
            )
            warnings.warn(warn_msg, UserWarning, stacklevel=2)

        # drop missing values
        self.frame.dropna(how="any")
        self.hasnans = self.frame.shape[0] != original_length





# DispatchArguments are passed to every DispatchStrategy, along with a reference
# to the parent DispatchFunc.  DirectDispatch, uses its arguments to index the
# DispatchFunc using DispatchArguments.key, and then calls the appropriate
# implementation with *DispatchArguments.args, **DispatchArguments.kwargs.

# DispatchArguments have a .normalize method, which extracts vectors and
# normalizes them.  This is only called if the recursive flag is false.

# CompositeDispatch constructs its own DispatchArguments for each group.





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
        A dict of the form `{A: {B, C}}` where `B` and `C` depend on `A`.

    Returns
    -------
    list
        An ordered list of nodes that satisfy the dependencies of `edges`.

    Examples
    --------
    .. doctest::

        >>> topological_sort({1: (2, 3), 2: (3, )})
        [1, 2, 3]

    References
    ----------    
    Kahn, Arthur B. (1962), "Topological sorting of large networks",
    Communications of the ACM
    """
    # edge_count is used to detect cycles
    edge_count = sum(len(dependencies) for dependencies in edges.values())

    # invert edges: {A: {B, C}} -> {B: {A}, C: {A}}
    inverted = {}
    for node, dependencies in edges.items():
        for dependent in dependencies:
            inverted.setdefault(dependent, set()).add(node)

    # Proceed with Kahn topological sort algorithm
    no_incoming = [node for node in edges if node not in inverted]
    result = []
    while no_incoming:

        # pop a node with no incoming edges (order doesn't matter)
        node = no_incoming.pop()
        result.append(node)

        # for each edge from node -> dependent:
        for dependent in edges.get(node, ()):

            # remove edge from inverted map
            inverted[dependent].remove(node)

            # decrement edge count
            edge_count -= 1

            # if dependent has no more incoming edges, add it to the queue
            if not inverted[dependent]:
                no_incoming.append(dependent)

    # if there are any edges left, then there must be a cycle
    if edge_count:
        cycles = [node for node in edges if inverted.get(node, None)]
        raise ValueError(f"edges are cyclic: {cycles}")

    return result


##########################
####    STRATEGIES    ####
##########################


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
        dispatch_args: dict[str, Any]
    ):
        self.func = func
        self.dispatch_args = dispatch_args
        self.key = tuple(detect_type(v) for v in dispatch_args.values())

    def execute(self, bound: inspect.BoundArguments) -> Any:
        """Call the dispatched function with the bound arguments."""
        bound.arguments = {**bound.arguments, **self.dispatch_args}
        return self.func[self.key](*bound.args, **bound.kwargs)

    def finalize(self, result: Any) -> Any:
        """Process the result returned by this strategy's `execute` method."""
        return result  # do nothing


class HomogenousDispatch(DispatchStrategy):
    """Dispatch homogenous inputs to the appropriate implementation."""

    def __init__(
        self,
        func: DispatchFunc,
        dispatch_args: dict[str, Any],
        frame: pd.DataFrame,
        detected: dict[str, types.VectorType],
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
            dispatch_args[col] = series.astype(detected[col].dtype, copy=False)

        # construct DispatchDirect wrapper
        self.direct = DirectDispatch(
            func=func,
            dispatch_args=dispatch_args
        )

    def execute(self, bound: inspect.BoundArguments) -> Any:
        """Call the dispatched implementation with the bound arguments."""
        return self.direct(bound)

    def finalize(self, result: Any) -> Any:
        """Infer mode of operation (filter/transform/aggregate) from return
        type and adjust result accordingly.
        """
        # transform
        if isinstance(result, (pd.Series, pd.DataFrame)):
            # warn if final index is not a subset of original
            if not result.index.difference(self.index).empty:
                warn_msg = (
                    f"index mismatch in {self.__qualname__}() with signature"
                    f"{tuple(str(x) for x in self.direct.key)}: dispatched "
                    f"implementation did not return a like-indexed Series/"
                    f"DataFrame"
                )
                warnings.warn(warn_msg, UserWarning, stacklevel=4)
                self.hasnans = True

            # replace missing values, aligning on index
            if self.hasnans or result.shape[0] < self.index.shape[0]:
                output_type = detect_type(result)
                output_index = pd.RangeIndex(0, self.original_index.shape[0])
                if isinstance(result, pd.Series):
                    nullable = output_type.make_nullable()
                    result = replace_na(
                        result,
                        dtype=nullable.dtype,
                        index=output_index,
                        na_value=nullable.na_value
                    )
                else:
                    with_na = {}
                    for col, series in result.items():
                        nullable = output_type[col].make_nullable()
                        with_na[col] = replace_na(
                            series,
                            dtype=nullable.dtype,
                            index=output_index,
                            na_value=nullable.na_value
                        )
                    result = pd.DataFrame(with_na)

            # replace original index
            result.index = self.original_index

        # aggregate
        return result


class CompositeDispatch(DispatchStrategy):
    """Dispatch composite inputs to the appropriate implementations."""

    def __init__(
        self,
        func: DispatchFunc,
        dispatch_args: dict[str, Any],
        frame: pd.DataFrame,
        detected: dict[str, types.VectorType | types.CompositeType],
        names: dict[str, str],
        hasnans: bool,
        original_index: pd.Index,
        convert_mixed: bool
    ):
        self.func = func
        self.dispatch_args = dispatch_args
        self.frame = frame
        self.names = names
        self.hasnans = hasnans
        self.original_index = original_index
        self.convert_mixed = convert_mixed

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
                dispatch_args=self.dispatch_args,
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

        # transform
        as_series = all(isinstance(comp, pd.Series) for comp in computed)
        as_dataframe = all(isinstance(comp, pd.DataFrame) for comp in computed)
        if as_series or as_dataframe:
            self._check_indices(series.index for series in computed)

            # return as series
            if as_series:
                return self._combine_series(computed)

            # return as dataframe
            columns = {tuple(df.columns) for df in computed}
            if len(columns) > 1:
                raise ValueError(f"column mismatch: {columns}")
            return pd.DataFrame({
                col: self._combine_series([df[col] for df in computed])
                for col in columns.pop()
            })

        # aggregate
        computed = [
            arg_types | {f"{self.func.__name__}()": pd.Series([comp])}
            for arg_types, comp in result
        ]
        return pd.concat(
            [pd.DataFrame(row, index=[0]) for row in computed],
            ignore_index=True
        )

    def _check_indices(self, indices: Iterator[pd.Index]) -> None:
        """Validate and merge a collection of transformed Series/DataFrame
        indices.
        """
        # check that indices do not overlap with each other
        final_index = next(indices)
        final_size = final_index.shape[0]
        for index in indices:
            final_index = final_index.union(index)
            if len(final_index) < final_size + index.shape[0]:
                warn_msg = (
                    f"index collision in composite {self.func.__name__}()"
                )
                warnings.warn(warn_msg, UserWarning)
            final_size = final_index.shape[0]

        # check that indices are subset of starting index
        if not final_index.difference(self.frame.index).empty:
            # TODO: this results in a KeyError during slice assignment, so
            # just raise it as an error here and in HomogenousDispatch
            warn_msg = "final index is not a subset of original"
            warnings.warn(warn_msg, UserWarning)
            self.hasnans = True

    def _combine_series(self, computed: list) -> pd.Series:
        """Merge the computed series results by index."""
        observed = [detect_type(series) for series in computed]
        unique = set(observed)

        # if results are composite but in same family, attempt to standardize
        if self.convert_mixed and len(unique) > 1:
            roots = {typ.generic.root for typ in unique}
            if any(all(t2.contains(t1) for t1 in unique) for t2 in roots):
                computed, unique = self._standardize_same_family(
                    computed,
                    unique
                )

        # results are homogenous
        if len(unique) == 1:
            final = pd.concat(computed)
            final.sort_index()
            if self.hasnans or final.shape[0] < self.frame.shape[0]:
                nullable = unique.pop().make_nullable()
                final = replace_na(
                    final,
                    dtype=nullable.dtype,
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
            index=pd.RangeIndex(0, self.original_index.shape[0])
        )
        for series in computed:
            final[series.index] = series
        final.index = self.original_index
        return final

    def _standardize_same_family(
        self,
        computed: list[pd.Series],
        observed: set[types.VectorType]
    ) -> tuple[list[pd.Series], set[types.VectorType]]:
        """If the dispatched implementations return series objects of different
        types within the same family, then attempt to standardize their
        results.

        This method attempts to standardize on the observed type with the
        largest possible range (max - min).  If a tie is encountered, the types
        will be sorted based on the absolute value of their mean range,
        preferring types that are centered toward 0.
        """
        from pdcast.convert import cast

        max_range = max(typ.max - typ.min for typ in observed)
        candidates = [typ for typ in observed if typ.max - typ.min == max_range]
        candidates = sorted(candidates, key=lambda typ: abs(typ.min + typ.max))
        for typ in candidates:
            try:
                computed = [
                    cast(x, typ, downcast=False, errors="raise")
                    for x in computed
                ]
                observed = {typ}
                return computed, observed
            except Exception:
                continue

        return computed, observed


def replace_na(
    series: pd.Series,
    dtype: np.dtype | pd.api.extensions.ExtensionDtype,
    index: pd.Index,
    na_value: Any
) -> pd.Series:
    """Replace any index that is not present in ``result`` with a missing
    value.
    """
    result = pd.Series(
        np.full(index.shape[0], na_value, dtype=object),
        index=index,
        name=series.name,
        dtype=object
    )
    result[series.index] = series
    return result.astype(dtype, copy=False)
