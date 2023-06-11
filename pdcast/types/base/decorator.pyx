"""This module describes an ``DecoratorType`` object, which can be subclassed
to create a dynamic wrapper around an ``ScalarType``.
"""
from types import MappingProxyType
from typing import Any, Iterator

import pandas as pd

from pdcast import resolve  # importing directly causes an ImportError
from pdcast.util.type_hints import dtype_like, type_specifier

from .registry cimport Type
from .scalar cimport ScalarType
from .vector cimport VectorType
from .composite cimport CompositeType


# TODO: DecoratorType.contains should absorb some of the logic of its
# subtypes.
# -> treat base instances as wildcards.


cdef class DecoratorType(VectorType):
    """Base class for all `Decorator pattern
    <https://en.wikipedia.org/wiki/Decorator_pattern>`_ type objects.

    These are used to dynamically modify the behavior of another type, for
    instance marking it as sparse or categorical.  They can be nested to
    form a `singly-linked list <https://en.wikipedia.org/wiki/Linked_list>`_
    that can be iteratively unwrapped using a type's
    :attr:`.decorators <DecoratorType.decorators>` attribute.  Conversions and
    other type-related functionality will automatically take these decorators
    into account, unwrapping them to the appropriate level before re-packaging
    the result.

    Parameters
    ----------
    wrapped : ScalarType | DecoratorType, optional
        A type to wrap.  This may be another
        :class:`DecoratorType <pdcast.DecoratorType>`, in which case they are
        nested to form a singly-linked list.
    **kwargs : dict
        Parametrized keyword arguments describing metadata for this type.  This
        is conceptually equivalent to the ``_metadata`` field of pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.
    """

    def __init__(self, wrapped: VectorType = None, **kwargs):
        super().__init__(wrapped=wrapped, **kwargs)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, wrapped: str = None, *args: str) -> DecoratorType:
        """Construct a :class:`DecoratorType <pdcast.DecoratorType>` from a
        string in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Parameters
        ----------
        wrapped : str, optional
            The type to be wrapped.  If given, this must be another valid type
            specifier in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        DecoratorType
            An instance of the associated type.

        See Also
        --------
        Type.from_string :
            For more information on how this method is called.

        Examples
        --------
        These types wrap other types as their first parameter.

        .. doctest::

            >>> pdcast.resolve_type("int64")
            Int64Type()
            >>> pdcast.resolve_type("sparse[int64]")
            SparseType(wrapped=Int64Type(), fill_value=None)

        If a wrapped type is not given, then this method will return a naked
        :class:`DecoratorType <pdcast.DecoratorType>`, meaning that it is not
        backed by an associated :class:`ScalarType <pdcast.ScalarType>`.

        .. doctest::

            >>> pdcast.resolve_type("sparse")
            SparseType(wrapped=None, fill_value=None)

        These act as wildcards during :func:`cast() <pdcast.cast>` operations,
        automatically acquiring the inferred type of the input data.

        .. doctest::

            >>> pdcast.cast([1, 2, 3], "sparse")
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]

        They also match any equivalently-decorated type during
        :meth:`contains() <pdcast.DecoratorType.contains>` checks.

        .. doctest::

            >>> pdcast.resolve_type("sparse").contains("sparse[int64]")
            True
        """
        if wrapped is None:
            return self

        cdef VectorType instance = resolve.resolve_type(wrapped)

        return self(instance, *args)

    def from_dtype(self, dtype: dtype_like) -> DecoratorType:
        """Construct a :class:`DecoratorType <pdcast.DecoratorType>` from a
        numpy/pandas :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            A numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.

        Returns
        -------
        DecoratorType
            An instance of the associated type.

        See Also
        --------
        Type.from_dtype :
            For more information on how this is called.

        Examples
        --------
        Pandas implements its own
        :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>` for
        :class:`sparse <pandas.SparseDtype>` and
        :class:`categorical <pandas.CategoricalDtype>` data.  This method can
        translate these directly into the ``pdcast`` type system.

        .. doctest::

            >>> import pandas as pd

            >>> pdcast.resolve_type(pd.SparseDtype("int64"))
            SparseType(wrapped=NumpyInt64Type(), fill_value=0)
            >>> pdcast.resolve_type(pd.CategoricalDtype([1, 2, 3]))
            CategoricalType(wrapped=NumpyInt64Type(), levels=[1, 2, 3])

        If either of these objects are given as classes rather than instances,
        then this method will return a naked
        :class:`DecoratorType <pdcast.DecoratorType>`.  This means that it is
        not backed by an associated :class:`ScalarType <pdcast.ScalarType>`.

        .. doctest::

            >>> pdcast.resolve_type(pd.SparseDtype)
            SparseType(wrapped=None, fill_value=None)
            >>> pdcast.resolve_type(pd.CategoricalDtype)
            CategoricalType(wrapped=None, levels=None)

        These act as wildcards during :func:`cast() <pdcast.cast>` operations,
        automatically acquiring the inferred type of the input data.

        .. doctest::

            >>> pdcast.cast([1, 2, 3], pd.SparseDtype)
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]
            >>> pdcast.cast([1, 2, 3], pd.CategoricalDtype)
            0    1
            1    2
            2    3
            dtype: category
            Categories (3, int64): [1, 2, 3]

        They also match any equivalently-decorated type during
        :meth:`contains() <pdcast.DecoratorType.contains>` checks.

        .. doctest::

            >>> pdcast.resolve_type(pd.SparseDtype).contains(pd.SparseDtype("int64"))
            True
            >>> pdcast.resolve_type(pd.CategoricalDtype).contains(pd.CategoricalDtype([1, 2, 3]))
            True
        """
        # NOTE: any special dtype parsing logic goes here
        return self

    def replace(self, **kwargs) -> DecoratorType:
        """Return an immutable copy of this type with the specified attributes.

        This can be used to modify a type without mutating it.
        """
        # filter kwargs pertaining to this decorator
        extracted = {}
        delegated = {}
        for k, v in kwargs.items():
            if k in self.kwargs:
                extracted[k] = v
            else:
                delegated[k] = v

        # merge with self.kwargs
        extracted = {**self.kwargs, **extracted}

        # pass delegated kwargs down the stack
        wrapped = extracted.pop("wrapped")
        if wrapped is None:
            if delegated:
                raise TypeError(f"unrecognized arguments: {delegated}")
        else:
            wrapped = wrapped.replace(**delegated)

        return type(self)(wrapped=wrapped, **extracted)


    ##################################
    ####    DECORATOR-SPECIFIC    ####
    ##################################

    @property
    def wrapped(self) -> VectorType:
        """Access the type object that this DecoratorType modifies."""
        return self.kwargs["wrapped"]

    def transform(self, series: pd.Series) -> pd.Series:
        """Given an unwrapped conversion result, apply all the necessary logic
        to bring it into alignment with this DecoratorType and all its children.

        This is a recursive method that traverses the `decorators` linked list
        in reverse order (from the inside out).  At the first level, the
        unwrapped series is passed as input to that decorators's
        `transform()` method, which may be overridden as needed.  That
        method must return a properly-wrapped copy of the original, which is
        passed to the next decorator and so on.  Thus, if an DecoratorType
        seeks to change any aspect of the series it adapts (as is the case with
        sparse/categorical types), then it must override this method and invoke
        it *before* applying its own logic, like so:

        ```
        series = super().apply_adapters(series)
        ```

        This pattern maintains the inside-out resolution order of this method.
        """
        return series

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Remove a decorator from an example series."""
        return series.astype(self.wrapped.dtype, copy=False)

    ##########################
    ####    OVERRIDDEN    ####
    ##########################

    @property
    def decorators(self) -> Iterator[DecoratorType]:
        """Iterate through every DecoratorType that is attached to the wrapped
        ScalarType.
        """
        curr = self
        while isinstance(curr, DecoratorType):
            yield curr
            curr = curr.wrapped

    @property
    def backends(self) -> MappingProxyType:
        """Modify the wrapped type's
        :attr:`backends <pdcast.ScalarType.backends>` dictionary, adding the
        wrapper to each of its values.
        """
        if self.wrapped is None:
            return {}

        return {
            k: self.replace(wrapped=v)
            for k, v in self.wrapped.backends.items()
        }

    def contains(self, other: type_specifier) -> bool:
        """Check whether ``other`` is a member of this type's hierarchy.

        Parameters
        ----------
        other : type_specifier
            The type to check for.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``other`` is a member of this type's hierarchy.
            ``False`` otherwise.

        See Also
        --------
        Type.contains :
            For more information on how this method is called.

        Examples
        --------
        This method forces ``other`` to share the same decorators as ``self``.

        .. doctest::

            >>> pdcast.resolve_type("sparse[int]").contains("sparse[int32]")
            True
            >>> pdcast.resolve_type("sparse[int]").contains("categorical[int32]")
            False
            >>> pdcast.resolve_type("sparse[int]").contains("int32")
            False

        The actual check is delegated down the stack to
        :meth:`ScalarType.contains() <pdcast.ScalarType.contains>`.

        .. doctest::

            >>> pdcast.resolve_type("sparse[int]").contains("sparse[int8[numpy]]")
            True
            >>> pdcast.resolve_type("sparse[int]").contains("sparse[float16[numpy]]")
            False

        If the decorator does not have a base
        :class:`ScalarType <pdcast.ScalarType>`, then it acts as a wildcard.

        .. doctest::

            >>> pdcast.resolve_type("sparse").contains("sparse[int8[numpy]]")
            True
            >>> pdcast.resolve_type("sparse").contains("sparse[float16[numpy]]")
            True
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        return (
            isinstance(other, type(self)) and
            self.wrapped.contains(other.wrapped)
        )

    def unwrap(self) -> ScalarType:
        """Strip any DecoratorTypes that have been attached to this ScalarType.
        """
        for curr in self.decorators:
            pass  # exhaust the iterator
        return curr.wrapped

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __dir__(self) -> list:
        """Merge dir() fields with those of the decorated type."""
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.wrapped) if x not in result]
        return result

    def __call__(self, wrapped: VectorType, *args, **kwargs) -> DecoratorType:
        """Create a parametrized decorator for a given type.

        Parameters
        ----------
        wrapped : VectorType
            A type to wrap.  This can be an instance of 
            :class:`ScalarType <pdcast.ScalarType>` or another
            :class:`DecoratorType <pdcast.DecoratorType>`, in which case they
            are nested to form a singly-linked list.
        *args, **kwargs
            Additional parameters to be supplied to this type's
            :meth:`__init__ <python:object.__init__>` method.

        Returns
        -------
        DecoratorType
            A wrapper for the type that modifies its behavior.

        Notes
        -----
        This a factory method that works just like the
        :meth:`__new__() <python:object.__new__>` method of a normal class
        object.  :func:`@register <pdcast.register>` allows us to reduce it to
        the instance level, but it works identically in every other way.

        Examples
        --------
        Decorators can be nested to form a singly-linked list on top of a base
        :class:`ScalarType <pdcast.ScalarType>` object.

        .. doctest::

            >>> pdcast.SparseType(pdcast.CategoricalType(pdcast.BooleanType))
            SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=None)

        The order of these decorators is determined by
        :attr:`TypeRegistry.decorator_priority <pdcast.TypeRegistry.decorator_priority>`,
        which can be customized at run time.

        .. doctest::

            >>> pdcast.registry.decorator_priority
            PriorityList([<class 'pdcast.types.sparse.SparseType'>, <class 'pdcast.types.categorical.CategoricalType'>])
            >>> pdcast.registry.decorator_priority.move(pdcast.CategoricalType, 0)
            >>> pdcast.resolve_type("sparse[categorical[bool]]")
            CategoricalType(wrapped=SparseType(wrapped=BooleanType(), fill_value=None), levels=None)

        .. note::

            Note that the output order does not always match the input order.

        If a decorator of this type already exists in the linked list, then
        this method will modify it rather than creating a duplicate.

        .. doctest::

            >>> pdcast.resolve_type("sparse[categorical[sparse[bool]], True]")
            CategoricalType(wrapped=SparseType(wrapped=BooleanType(), fill_value=True), levels=None)
            >>> str(_)
            categorical[sparse[bool, True], None]
        """
        if isinstance(wrapped, DecoratorType):
            return insort(self, wrapped, args, kwargs)

        return type(self)(wrapped, *args, **kwargs)

    def __getattr__(self, name: str) -> Any:
        """Delegate attribute lookups to the wrapped type.

        This is a sticky wrapper, meaning that if the returned value is a
        compatible type object, it will be automatically re-wrapped with this
        decorator.
        """
        try:
            return self.kwargs[name]
        except KeyError as err:
            val = getattr(self.wrapped, name)

        # wrap data attributes as decorators
        if isinstance(val, Type):
            return wrap_result(self, val)

        # wrap callables to return decorators
        if callable(val):
            def sticky_wrapper(*args, **kwargs):
                result = val(*args, **kwargs)
                if isinstance(result, Type):
                    return wrap_result(self, result)
                return result

            return sticky_wrapper

        return val


#######################
####    PRIVATE    ####
#######################


cdef Type wrap_result(DecoratorType decorator, Type result):
    """Replace a decorator on the result of a wrapped computation."""
    if isinstance(result, CompositeType):
        return CompositeType(decorator.replace(wrapped=typ) for typ in result)
    return decorator.replace(wrapped=result)


cdef DecoratorType insort(
    DecoratorType self,
    DecoratorType wrapped,
    tuple args,
    dict kwargs
):
    """Insert a decorator into a singly-linked list in sorted order."""
    priority = self.registry.decorator_priority
    threshold = priority.index(self)

    # iterate through list
    encountered = []
    for curr in wrapped.decorators:
        # curr is higher priority than self
        if priority.index(curr) < threshold:
            encountered.append(curr)
            continue

        # curr is a duplicate of self
        if isinstance(curr, type(self)):
            if self == self.base_instance:
                return wrapped
            curr = curr.wrapped

        # curr is lower priority than self
        result = type(self)(curr, *args, **kwargs)
        break
    else:
        # all decorators are higher priority - insert at base
        result = type(self)(curr.wrapped, *args, **kwargs)

    for prev in reversed(encountered):
        result = prev.replace(wrapped=result)
    return result
