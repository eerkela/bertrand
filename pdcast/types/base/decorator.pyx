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

    See Also
    --------
    ScalarType : Base class for all decoratable scalar types.
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
        Type.from_string : For more information on how this method is called.

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
        Type.from_dtype : For more information on how this is called.

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
        """Return a modified copy of a type with the values specified in
        ``**kwargs``.

        Parameters
        ----------
        **kwargs : dict
            Keyword arguments corresponding to parametrized attributes of this
            type or any of its wrapped children.  Any parameters that are not
            listed explicitly will use the current values for this instance.

        Returns
        -------
        DecoratorType
            A new instance of this type with the specified values.

        Examples
        --------
        This method is used to modify a type without mutating it.

        .. doctest::

            >>> pdcast.resolve_type("sparse[int64]").replace(wrapped="float64")
            SparseType(wrapped=Float64Type(), fill_value=None)
            >>> pdcast.resolve_type("sparse[int64]").replace(fill_value=1)
            SparseType(wrapped=Int64Type(), fill_value=1)

        Unlike :meth:`ScalarType.replace() <pdcast.ScalarType.replace>`, this
        method can also delegate parameters to the wrapped type.

        .. doctest::

            >>> pdcast.resolve_type("sparse[M8[5ns]]").replace(unit="s")
            SparseType(wrapped=NumpyDatetime64Type(unit='s', step_size=5), fill_value=None)
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

    ##########################
    ####    MEMBERSHIP    ####
    ##########################

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
        Type.contains : For more information on how this method is called.

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

    #################################
    ####    DECORATOR PATTERN    ####
    #################################

    @property
    def wrapped(self):
        """Access the type object that this decorator modifies.

        Returns
        -------
        ScalarType | DecoratorType | None
            The :class:`ScalarType <pdcast.ScalarType>` or
            :class:`DecoratorType <pdcast.DecoratorType>` being wrapped by this
            type.  If the decorator is naked, then this will be ``None``.

        See Also
        --------
        DecoratorType.decorators : Iterate through the decorators that are
            attached to this type.
        DecoratorType.unwrap : Retrieve the base type object.

        Examples
        --------
        .. doctest::

            >>> pdcast.resolve_type("sparse").wrapped
            None
            >>> pdcast.resolve_type("sparse[int]").wrapped
            IntegerType()
            >>> pdcast.resolve_type("sparse[categorical[int]]").wrapped
            CategoricalType(wrapped=IntegerType(), levels=None)
        """
        return self.kwargs["wrapped"]

    @property
    def decorators(self):
        """A generator that iterates over every
        :class:`DecoratorType <pdcast.DecoratorType>` that is attached to this
        type.

        Returns
        -------
        Iterator
            A generator expression that yields each decorator in order.

        See Also
        --------
        DecoratorType.wrapped : Access the type object that this decorator
            modifies.
        DecoratorType.unwrap() :
            Get the base :class:`ScalarType <pdcast.ScalarType>` associated
            with this type.

        Examples
        --------
        Decorators can be nested to form a singly-linked list on top of a base
        :class:`ScalarType <pdcast.ScalarType>` object.  This attribute allows
        users to iterate through the decorators in order, progressively
        unwrapping them.

        .. doctest::

            >>> pdcast.resolve_type("sparse[categorical[str]]")
            SparseType(wrapped=CategoricalType(wrapped=StringType(), levels=None), fill_value=None)
            >>> [str(x) for x in _.decorators]
            ['sparse[categorical[string, None], None]', 'categorical[string, None]']
        """
        curr = self
        while isinstance(curr, DecoratorType):
            yield curr
            curr = curr.wrapped

    def unwrap(self) -> ScalarType | None:
        """Strip all the :class:`DecoratorTypes <pdcast.DecoratorType>` that
        are attached to this type.

        Returns
        -------
        ScalarType
            The base :class:`ScalarType <pdcast.ScalarType>` associated with
            this type.

        See Also
        --------
        DecoratorType.wrapped : Access the type object that this decorator
            modifies.
        DecoratorType.decorators :
            A generator that iterates through decorators layer by layer.

        Examples
        --------
        .. doctest::

            >>> pdcast.resolve_type("categorical[str]")
            CategoricalType(wrapped=StringType(), levels=None)
            >>> _.unwrap()
            StringType()
        """
        for curr in self.decorators:
            pass  # exhaust the iterator
        return curr.wrapped

    def __getattr__(self, name: str) -> Any:
        """Forward attribute lookups to the wrapped type.

        Parameters
        ----------
        name : str
            The name of the attribute to look up.

        Returns
        -------
        Any
            The value of the attribute on the wrapped type.

        See Also
        --------
        DecoratorType.wrapped : Access the type object that this decorator
            modifies.

        Notes
        -----
        This is a sticky wrapper, meaning that if the returned value is a
        compatible type object, it will be automatically re-wrapped with this
        :class:`DecoratorType <pdcast.DecoratorType>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.resolve_type("sparse[int8]").max
            127
            >>> pdcast.resolve_type("sparse[int8]").supertype
            SparseType(wrapped=SignedIntegerType(), fill_value=None)
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

    ####################################
    ####    DATA TRANSFORMATIONS    ####
    ####################################

    def transform(self, series: pd.Series) -> pd.Series:
        """Remove the decorator from an equivalently-typed series.

        Parameters
        ----------
        series : pandas.Series
            The series to transform.  This will always have this decorator's
            :attr:`dtype <pdcast.ScalarType.dtype>`.

        Returns
        -------
        pandas.Series
            A copy of the input series with the decorator removed.

        See Also
        --------
        DecoratorType.inverse_transform : Apply the decorator to a series.

        Notes
        -----
        This method is used to automatically retry :func:`cast() <pdcast.cast>`
        operations when they fail due to a type mismatch.

        If the mismatch occurs due to the presence of a decorator in the target
        type, then the decorator will be removed and the conversion will be
        retried with the wrapped type as the target.  This is done recursively
        until a match is found somewhere in its decorator stack.  Once found,
        the conversion is performed, and the result is re-wrapped to match the
        original target using this method at every level.

        This approach helps reduce the combinatorial explosion of conversions
        that would otherwise be required to handle all the possible
        permutations of decorators and types.  As a result, each conversion can
        be defined exclusively at the scalar level, with decorators being
        handled automatically in the background.  As long as this method
        produces the same output as a direct conversion to the original target,
        it will give identical results.

        Examples
        --------
        .. doctest::

            >>> import pandas as pd

            >>> target = pdcast.resolve_type("sparse[int64[numpy]]")
            >>> target.transform(pd.Series([1, 2, 3]))
            0    1
            1    2
            2    3
            dtype: Sparse[int64, <NA>]
        """
        # NOTE: by default, this method does nothing.  Users should override
        # it to implement the appropriate transformations for their type.
        return series

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Remove a decorator from an example series.

        Parameters
        ----------
        series : pandas.Series
            The series to transform.  This will always have the same
            :attr:`dtype <pdcast.ScalarType.dtype>` as the wrapped type.

        Returns
        -------
        pandas.Series
            A copy of the input series with the decorator applied.

        See Also
        --------
        DecoratorType.transform : Remove the decorator from a series.

        Notes
        -----
        This method is used to automatically retry :func:`cast() <pdcast.cast>`
        operations when they fail due to a type mismatch.

        If the mismatch occurs due to the presence of a decorator in the source
        data, then this method will be used to remove it from the data and
        retry the conversion with the wrapped type as the source.  This is done
        recursively until a match is found, at which point the conversion is
        performed and the result returned as normal.

        This approach helps reduce the combinatorial explosion of conversions
        that would otherwise be required to handle all the possible
        permutations of decorators and types.  As a result, each conversion can
        be defined exclusively at the scalar level, with decorators being
        handled automatically in the background.  As long as this method
        produces like-valued output data, it will give identical results.

        Examples
        --------
        .. doctest::

            >>> import pandas as pd

            >>> target = pdcast.resolve_type("sparse[int64[numpy]]")
            >>> target.inverse_transform(pd.Series([1, 2, 3], dtype="Sparse[int]"))
            0    1
            1    2
            2    3
            dtype: int64
        """
        # NOTE: by default, this method just astypes() to the wrapped dtype.
        # This may not be appropriate for all decorators, so users should
        # override it if necessary.
        return series.astype(self.wrapped.dtype, copy=False)

    ##########################
    ####    OVERRIDDEN    ####
    ##########################

    @property
    def implementations(self):
        """A mapping of all the
        :meth:`implementations <pdcast.AbstractType.implementation>` that are
        registered to the wrapped type.

        Returns
        -------
        MappingProxyType
            A read-only dictionary listing the concrete implementations that
            have been registered to the base
            :class:`ScalarType <pdcast.ScalarType>`, with their backend
            specifiers as keys.  This always includes the type's default
            implementation under the ``None`` key.

        See Also
        --------
        ScalarType.implementations : The scalar equivalent of this attribute.
        AbstractType.implementation : A class decorator used to mark types as
            concrete implementations of an abstract type.

        Notes
        -----
        This is equivalent to the
        :meth:`implementations <pdcast.ScalarType.implementations>` property of
        the wrapped type, except that the results are automatically wrapped
        using this :class:`DecoratorType <pdcast.DecoratorType>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.resolve_type("sparse[int64[numpy]]").implementations
            mappingproxy({None: SparseType(wrapped=NumpyInt64Type(), fill_value=None)})
            >>> pdcast.resolve_type("sparse[float16]").implementations
            mappingproxy({None: SparseType(wrapped=NumpyFloat16Type(), fill_value=None), 'numpy': SparseType(wrapped=NumpyFloat16Type(), fill_value=None)})
            >>> pdcast.resolve_type("sparse[bool]").implementations
            mappingproxy({None: SparseType(wrapped=NumpyBooleanType(), fill_value=None), 'numpy': SparseType(wrapped=NumpyBooleanType(), fill_value=None), 'pandas': SparseType(wrapped=PandasBooleanType(), fill_value=None), 'python': SparseType(wrapped=PythonBooleanType(), fill_value=None)})
        """
        if self.wrapped is None:
            return MappingProxyType({})

        return MappingProxyType({
            k: self.replace(wrapped=v)
            for k, v in self.wrapped.implementations.items()
        })

    def __dir__(self) -> list:
        """Merge :func:`dir() <python:dir>` fields with those of the decorated
        type.

        Returns
        -------
        list
            A list of all the attributes on this type and its wrapped type.

        See Also
        --------
        DecoratorType.wrapped : Access the type object that this decorator
            modifies.
        """
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

            >>> SparseType(CategoricalType(BooleanType))
            SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=None)

        The order of these decorators is determined by the
        :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table,
        which can be customized at runtime.

        .. doctest::

            >>> registry.priority
            PrioritySet({...})
            >>> (SparseType, CategoricalType) in registry.priority
            True
            >>> registry.priority.remove((SparseType, CategoricalType))
            >>> registry.priority.add((CategoricalType, SparseType))
            >>> resolve_type("sparse[categorical[bool]]")
            CategoricalType(wrapped=SparseType(wrapped=BooleanType(), fill_value=None), levels=None)

        .. note::

            Note that the output order does not always match the input order.

        If a decorator of this type already exists in the linked list, then
        this method will modify it rather than creating a duplicate.

        .. doctest::

            >>> resolve_type("sparse[categorical[sparse[bool]], True]")
            CategoricalType(wrapped=SparseType(wrapped=BooleanType(), fill_value=True), levels=None)
            >>> str(_)
            categorical[sparse[bool, True], None]
        """
        # insert into linked list according to priority
        if isinstance(wrapped, DecoratorType):
            encountered = []
            for curr in wrapped.decorators:

                # curr is higher priority than self
                if curr.base_instance < self.base_instance:
                    encountered.append(curr)  # remember for later
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

            # replace all encountered decorators in original order
            for prev in reversed(encountered):
                result = prev.replace(wrapped=result)

            return result

            # return insort(self, wrapped, args, kwargs)

        # wrap directly
        return type(self)(wrapped, *args, **kwargs)


    def __lt__(self, other: VectorType) -> bool:
        """Sort types according to their priority and representable range.

        Parameters
        ----------
        other : VectorType
            Another type to compare against.

        Returns
        -------
        bool
            ``True`` if this type is considered to be smaller than ``other``.
            See the notes below for details.

        Raises
        ------
        ValueError
            If this decorator has no
            :attr:`wrapped <pdcast.DecoratorType.wrapped>` type, ``other`` is
            another decorator, and no edge exists between them in the
            :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table.

        See Also
        --------
        DecoratorType.__gt__ : The inverse of this operator.
        TypeRegistry.priority : Override the default sorting behavior.

        Notes
        -----
        This operator is the decorated equivalent of
        :meth:ScalarType.__lt__ <pdcast.ScalarType.__lt__>`.  Most of the time,
        it simply delegates to that operator, but if a decorator is naked,
        (i.e. has no :attr:`wrapped <pdcast.DecoratorType.wrapped>` type), then
        its behavior can become undefined.  In these cases, special logic is
        applied to determine its priority.

        The priority of a naked decorator is determined by the following rules:

            *   If the comparison type is a decorator of the same species, then
                the naked decorator is considered to be smaller.  If the other
                decorator is also naked, then this operator returns ``False``.
            *   If the comparison type is a decorator of a different species,
                then the
                :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>`
                table is searched for an edge containing the two types.  If
                none is found, then a :class:`ValueError <python:ValueError>`
                is raised stating the ambiguity.
            *   If the comparison type is scalar, then the naked
                decorator is always considered to be smaller.

        Examples
        --------
        In most cases, this operator simply delegates to the wrapped type.

        .. doctest::

            >>> SparseType(BooleanType) < SparseType(IntegerType)
            True
            >>> SparseType(IntegerType) < SparseType(BooleanType)
            False
            >>> SparseType(BooleanType) < CategoricalType(IntegerType)
            True
            >>> SparseType(IntegerType) < SparseType(CategoricalType(BooleanType))
            False

        If the decorator is naked, then its priority is determined by the
        :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table.

        .. doctest::

            >>> SparseType < CategoricalType
            True
            >>> CategoricalType < SparseType
            False
            >>> (SparseType, CategoricalType) in registry.priority
            True

        Naked decorators are always considered smaller than scalars and their
        non-naked counterparts.

        .. doctest::

            >>> SparseType < BooleanType
            True
            >>> SparseType < SparseType(BooleanType)
            True
            >>> SparseType < CategoricalType(BooleanType)
            True
            >>> SparseType(BooleanType) < SparseType
            False
            >>> SparseType(BooleanType) < CategoricalType
            False
        """
        # special case for decorators with no wrapped type
        if self.wrapped is None:
            # comparison type is another decorator
            if isinstance(other, DecoratorType):
                # decorator is of the same species
                if isinstance(other, type(self)):
                    return other.wrapped is not None

                # search for edge
                if (type(self), type(other)) in self.registry.priority:
                    return True
                if (type(other), type(self)) in self.registry.priority:
                    return False

                # no edge found
                raise ValueError(
                    f"no edge between {self} and {other} in registry.priority"
                )

            # comparison type is scalar
            return True

        # delegate to wrapped type
        return self.wrapped < other

    def __gt__(self, other: VectorType) -> bool:
        """Sort types according to their priority and representable range.

        Parameters
        ----------
        other : VectorType
            Another type to compare against.

        Returns
        -------
        bool
            ``True`` if this type is considered to be larger than ``other``.
            See the notes below for details.

        Raises
        ------
        ValueError
            If this decorator has no
            :attr:`wrapped <pdcast.DecoratorType.wrapped>` type, ``other`` is
            another decorator, and no edge exists between them in the
            :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table.

        See Also
        --------
        DecoratorType.__lt__ : The inverse of this operator.
        TypeRegistry.priority : Override the default sorting behavior.

        Notes
        -----
        This operator is provided for completeness with respect to
        :meth:`DecoratorType.__lt__ <pdcast.DecoratorType.__lt__>`.  It
        represents the inverse of that operation.

        If a manual override is registered under the
        :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table,
        then it will be used for both operators.
        """
        # special case for decorators with no wrapped type
        if self.wrapped is None:
            # comparison type is another decorator
            if isinstance(other, DecoratorType):
                # decorator is of the same species
                if isinstance(other, type(self)):
                    return self.wrapped is not None

                # search for edge
                if (type(self), type(other)) in self.registry.priority:
                    return False
                if (type(other), type(self)) in self.registry.priority:
                    return True

                # no edge found
                raise ValueError(
                    f"no edge between {self} and {other} in registry.priority"
                )

            # comparison type is scalar
            return False

        # delegate to wrapped type
        return self.wrapped > other


#######################
####    PRIVATE    ####
#######################


cdef Type wrap_result(DecoratorType decorator, Type result):
    """Replace a decorator on the result of a wrapped computation."""
    # broadcast the decorator to all types
    if isinstance(result, CompositeType):
        return CompositeType(decorator.replace(wrapped=typ) for typ in result)

    # wrap a single result
    return decorator.replace(wrapped=result)
