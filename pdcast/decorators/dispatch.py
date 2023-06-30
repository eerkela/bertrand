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

from .base import KINDS, Arguments, FunctionDecorator, Signature


# TODO: emit a warning whenever an implementation is replaced.
# -> use a simplefilter when the module is loaded, or implement None as a
# wildcard.  This would get expanded to registry.roots at runtime.


# TODO: None wildcard value?
# -> use registry wildcards instead

# TODO: result is None -> fill with NA?
# -> probably not.  Just return an empty series if filtering.


# TODO: pdcast.cast("today", "datetime") uses python datetimes rather than
# pandas
# -> this because of an errant .larger lookup that made it through the
# refactor.


# TODO: consider empty series during dispatch.
# -> pdcast.cast([None, None, None], int)
# Traceback (most recent call last):
#     ...
# AttributeError: 'NoneType' object has no attribute 'dtype'


######################
####    PUBLIC    ####
######################


def dispatch(
    *args,
    vectorize: bool = True,
    allow_composite: bool = True,
    drop_na: bool = True,
    replace_na: bool = True,
    convert_mixed: bool = False,
    cache_size: int = 128
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
    """A wrapper for a function that can dispatch to a collection of virtual
    implementations based on the inferred type of its arguments.

    Parameters
    ----------
    func : Callable
        The decorated function or other callable.
    dispatched : tuple[str, ...]
        The names of the arguments that the function dispatches on.  These
        must occur within the decorated function's signature, and their order
        determines the order of the keys under which implementations are
        searched and stored.
    cache_size : int, default 128
        Implementation searches are performed every time the decorated function
        is executed, which may be expensive depending on how many
        implementations are being dispatched to and how often the function is
        being called.  To mitigate this,
        :class:`DispatchFuncs <pdcast.dispatch.DispatchFunc>` maintain an LRU
        cache containing the ``n`` most recently requested implementations,
        where ``n`` is equal to the value of this argument.  This cache is
        always checked before performing a full search on the
        :attr:`overloaded <pdcast.DispatchFunc.overloaded>` table itself.

    TODO: document drop_na and convert_mixed + other flags as they are added.

    Examples
    --------
    See the docs for :func:`@dispatch <pdcast.dispatch>` for example usage.
    """

    _reserved = (
        FunctionDecorator._reserved |
        {"_signature", "_flags", "_drop_na", "_convert_mixed"}
    )

    def __init__(
        self,
        func: Callable,
        dispatched: tuple[str, ...],
        cache_size: int,
        drop_na: bool,
        convert_mixed: bool
    ):
        super().__init__(func=func)
        self._flags = threading.local()
        self._signature = DispatchSignature(
            func,
            dispatched=dispatched,
            cache_size=cache_size
        )
        self._drop_na = drop_na
        self._convert_mixed = convert_mixed

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
        The order of these names is significant, as it determines the order of
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
        from left to right.  In the case above, this means we always choose
        ``foo2()`` over ``foo1()``.  This behavior can be reversed by changing
        the order of the arguments to :func:`@dispatch() <pdcast.dispatch>`.

        .. code:: python

            @dispatch("b", "a")  # reversed
            def foo(a, b):
                ...

            @foo.overload("int", "int[python]")  # same order (a, b)
            def foo1(a, b):
                ...

            @foo.overload("int[python]", "int")  # same order (a, b)
            def foo2(a, b):
                ...

        We will now always prefer ``foo1()`` over ``foo2()``.

        More complicated priorities can be assigned if a function has 3 or
        more arguments.

        .. note::

            The argument order given in in :func:`@dispatch() <pdcast.dispatch>`
            **does not affect** the signature of
            :meth:`@overload() <pdcast.DispatchFunc.overload>` in any way.  The
            latter always parses its arguments relative to the function that it
            decorates.

            At an implementation level,
            :meth:`@overload() <pdcast.DispatchFunc.overload>` binds its
            arguments directly to the decorated function's signature.  It then
            extracts the dispatched arguments by name, resolves them, and sorts
            them into the order specified by
            :func:`@dispatch() <pdcast.dispatch>`.  All changing this order
            does is reverse the keys that are used to index the
            :class:`DispatchFunc <pdcast.DispatchFunc>`, and thereby consider
            them from right to left (``b`` before ``a``) instead of left to
            right (``a`` before ``b``).

        """
        return self._signature.dispatched

    @property
    def overloaded(self) -> Mapping[tuple[types.VectorType, ...], Callable]:
        """A map connecting :doc:`types </content/types/types>` to their
        :meth:`overloaded <pdcast.DispatchFunc.overload>` implementations.

        Returns
        -------
        Mapping[tuple[types.VectorType, ...], Callable]
            A read-only dictionary mapping types to their associated
            implementations.  The keys are tuples that are sorted into the same
            order as the :attr:`dispatched <pdcast.DispatchFunc.dispatched>`
            arguments.

        Notes
        -----
        The returned mapping is `topologically sorted
        <https://en.wikipedia.org/wiki/Topological_sorting>`_ according to
        specificity.  Iterating through the map equates to searching it from
        most to least specific.

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
        # sort the dispatch map if it hasn't been sorted already.  Usually,
        # this is done lazily the first time the function is called.
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
            If the decorated function does not accept the arguments named in
            :func:`@dispatch() <pdcast.dispatch>` (order doesn't matter).

        Notes
        -----
        This decorator works just like the :meth:`register` method of
        :func:`singledispatch <python:functools.singledispatch>` functions,
        except that it does not interact with type annotations in any way.

        Instead, types are declared naturally through the decorator itself and
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
            signature = Signature(func)

            # bind *args, **kwargs to the decorated function
            try:
                bound = signature(*args, **kwargs)
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
            key = self._signature.dispatch_key(*bound.args, **bound.kwargs)

            # remember dispatched function's signature
            self._signature.signatures[func] = signature

            # register every combination of types
            for path in self._signature.cartesian_product(key):
                self._signature[path] = func

            return func

        return implementation

    def fallback(self, *args, **kwargs) -> Any:
        """A reference to the base implementation of the dispatch function.

        Parameters
        ----------
        *args, **kwargs
            Positional and keyword arguments to supply to the base
            implementation.  These are passed through directly to the base
            function.

        Returns
        -------
        Any
            The return value of the base implementation.

        Examples
        --------
        This allows direct access to the base implementation of a
        :class:`DispatchFunc <pdcast.DispatchFunc>`, bypassing the dispatch
        mechanism entirely.

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
        """Execute the :class:`DispatchFunc <pdcast.DispatchFunc>`, searching
        for an overloaded implementation.

        Parameters
        ----------
        *args, **kwargs
            Positional and keyword arguments to supply to the virtual
            implementation.  The
            :attr:`dispatched <pdcast.DispatchFunc.dispatched>` arguments are
            automatically extracted from these and analyzed to determine the
            most specific implementation to use.  They may also include
            vectors, which are normalized and grouped according to their
            inferred type.

        Returns
        -------
        Any
            The return value of the dispatched implementation(s).  If any of
            the arguments were vectorized, then this value will be analyzed for
            aggregations, transformations, and filtrations.  See the notes
            below for more information.

        See Also
        --------
        DispatchFunc.__getitem__ :
            Access a specific implementation of the dispatch function.

        Notes
        -----
        :func:`@dispatch() <pdcast.dispatch>` automatically intercepts any
        vectors that are supplied to the
        :attr:`dispatched <pdcast.DispatchFunc.dispatched>` arguments and
        converts them into properly-formatted
        :class:`pandas.Series <pandas.Series>` objects.  These are always
        normalized and labeled with an appropriate ``dtype`` before being
        passed into the dispatched function itself.  Scalars are passed through
        as-is.

        The steps that are taken to normalize input vectors are as follows:

            #.  Convert each vector into a
                :class:`pandas.Series <pandas.Series>` object if it is not one
                already.  If a vector does not have an explicit ``dtype``, then
                it is treated as a ``dtype: object`` series.
            #.  Bind each :class:`Series <pandas.Series>` into a single
                :class:`DataFrame <pandas.DataFrame>` with a column for each
                argument.
            #.  Normalize the frame's collective index to be a
                :class:`RangeIndex <pandas.RangeIndex>` spanning the length of
                each vector.  If the vectors had a custom index, then it is
                stored until the end of the dispatch process.
            #.  Drop any row that contains a missing value in one or more
                columns.  This creates gaps in the index, which are used to
                identify the locations of missing values later on in the
                dispatch process.
            #.  Detect the type of each vector and label it accordingly.  This
                allows :func:`detect_type() <pdcast.detect_type>` to infer each
                vector's type in constant time within the dispatched context.
                If any of the vectors contain data of mixed type, then the
                whole frame is split into groups based on the observed type at
                every index.  Each group is then passed through individually
                and combined afterwards.
            #.  Break the frame back into its constituent vectors, replacing
                their original values in ``*args`` and ``**kwargs``.

        The dispatched implementation is then invoked with the normalized
        arguments, and its return value is analyzed for aggregations,
        transformations, and filtrations.  These are determined based on the
        type of the return value.

            *   A pandas :class:`Series <pandas.Series>` or
                :class:`DataFrame <pandas.DataFrame>` signifies a
                transformation.  Upon exiting the dispatched context, any
                missing indices will be replaced with the appropriate
                :attr:`na_value <pdcast.ScalarType.na_value>` for the inferred
                output type.  If the arguments defined a custom index, then it
                will be replaced, and the output ``dtype`` will be labeled with
                the type of its elements.
            *   :data:`None <python:None>` signifies a filtration.  In most
                cases, this will be passed through as-is.  However, if the
                input includes a vector of mixed type, then the group will
                be removed from the output.
            *   Anything else signifies an aggreggation.  The result will be
                returned as-is if the data are homogenous.  If mixed data are
                given, the result will be a
                :class:`DataFrame <pandas.DataFrame>` with columns containing
                the observed type of each argument for every group.  The final
                column contains the result of the computation for that group.

        These behaviors can be customized using the keyword arguments to
        :func:`@dispatch() <pdcast.dispatch>`.  See the
        :ref:`documentation <dispatch>` for more details.

        Examples
        --------
        See the :ref:`API docs <dispatch>` for example usage.
        """
        # bind arguments
        bound = self._signature(*args, **kwargs)

        # fastpath: if calling from a recursive context, skip normalization
        if getattr(self._flags, "recursive", False):
            strategy = HomogenousDispatch(
                self,
                arguments=bound,
                drop_na=self._drop_na
            )
            return strategy.execute()  # do not finalize

        # normalize arguments
        bound.normalize()

        # choose strategy
        if not bound.is_composite:
            strategy = HomogenousDispatch(
                self,
                arguments=bound,
                drop_na=self._drop_na
            )
        else:
            strategy = CompositeDispatch(
                self,
                arguments=bound,
                drop_na=self._drop_na,
                convert_mixed=self._convert_mixed,
            )

        # execute strategy
        self._flags.recursive = True
        try:
            return strategy.finalize(strategy.execute())
        finally:
          self._flags.recursive = False

    def __getitem__(
        self,
        key: type_specifier | tuple[type_specifier, ...]
    ) -> Callable:
        """Get the dispatched implementation for arguments of a given type.

        Parameters
        ----------
        key : type_specifier | tuple[type_specifier, ...]
            The type of input to get the implementation for.  Multiple types
            can be given by separating them with commas.

        Returns
        -------
        Callable
            The implementation that will be chosen when the function is invoked
            with the given type(s).  If no specific implementation is found
            for the associated types, then a reference to the default
            implementation is returned.

        See Also
        --------
        DispatchFunc.__call__ :
            Call the :class:`DispatchFunc <pdcast.DispatchFunc>` with the
            associated implementation.

        Notes
        -----
        This always returns the same implementation that is chosen when the
        dispatch mechanism is executed.  In fact, the mechanism simply calls
        this method to retrieve the implementation in the first place.

        Examples
        --------
        Types must be supplied in the same order as the
        :attr:`dispatched <pdcast.DispatchFunc.dispatched>` arguments.

        .. doctest::

            >>> pdcast.cast[int, int]
            <function integer_to_integer at ...>
            >>> pdcast.cast[float, bool]
            <function float_to_boolean at ...>
            >>> pdcast.cast[int, "datetime[pandas, US/Pacific]"]
            <function integer_to_pandas_timestamp at ...>
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
        cache_size: int
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

    # pylint: disable=no-self-argument
    def dispatch_key(__self, *args, **kwargs) -> tuple[types.Type, ...]:
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
        bound = __self.signature.bind_partial(*args, **kwargs)
        bound.apply_defaults()

        return tuple(
            resolve_type(bound.arguments[x]) for x in __self.dispatched
        )

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

    def __setitem__(
        self,
        key: tuple[type_specifier, ...],
        value: Callable
    ) -> None:
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

        # verify implementation has compatible signature
        signature = self.signatures[value]
        if not self.compatible(signature):
            self_str = self.reconstruct(
                defaults=False,
                annotations=False,
                return_annotation=False
            )
            other_str = signature.reconstruct(
                defaults=False,
                annotations=False,
                return_annotation=False
            )
            raise TypeError(
                f"signatures are not compatible: '{other_str}' is not an "
                f"extension of '{self_str}'"
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
            detected = dict(zip(self.dispatched, key))

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
        self.frame = self.frame.dropna(how="any")
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

    def rectify(self, series: pd.Series) -> pd.Series:
        """Normalize the dtype of an output series to its detected type.
        """
        return series.astype(detect_type(series).dtype, copy=False)

    def replace_na(self, series: pd.Series) -> pd.Series:
        """Abstract method to replace missing values in the result of a
        dispatched strategy.
        """
        # get nullable type for series
        nullable = detect_type(series).make_nullable()

        # get length of vectors before normalizing
        original_length = self.arguments.original_index.shape[0]

        # build empty series containing only missing values
        result = pd.Series(
            np.full(original_length, nullable.na_value, dtype=object),
            index = pd.RangeIndex(0, original_length),
            name=series.name,
            # dtype=object  # TODO: decide which of these are best
            dtype=nullable.dtype
        )

        # merge result into the empty series
        result[series.index] = series
        # return result.astype(nullable.dtype, copy=False)  # TODO: same as above
        return result


class HomogenousDispatch(DispatchStrategy):
    """Dispatch homogenous inputs to the appropriate implementation."""

    def __init__(
        self,
        func: DispatchFunc,
        arguments: DispatchArguments,
        drop_na: bool,
    ):
        super().__init__(func=func, arguments=arguments)
        self.drop_na = drop_na

    def execute(self) -> Any:
        """Call the dispatched function with the bound arguments."""
        bound = self.arguments
        return self.func[bound.key](*bound.args, **bound.kwargs)

    def finalize(self, result: Any) -> Any:
        """Infer mode of operation (filter/transform/aggregate) from return
        type and adjust result accordingly.
        """
        # transform
        if isinstance(result, (pd.Series, pd.DataFrame)):
            # rectify output dtype
            result = self._standardize_dtype(result)

            # check if index is subset of normalized
            self._check_index(result)

            # replace missing values
            frame_length = self.arguments.frame.shape[0]
            if self.arguments.hasnans or result.shape[0] < frame_length:
                result = self._merge_na(result)

            # replace original index
            result.index = self.arguments.original_index

        # aggregate
        return result

    def _standardize_dtype(
        self,
        result: pd.Series | pd.DataFrame
    ) -> pd.Series | pd.DataFrame:
        """Rectify the dtype of a series to match the detected output."""
        # series
        if isinstance(result, pd.Series):
            return self.rectify(result)

        # dataframe
        return pd.DataFrame(
            {col: self.rectify(series) for col, series in result.items()},
            copy=False
        )

    def _check_index(self, result: pd.Series | pd.DataFrame) -> None:
        """Check that the index is a subset of the original."""
        if not result.index.difference(self.arguments.frame.index).empty:
            sig = [f"{k}: {str(v)}" for k, v in self.arguments.types.items()]
            warn_msg = (
                f"index mismatch in '{self.__qualname__}({', '.join(sig)})': "
                f"final index is not a subset of starting index"
            )
            warnings.warn(warn_msg, UserWarning, stacklevel=4)

            # index mismatch results in extraneous NaNs
            self.arguments.hasnans = True

    def _merge_na(
        self,
        result: pd.Series | pd.DataFrame
    ) -> pd.Series | pd.DataFrame:
        """Replace missing values.
        """
        # series
        if isinstance(result, pd.Series):
            return self.replace_na(result)

        # dataframe
        return pd.DataFrame(
            {col: self.replace_na(series) for col, series in result.items()},
            copy=False
        )


class CompositeDispatch(DispatchStrategy):
    """Dispatch composite inputs to the appropriate implementations."""

    def __init__(
        self,
        func: DispatchFunc,
        arguments: DispatchArguments,
        drop_na: bool,
        convert_mixed: bool
    ):
        super().__init__(func=func, arguments=arguments)
        self.drop_na = drop_na
        self.convert_mixed = convert_mixed

    def execute(self) -> list:
        """For each group in the input, call the dispatched implementation
        with the bound arguments.
        """
        results = []

        # process each group independently
        for group in self.arguments.groups:
            strategy = HomogenousDispatch(
                self.func,
                arguments=group,
                drop_na=self.drop_na
            )
            results.append((group.key, strategy.execute()))

        return results

    def finalize(self, result: list) -> Any:
        groups = dict(result)

        # transform
        as_series = all(isinstance(grp, pd.Series) for grp in groups.values())
        as_df = all(isinstance(grp, pd.DataFrame) for grp in groups.values())
        if as_series or as_df:
            # rectify output dtype
            groups = self._standardize_dtype(groups, as_series=as_series)

            # check if index is subset of normalized
            self._check_index(groups)

            # merge series and replace missing values
            if as_series:
                return self._merge_series(groups)  # TODO: include a flag to convert mixed

            # check for column mismatch
            columns = {tuple(df.columns) for df in groups.values()}
            if len(columns) > 1:
                raise ValueError(
                    f"DataFrames do not share the same column names {columns}"
                )

            # merge each column and replace missing values
            df = {}
            for col in columns.pop():
                df[col] = self._merge_series(
                    {key: df[col] for key, df in groups.items()}
                )
            return pd.DataFrame(df, copy=False)

        # aggregate
        df = {}
        for idx, arg_name in enumerate(self.arguments.dispatched):
            df[arg_name] = [key[idx] for key in groups]
        df[f"{self.func.__name__}()"] = list(groups.values())
        return pd.DataFrame(df, copy=False)

    def _standardize_dtype(
        self,
        groups: dict[tuple[types.VectorType, ...], pd.Series | pd.DataFrame],
        as_series: bool
    ) -> dict[tuple[types.VectorType, ...], pd.Series | pd.DataFrame]:
        """Rectify each group to match the detected output.
        """
        # series
        if as_series:
            return {
                key: self.rectify(grp) for key, grp in groups.items()
            }

        # dataframe
        return {
            key: pd.DataFrame(
                {col: self.rectify(series) for col, series in grp.items()},
                copy=False
            )
            for key, grp in groups.items()
        }

    def _check_index(
        self,
        groups: dict[tuple[types.VectorType, ...], pd.Series | pd.DataFrame]
    ) -> None:
        """Validate and merge a collection of transformed Series/DataFrame
        indices.
        """
        # merge indices
        group_iter = iter(groups.values())
        index = next(group_iter).index
        size = index.shape[0]
        for grp in group_iter:
            index = index.union(grp.index)
            if len(index) < size + grp.shape[0]:
                sig = [
                    f"{k}: {str(v)}" for k, v in self.arguments.types.items()
                ]
                warn_msg = (
                    f"index collision in '{self.__qualname__}("
                    f"{', '.join(sig)})': 2 or more implementations returned "
                    f"overlapping indices"
                )
                warnings.warn(warn_msg, UserWarning)
            size = index.shape[0]

        # check that final index is subset of starting index
        if not index.difference(self.arguments.frame.index).empty:
            sig = [f"{k}: {str(v)}" for k, v in self.arguments.types.items()]
            warn_msg = (
                f"index mismatch in '{self.__qualname__}({', '.join(sig)})': "
                f"final index is not a subset of starting index"
            )
            warnings.warn(warn_msg, UserWarning, stacklevel=4)

            # index mismatch results in extraneous NaNs
            self.arguments.hasnans = True

    def _merge_series(
        self,
        groups: dict[tuple[types.VectorType, ...], pd.Series]
    ) -> pd.Series:
        """Merge the computed series results by index."""
        # get unique output types
        unique = set(detect_type(series) for series in groups.values())

        # if all types are in same family, attempt to standardize to widest
        if (
            self.convert_mixed and len(unique) > 1 and any(
                all(t2.contains(t1) for t1 in unique)
                for t2 in {t.generic.root for t in unique}
            )
        ):
            from pdcast.convert import cast
            widest = max(unique)  # using comparison operators
            try:
                groups = {
                    key: cast(grp, widest, downcast=False, errors="raise")
                    for key, grp in groups.items()
                }
                unique = {widest}
            except Exception:
                pass

        # results are homogenous
        if len(unique) == 1:
            # merge series
            result = pd.concat(groups.values())  # NOTE: can remove ObjectDtype
            result = result.astype(unique.pop().dtype, copy=False)
            result.sort_index(inplace=True)

            # replace missing values
            frame_length = self.arguments.frame.shape[0]
            if self.arguments.hasnans or result.shape[0] < frame_length:
                result = self.replace_na(result)

            # replace original index
            result.index = self.arguments.original_index
            return result

        # NOTE: we can't use pd.concat() because it tends to coerce mixed-type
        # results in uncontrollable ways.  Instead, we fold each group into a
        # `dtype: object` series to preserve the actual values.

        # results are mixed
        original_length = self.arguments.original_index.shape[0]
        result = pd.Series(
            np.full(original_length, pd.NA, dtype=object),
            index=pd.RangeIndex(0, original_length)
        )
        for series in groups.values():
            result[series.index] = series
        result.index = self.arguments.original_index
        return result


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
