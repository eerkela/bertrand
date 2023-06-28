"""EXPERIMENTAL - NOT CURRENTLY FUNCTIONAL

This module describes an ``@dispatch`` decorator that transforms an
ordinary Python function into one that dispatches to a method attached to the
inferred type of its first argument.
"""
from __future__ import annotations
import inspect
import itertools
import threading
from types import MappingProxyType
from typing import Any, Callable, Iterator, Mapping
import warnings

import numpy as np
import pandas as pd

from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast import types
from pdcast.util.structs import LRUDict
from pdcast.util.type_hints import dtype_like, type_specifier

from .base import Arguments, FunctionDecorator, Signature


# TODO: emit a warning whenever an implementation is replaced.
# -> use a simplefilter when the module is loaded, or implement None as a
# wildcard.  This would get expanded to registry.roots at runtime.


# TODO: None wildcard value?
# -> use registry wildcards instead

# TODO: result is None -> fill with NA?
# -> probably not.  Just return an empty series if filtering.


# TODO: appears to be an index mismatch in composite add() example from
# README.features



# TODO: pdcast.cast("today", "datetime") uses python datetimes rather than
# pandas
# -> this because of an errant .larger lookup that made it through the
# refactor.




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
    if not args or len(args) == 1 and callable(args[0]):
        raise TypeError("@dispatch requires at least one named argument")

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

    _reserved = (
        FunctionDecorator._reserved |
        {"_signature", "_drop_na", "_convert_mixed", "_flags"}
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
        self._signature = DispatchSignature(
            func,
            dispatched=dispatched,
            cache_size=cache_size
        )
        self._drop_na = drop_na
        self._convert_mixed = convert_mixed
        self._flags = threading.local()

    @property
    def dispatched(self) -> tuple[str, ...]:
        """The names of the arguments that this function dispatches on.

        Returns
        -------
        tuple[str, ...]
            The tuple containing the name of every dispatched argument, as
            provided to :func:`@dispatch <pdcast.dispatch>`.

        Notes
        -----
        The order of this tuple is significant, as it determines the order of
        the keys under which dispatched implementations are stored.  The keys
        themselves are always sorted according to their specificity, but ties
        can still occur if two or more implementations are equally valid for a
        given set of inputs.  For example:

        .. code:: python

            @dispatch("a", "b")
            def foo(a, b):
                ...

            @foo.overload("int", "int[python]")
            def foo1(a, b):
                ...

            @foo.overload("int[python]", "int")
            def foo2(a, b):
                ...

            >>> foo["int[python]", "int[python]"]
            ???

        In this case, it's not immediately clear which implementation we should
        choose.  ``foo1()`` is more specific in its first argument and
        ``foo2()`` in its second.

        In these cases, :func:`@dispatch <pdcast.dispatch>` always **prefers
        implementations** that are **more specific in their first argument**,
        from left to right.  In the case above, this means we would always
        choose ``foo2()`` over ``foo1()``.  This behavior can be reversed by
        changing the order of the arguments in the decorator, like so:

        .. code:: python

            @dispatch("b", "a")  # reversed
            def foo(a, b):
                ...

            @foo.overload("int", "int[python]")  # same order (a, b)
            def foo1(a, b):
                ...

            @foo.overload(a="int[python]", b="int")  # keyword (a, b)
            def foo2(a, b):
                ...

        We will now always prefer ``foo1()`` over ``foo2()``.

        .. note::

            :meth:`@overload() <pdcast.DispatchFunc.overload>` always parses
            arguments relative to the function that it decorates, not those
            specified in :func:`@dispatch() <pdcast.dispatch>`.  This is for
            clarity and maintenance, allowing users to write implementations
            without coupling too strongly to the base definition.

            At an implementation level,
            :meth:`@overload() <pdcast.DispatchFunc.overload>` binds its
            arguments to the decorated function, extracts the dispatched names,
            and then sorts them into the order specified by
            :func:`@dispatch() <pdcast.dispatch>`.  All changing this order
            does is reverse the keys that are used to index the
            :class:`DispatchFunc <pdcast.DispatchFunc>`, and thereby consider
            them from right to left (``b`` before ``a``) instead of
            left to right (``a`` before ``b``).

            More complicated dispatch priorities can be achieved if there are
            more than 2 arguments, but the same principle applies.
        """
        return self._signature.dispatched

    @property
    def overloaded(self) -> MappingProxyType:
        """A mapping from :doc:`types </content/types/types>` to their
        corresponding implementations.

        Returns
        -------
        MappingProxyType
            A read-only dictionary mapping types to their
            :meth:`overloaded <pdcast.DispatchFunc.overload>` callables.  The
            keys are always in the same order as the
            :attr:`dispatched <pdcast.DispatchFunc.dispatched>` arguments.

        Notes
        -----
        The returned mapping is sorted according to `topological order
        <https://en.wikipedia.org/wiki/Topological_sorting>`_.  Iterating
        through the map equates to searching it from most to least specific.

        Examples
        --------
        .. doctest::

            >>> @dispatch("bar")
            ... def foo(bar):
            ...     print("base")
            ...     return bar

            >>> @foo.overload("int")  # least specific
            ... def integer_foo(bar):
            ...     print("integer")
            ...     return bar

            >>> @foo.overload("int64[numpy]")  # most specific
            ... def numpy_int64_foo(bar):
            ...     print("int64[numpy]")
            ...     return bar

            >>> @foo.overload("int64")
            ... def int64_foo(bar):
            ...     print("int64")
            ...     return bar

            >>> for key, func in foo.overloaded.items():
            ...     print(f"{key}: {func}")
            (NumpyInt64Type(),): <function numpy_int64_foo at ...>
            (Int64Type(),): <function int64_foo at ...>
            (IntegerType(),): <function integer_foo at ...>
        """
        if not self._signature.ordered:
            self._signature.sort()

        return self._signature.dispatch_map

    def overload(self, *args, **kwargs) -> Callable:
        """A decorator that transforms a function into a dispatched
        implementation for this :class:`DispatchFunc <pdcast.DispatchFunc>`.

        Parameters
        ----------
        *args, **kwargs
            The type(s) to dispatch on.  These are provided as positional and
            keyword arguments that are bound to the decorated function's
            signature.  They have the same semantics as if the function
            were being invoked directly, and can be in any form recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        Callable
            The same function as was decorated.  This is not changed in any
            way.

        Raises
        ------
        TypeError
            If the decorated function does not accept the arguments specified
            in :func:`@dispatch() <pdcast.dispatch>`.

        Notes
        -----
        This decorator works just like the :meth:`register` method of
        :func:`singledispatch <python:functools.singledispatch>` objects,
        except that it does not interact with type annotations in any way.

        Instead, types are declared naturally in the decorator itself and
        bound to the function as if it were being invoked.  They can be
        supplied as either positional or keyword arguments, and can be in any
        format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Examples
        --------
        See the :ref:`API docs <dispatch.dispatched>` for example usage.
        """

        def implementation(func: Callable) -> Callable:
            """Attach a dispatched implementation to the DispatchFunc with the
            associated types.
            """
            signature = inspect.signature(func)

            # bind *args, **kwargs to the decorated function
            try:
                bound = signature.bind_partial(*args, **kwargs)
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

            # translate bound arguments into this signature's order
            bound.apply_defaults()
            key = self._signature.dispatch_key(**bound.arguments)

            # register every combination of types
            for path in self._signature.cartesian_product(key):
                self._signature[path] = func

            # remember dispatched function's signature
            self._signature.signatures[func] = signature

            return func

        return implementation

    def fallback(self, *args, **kwargs) -> Any:
        """A reference to the base implementation of the decorated function.

        Parameters
        ----------
        *args, **kwargs
            Positional and keyword arguments to supply to the base
            implementation.

        Returns
        -------
        Any
            The return value of the base implementation.

        Examples
        --------
        This allows direct access to the base implementation of the decorated
        function, bypassing the dispatch mechanism entirely.

        .. doctest::

            >>> @dispatch("bar")
            ... def foo(bar):
            ...     print("base")
            ...     return bar

            >>> @foo.overload("int")
            ... def integer_foo(bar):
            ...     print("integer")
            ...     return bar

            >>> foo(1)
            integer
            1
            >>> foo.fallback(1)
            base
            1
        """
        return self.__wrapped__(*args, **kwargs)

    def __call__(self, *args, **kwargs) -> Any:
        """Execute the decorated function, dispatching to an overloaded
        implementation if one exists.

        Parameters
        ----------
        *args, **kwargs
            Positional and keyword arguments to supply to the dispatched
            implementation.  The dispatched arguments are automatically
            extracted, normalized, and grouped according to their inferred
            type.

        Returns
        -------
        Any
            The return value of the dispatched implementation(s).

        TODO: expand notes and examples

        Notes
        -----
        This automatically detects aggregations, transformations, and
        filtrations based on the return value.

            *   A pandas :class:`Series <pandas.Series>` or
                :class:`DataFrame <pandas.DataFrame>`signifies a
                transformation.  Any missing indices will be replaced with NA.
            *   Anything else signifies an aggreggation.  Its result will be
                returned as-is if data is homogenous.  If mixed data is given,
                This will be a DataFrame with rows for each group.

        Examples
        --------
        TODO

        """
        # bind arguments
        bound = self._signature(*args, **kwargs)

        # fastpath: if calling from a recursive context, skip normalization
        if getattr(self._flags, "recursive", False):
            strategy = HomogenousDispatch(self, bound)
            return strategy.execute()  # do not finalize

        # normalize arguments
        bound.normalize()

        # choose strategy
        if not bound.is_composite:
            strategy = HomogenousDispatch(self, bound)
        else:
            strategy = CompositeDispatch(self, bound)

        # execute strategy
        self._flags.recursive = True
        try:
            return strategy.finalize(strategy.execute())
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


class DispatchSignature(Signature):
    """An ordered dictionary that stores types and their dispatched
    implementations for :class:`DispatchFunc` operations.
    """

    def __init__(
        self,
        func: Callable,
        dispatched: tuple[str, ...],
        cache_size: int = 128
    ):
        super().__init__(func=func)
        missing = [arg for arg in dispatched if arg not in self.parameter_map]
        if missing:
            raise TypeError(f"argument not recognized: {missing}")

        self.dispatched = dispatched
        self._dispatch_map = {}
        self._signatures = {func: self.signature}
        self.cache = LRUDict(maxsize=cache_size)
        self.ordered = False

    @property
    def dispatch_map(self) -> Mapping[tuple[types.VectorType, ...], Callable]:
        """A map containing all the dispatched implementations being handled by
        this :class:`DispatchSignature <pdcast.DispatchSignature>`.

        Returns
        -------
        MappingProxyType
            A read-only mapping from
            :meth:`dispatch_keys <pdcast.DispatchSignature.dispatch_key>` to
            their respective
            :meth:`implementations <pdcast.DispatchFunc.overload>`.

        See Also
        --------
        DispatchSignature.__getitem__ :
            Index the dispatch map for a particular combination of types.
        DispatchSignature.__setitem__ :
            Set an item within the dispatch map.
        DispatchSignature.__delitem__ :
            Delete an implementation from the map.

        Notes
        -----
        
        """
        return MappingProxyType(self._dispatch_map)

    @property
    def signatures(self) -> Mapping[Callable, inspect.Signature]:
        """A map containing the signatures of all the dispatched
        implementations that are being handled by this
        :class:`DispatchSignature <pdcast.DispatchSignature>`.

        Returns
        -------
        MappingProxyType
            A read-only mapping from
            :meth:`implementations <pdcast.DispatchFunc.overload>` to their
            respective :class:`signatures <inspect.Signature>`.

        Examples
        --------
        TODO
        """
        return self._signatures

    def sort(self) -> None:
        """Sort the dictionary into topological order, with most specific
        keys first.
        """
        keys = tuple(self._dispatch_map)

        # draw edges between keys according to their specificity
        edges = {key: set() for key in keys}
        for key1 in keys:
            for key2 in keys:
                if edge(key1, key2):
                    edges[key1].add(key2)

        # sort according to edges
        for key in topological_sort(edges):
            # NOTE: (Python 3.6+) equivalent to OrderedDict.move_to_end()
            item = self._dispatch_map.pop(key)
            self._dispatch_map[key] = item

        self.ordered = True

    def dispatch_key(self, *args, **kwargs) -> tuple[types.Type, ...]:
        """Create a dispatch key from the provided arguments.

        Parameters
        ----------
        *args, **kwargs
            Arbitrary positional and/or keyword arguments to bind to this
            signature.  Any that are marked as dispatched arguments for this
            :class:`DispatchSignature <pdcast.DispatchSignature>` will be
            extracted and resolved in a deterministic order.

        Returns
        -------
        tuple[types.Type, ...]
            A sequence of resolved types that can be used as a key to the
            signature's
            :attr:`dispatch_map <pdcast.DispatchSignature.dispatch_map>`.

        See Also
        --------
        DispatchSignature.cartesian_product :
            Expand composite keys into a cartesian product of all possible
            combinations.

        Notes
        -----
        :class:`composite <pdcast.CompositeType>` specifiers will be resolved
        as normal.  However, as they are not hashable, they cannot be stored in
        :attr:`dispatch_map <pdcast.dispatch_map>` directly.  Instead they must
        be expanded using
        :meth:`DispatchSignature.cartesian_product() <pdcast.DispatchSignature.cartesian_product>`
        and stored independently.

        Examples
        --------
        .. doctest::

            >>> TODO
        """
        bound = self.signature.bind_partial(*args, **kwargs)
        bound.apply_defaults()

        return tuple(resolve_type(bound.arguments[x]) for x in self.dispatched)

    def cartesian_product(
        self,
        key: tuple[types.Type, ...]
    ) -> Iterator[tuple[types.VectorType, ...]]:
        """Convert a composite
        :meth:`dispatch key <pdcast.DispatchSignature.dispatch_key>` into a
        Cartesian product containing all possible combinations.

        Parameters
        ----------
        key : tuple[types.Type, ...]
            A :meth:`dispatch key <pdcast.DispatchSignature.dispatch_key>` to
            parse.

        Returns
        -------
        Iterator[tuple[types.VectorType, ...]]
            A generator that yields non-composite dispatch keys representing
            every combination of the input types.

        See Also
        --------
        DispatchSignature.dispatch_key :
            Create a dispatch key by binding to the signature.

        Examples
        --------
        .. doctest::

            >>> TODO
        """
        # convert keys into CompositeTypes
        key = tuple(resolve_type([x]) for x in key)
        for path in itertools.product(*key):
            yield path

    def __call__(self, *args, **kwargs) -> DispatchArguments:
        """Bind this signature to create a `DispatchArguments` object."""
        bound = self.signature.bind_partial(*args, **kwargs)
        bound.apply_defaults()

        return DispatchArguments(bound=bound, signature=self)

    def __getitem__(
        self,
        key: type_specifier | tuple[type_specifier]
    ) -> Callable:
        """Search the :class:`DispatchSignature <pdcast.DispatchSignature>` for
        a particular implementation.
        """
        if not isinstance(key, tuple):
            key = (key,)

        # resolve key
        key = self.dispatch_key(**dict(zip(self.dispatched, key)))

        # trivial case: key has exact match
        if key in self._dispatch_map:
            return self._dispatch_map[key]

        # check for cached result
        if key in self.cache:
            return self.cache[key]

        # sort map
        if not self.ordered:
            self.cache.clear()
            self.sort()

        # search for first (sorted) match that fully contains key
        for dispatch_key, implementation in self._dispatch_map.items():
            if all(y.contains(x) for x, y in zip(key, dispatch_key)):
                self.cache[key] = implementation
                return implementation

        # no match found
        raise KeyError(tuple(str(x) for x in key))

    def __setitem__(self, key: type_specifier, value: Callable) -> None:
        """Add an overloaded implementation to the
        :class:`DispatchSignature <pdcast.DispatchSignature>`.
        """
        if not isinstance(key, tuple):
            key = (key,)

        # resolve key
        key = self.dispatch_key(**dict(zip(self.dispatched, key)))

        # verify implementation is callable
        if not callable(value):
            raise TypeError(f"implementation must be callable: {repr(value)}")

        # verify implementation accepts the dispatched arguments
        signature = inspect.signature(value)
        missing = [
            arg for arg in self.dispatched if arg not in signature.parameters
        ]
        if missing:
            func_name = f"'{value.__module__}.{value.__qualname__}()'"
            raise TypeError(
                f"{func_name} must accept dispatched arguments: {missing}"
            )

        # warn if overwriting a previous key
        if key in self._dispatch_map:
            warn_msg = (
                f"Replacing '{self.dispatch_map[key].__qualname__}()' "
                f"with '{value.__qualname__}()' for signature "
                f"{tuple(str(x) for x in key)}"
            )
            # warnings.warn(warn_msg, UserWarning, stacklevel=2)

        # insert into dispatch map
        self._dispatch_map[key] = value
        self.ordered = False

    def __delitem__(self, key: type_specifier) -> None:
        """Remove an overloaded implementation from the
        :class:`DispatchSignature <pdcast.DispatchSignature>`.
        """
        if not isinstance(key, tuple):
            key = (key,)

        # resolve key
        key = self.dispatch_key(**dict(zip(self.dispatched, key)))

        # require exact match
        if key in self._dispatch_map:
            del self._dispatch_map[key]
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


class DispatchArguments(Arguments):
    """A simple wrapper for an `inspect.BoundArguments` object with extra
    context for dispatched arguments and their detected types.
    """

    def __init__(
        self,
        bound: inspect.BoundArguments,
        signature: DispatchSignature,
        detected: dict[str, types.Type] | None = None,
        normalized: bool = False
    ):
        super().__init__(bound=bound, signature=signature)
        self.dispatched = {
            arg: bound.arguments[arg] for arg in self.signature.dispatched
        }

        # cached in .types
        self._types = detected

        # cached in .normalize()
        self.normalized = normalized
        self.series_names = {}
        self.frame = None
        self.original_index = None
        self.hasnans = None

    # TODO: make sure CompositeDispatch works without frame/hasnans/etc.
    # These will be blocked for any DispatchArguments that are spawned in
    # .groups

    @property
    def types(self) -> dict[str, types.Type]:
        """The detected type of each dispatched argument."""
        if self._types is None:
            self._types = {
                arg: detect_type(val) for arg, val in self.dispatched.items()
            }
        return self._types

    @property
    def key(self) -> tuple[types.Type, ...]:
        """Form a key for indexing a DispatchFunc."""
        return tuple(self.types.values())

    @property
    def is_composite(self) -> bool:
        """Check if the dispatched arguments are composite."""
        return any(isinstance(typ, types.CompositeType) for typ in self.key)

    @property
    def groups(self) -> Iterator[DispatchArguments]:
        """Group vectors by type.

        This only applies if `is_composite=True`.
        """
        if not self.is_composite:
            raise RuntimeError("DispatchArguments are not composite")

        # generate frame of types observed at each index
        type_frame = pd.DataFrame({
            arg: getattr(typ, "index", typ) for arg, typ in self.types.items()
        })

        # groupby() to get indices of each group
        groupby = type_frame.groupby(list(type_frame.columns), sort=False)
        del type_frame  # free memory

        # extract groups one by one
        for key, indices in groupby.groups.items():
            group = self.frame.iloc[indices]

            # bind names to key
            if not isinstance(key, tuple):
                key = (key,)
            detected = dict(zip(group.columns, key))

            # split group into vectors and rectify their dtypes
            vectors = self._rectify(self._extract_vectors(group), detected)

            # generate new BoundArguments for each group
            bound = type(self.bound)(
                arguments=self.bound.arguments | vectors,
                signature=self.bound.signature
            )

            # convert to DispatchArguments and yield
            yield DispatchArguments(
                bound=bound,
                signature=self.signature,
                detected=detected,
                normalized=True  # block normalize() calls
            )

    def normalize(self) -> None:
        """Extract vectors from dispatched arguments and normalize them."""
        # ensure normalize() is only called once
        if self.normalized:
            raise RuntimeError("DispatchArguments have already been normalized")

        # ensure types have not yet been detected
        if self._types is not None:
            raise RuntimeError("types were inferred before normalizing")

        # extract vectors from dispatched args and bind them to a DataFrame
        self._build_frame()

        # convert index to RangeIndex
        self._replace_index()

        # drop missing values
        self._dropna()  # TODO: should be optional

        # split DataFrame into vectors and replace original args
        vectors = self._extract_vectors(self.frame)
        self.bound.arguments |= vectors

        # rectify dtypes.  NOTE: implicitly detects type of each arg
        if not self.is_composite:
            self.bound.arguments |= self._rectify(vectors, self.types)

        # mark as normalized
        self.normalized = True

    def _build_frame(self) -> None:
        """Extract vectors from dispatched arguments and bind them into a
        shared DataFrame.
        """
        # extract vectors
        vectors = {}
        for arg, value in self.dispatched.items():
            if isinstance(value, pd.Series):
                vectors[arg] = value
                self.series_names[arg] = value.name
            elif isinstance(value, np.ndarray):
                vectors[arg] = value
            else:
                value = np.asarray(value, dtype=object)
                if value.shape:
                    vectors[arg] = value

        # bind vectors into DataFrame
        self.frame = pd.DataFrame(vectors)

        # NOTE: the DataFrame constructor will happily accept vectors with
        # misaligned indices, filling any absent values with NaNs.  Since this
        # is a likely source of bugs, we always warn the user when it happens.

        # warn if indices were misaligned
        original_length = self.frame.shape[0]
        if any(vec.shape[0] != original_length for vec in vectors.values()):
            warn_msg = (
                f"{self.__qualname__}() - vectors have misaligned indices"
            )
            warnings.warn(warn_msg, UserWarning, stacklevel=3)

    def _replace_index(self) -> None:
        """Normalize the indices of the dispatched vectors.
        """
        self.original_index = self.frame.index
        if not isinstance(self.original_index, pd.RangeIndex):
            self.frame.index = pd.RangeIndex(0, self.frame.shape[0])

    def _dropna(self) -> None:
        """Drop missing values from the dispatched vectors.
        """
        original_length = self.frame.shape[0]
        self.frame.dropna(how="any")
        self.hasnans = self.frame.shape[0] != original_length

    def _extract_vectors(self, df: pd.DataFrame) -> dict[str, pd.Series]:
        """Split a DataFrame into individual vectors.
        """
        return {
            arg: col.rename(self.series_names.get(arg, None), copy=False)
            for arg, col in df.items()
        }

    def _rectify(
        self,
        vectors: dict[str, pd.Series],
        detected: dict[str, types.VectorType]
    ) -> dict[str, pd.Series]:
        """Convert the vectors to the detected types.
        """
        return {
            arg: series.astype(detected[arg].dtype, copy=False)
            for arg, series in vectors.items()
        }


def supercedes(node1: tuple, node2: tuple) -> bool:
    """Check if node1 is consistent with and strictly more specific than node2.
    """
    return all(x.contains(y) for x, y in zip(node2, node1))


def edge(node1: tuple, node2: tuple) -> bool:
    """If ``True``, check node1 before node2.

    Ties are broken by recursively backing off the last element of both
    signatures.  As a result, whichever one is more specific in its earlier
    elements will always be preferred.
    """
    # pylint: disable=arguments-out-of-order
    if not node1 or not node2:
        return False

    return (
        supercedes(node1, node2) and
        not supercedes(node2, node1) or
        edge(node1[:-1], node2[:-1])  # back off from right to left
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

            # remove edge from inverted map and decrement edge count
            inverted[dependent].remove(node)
            edge_count -= 1

            # if dependent has no more incoming edges, add it to the queue
            if not inverted[dependent]:
                no_incoming.append(dependent)
                del inverted[dependent]  # no reason to keep an empty set

    # if there are any edges left, then there must be a cycle
    if edge_count:
        cycles = [node for node in edges if inverted.get(node, None)]
        raise ValueError(f"edges are cyclic: {cycles}")

    return result


##########################
####    STRATEGIES    ####
##########################


# TODO: document these


class DispatchStrategy:
    """Interface for Strategy pattern dispatch pipelines.


    """

    def __init__(
        self,
        func: DispatchFunc,
        arguments: DispatchArguments
    ):
        self.func = func
        self.signature = func._signature
        self.arguments = arguments

    def execute(self) -> Any:
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

    def replace_na(
        self,
        series: pd.Series,
        dtype: dtype_like
    ) -> pd.Series:
        """Abstract method to replace missing values in the result of a
        dispatched strategy.
        """
        original_length = self.arguments.original_index.shape[0]

        result = pd.Series(
            np.full(original_length, dtype.na_value, dtype=object),
            index = pd.RangeIndex(0, original_length),
            name=series.name,
            # dtype=object
            dtype=dtype
        )
        result[series.index] = series
        # return result.astype(dtype, copy=False)
        return result


# TODO: add transform() and aggregate() methods


class HomogenousDispatch(DispatchStrategy):
    """Dispatch homogenous inputs to the appropriate implementation."""

    def execute(self) -> Any:
        """Call the dispatched function with the bound arguments."""
        # search for dispatched implementation
        func = self.func[self.arguments.key]

        # flatten *args and **kwargs
        arg_names = tuple(self.arguments.signature.parameter_map)
        kwargs = dict(zip(arg_names, self.arguments.args))
        kwargs |= self.arguments.kwargs

        # translate flatten arguments to the dispatched function's signature
        signature = self.signature.signatures[func]
        bound = signature.bind(**kwargs)

        # call the dispatched function with the translated arguments
        return func(*bound.args, **bound.kwargs)

    def finalize(self, result: Any) -> Any:
        """Infer mode of operation (filter/transform/aggregate) from return
        type and adjust result accordingly.
        """
        context = self.arguments

        # transform
        if isinstance(result, (pd.Series, pd.DataFrame)):
            # warn if final index is not a subset of original
            if not result.index.difference(context.frame.index).empty:
                warn_msg = (
                    f"index mismatch in {self.__qualname__}() with signature"
                    f"{tuple(str(x) for x in context.key)}: dispatched "
                    f"implementation did not return a like-indexed Series/"
                    f"DataFrame"
                )
                warnings.warn(warn_msg, UserWarning, stacklevel=4)
                context.hasnans = True

            # replace missing values, aligning on index
            if context.hasnans or result.shape[0] < context.frame.shape[0]:
                output_type = detect_type(result)
                if isinstance(result, pd.Series):
                    nullable = output_type.make_nullable()
                    result = self.replace_na(result, nullable.dtype)
                else:
                    with_na = {}
                    for col, series in result.items():
                        nullable = output_type[col].make_nullable()
                        with_na[col] = self.replace_na(series, nullable.dtype)
                    result = pd.DataFrame(with_na)

            # replace original index
            result.index = context.original_index

        # aggregate
        return result


class CompositeDispatch(DispatchStrategy):
    """Dispatch composite inputs to the appropriate implementations."""

    def execute(self) -> list:
        """For each group in the input, call the dispatched implementation
        with the bound arguments.
        """
        results = []

        # process each group independently
        for group in self.arguments.groups:
            strategy = HomogenousDispatch(self.func, group)
            results.append((group.detected, strategy.execute()))

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
                final = self.replace_na(final, nullable.dtype)
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


#######################
####    TESTING    ####
#######################


@dispatch("x", "y")
def add(x, y):
    return x + y


@add.overload("int", "int")
def add1(x, y):
    return x - y


# @add.overload("int64", "int64")
# def add2(x, y):
#     return x + y


# @add.overload("float", "float")
# def add3(x, y):
#     return x + y



sig = add._signature
bound = sig([1, 2, 3.0], [3, 2, 1])

# strat = DirectDispatch(add)




# @dispatch("bar")
# def foo(bar):
#     print("base")
#     return bar

# @foo.overload("int")  # least specific
# def integer_foo(bar):
#     print("integer")
#     return bar

# @foo.overload("int64[numpy]")  # most specific
# def numpy_int64_foo(bar):
#     print("int64[numpy]")
#     return bar

# @foo.overload("int64")
# def int64_foo(bar):
#     print("int64")
#     return bar
