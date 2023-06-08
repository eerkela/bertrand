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


cdef class DecoratorType(VectorType):
    """Abstract base class for all `Decorator pattern
    <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_ type
    objects.

    These are used to dynamically modify the behavior of other types, for
    instance by marking them as sparse or categorical.  They can be nested to
    form a singly-linked list that can be iteratively unwrapped using a type's
    :attr:`.decorators <DecoratorType.decorators>` attribute.  Conversions and
    other type-related functionality will automatically take these decorators
    into account, unwrapping them to the appropriate level before re-packaging
    the result.

    .. note::

        These are examples of the `Gang of Four
        <https://en.wikipedia.org/wiki/Design_Patterns>`_\'s `Decorator Pattern
        <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
        which is not to be confused with the built-in python ``@decorator``
        syntax.  This pattern leverages `composition over inheritance
        <https://en.wikipedia.org/wiki/Composition_over_inheritance>`_ to
        prevent subclass explosions in the ``pdcast`` type system.

    Parameters
    ----------
    wrapped : ScalarType | DecoratorType
        The type object to wrap.  Any attributes that are not caught by the
        :class:`DecoratorType` itself will be automatically delegated to this
        object, in accordance with the `Decorator pattern
        <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_.
    **kwargs : dict
        Arbitrary keyword arguments describing metadata for this type.  If a
        subclass accepts arguments in its ``__init__`` method, they should
        always be passed here via ``super().__init__(**kwargs)``.  This is
        conceptually equivalent to the ``_metadata`` field of pandas
        :class:`ExtensionDtype <pandas.api.extension.ExtensionDtype>` objects.

    Attributes
    ----------
    name : str
        A unique name for each type, which must be defined at the class level.
        This is used in conjunction with :meth:`encode()
        <DecoratorType.encode>` to generate string representations of the
        associated type.
    aliases : set[str | ExtensionDtype]
        A set of unique aliases for this type, which must be defined at the
        class level.  These are used by :func:`detect_type` and
        :func:`resolve_type` to map aliases onto their corresponding types.

        .. note::

            These work slightly differently from :attr:`ScalarType.aliases` in
            that they do not support ``type`` or ``np.dtype`` objects.  This is
            because :class:`DecoratorTypes <DecoratorType>` cannot describe scalar
            (non-vectorized) data, and there is no direct equivalent within
            the numpy typing system.  Strings and ``ExtensionDtypes`` work
            identically to their :class:`ScalarType` equivalents.

    Notes
    -----
    .. _adapter_type.inheritance:

    :class:`DecoratorTypes <DecoratorType>` are `metaclasses <https://peps.python.org/pep-0487/>`_
    that are limited to **first-order inheritance**.  This means that they must
    inherit from :class:`DecoratorType` *directly*, and cannot have any children
    of their own.  For example:

    .. code:: python

        class Type1(pdcast.DecoratorType):   # valid
            ...

        class Type2(Type1):   # invalid
            ...

    If you'd like to share functionality between types, this can be done using
    `Mixin classes <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_,
    like so:

    .. code:: python

        class Mixin:
            # shared attributes/methods go here
            ...

        class Type1(Mixin, pdcast.DecoratorType):
            ...

        class Type2(Mixin, pdcast.DecoratorType):
            ...

    .. note::

        Note that ``Mixin`` comes **before** :class:`DecoratorType` in each
        inheritance signature.  This ensures correct `Method Resolution Order
        (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.

    .. _adapter_type.allocation:

    :class:`DecoratorTypes <DecoratorType>` are **not** cached as `flyweights
    <https://python-patterns.guide/gang-of-four/flyweight/>`_, unlike
    :class:`ScalarType`.
    """

    def __init__(self, wrapped: VectorType = None, **kwargs):
        super().__init__(wrapped=wrapped, **kwargs)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, wrapped: str = None, *args: str) -> DecoratorType:
        """Construct a new :class:`DecoratorType` in the :ref:`type specification
        mini-language <resolve_type.mini_language>`.

        Override this if a type implements custom parsing rules for any
        arguments that are supplied to it.

        Parameters
        ----------
        wrapped : str, optional
            The type to be wrapped.  If given, this must be an
            independently-resolvable type specifier in the :ref:`type
            specification mini-language <resolve_type.mini_language>`.
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the :ref:`type
            specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        DecoratorType
            An :class:`DecoratorType` with the appropriate configuration based
            on the arguments that were passed to this constructor.

        See Also
        --------
        ScalarType.resolve : The scalar equivalent of this method.

        Notes
        -----
        If ``wrapped`` is not specified, then this method will return a *naked*
        :class:`DecoratorType`, meaning that it is not backed by an associated
        :class:`ScalarType` instance.  This technically breaks the `Decorator
        Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
        limiting the usefulness of these types.

        Naked :class:`DecoratorTypes <DecoratorType>` act as wildcards when
        provided to :doc:`conversion functions </content/api/cast>` or
        :func:`typechecks <typecheck>`.  Additionally, new :func:`@dispatch
        <dispatch>` methods can be attached to :class:`DecoratorTypes
        <DecoratorType>` by providing a naked example of that type.
        """
        if wrapped is None:
            return self

        cdef VectorType instance = resolve.resolve_type(wrapped)

        return self(instance, *args)

    def from_dtype(self, dtype: dtype_like) -> DecoratorType:
        """Construct an :class:`DecoratorType` from a corresponding pandas
        ``ExtensionDtype``.

        Override this if a type must parse the attributes of an associated
        ``ExtensionDtype``.

        Parameters
        ----------
        dtype : ExtensionDtype
            The pandas ``ExtensionDtype`` to parse.

        Returns
        -------
        DecoratorType
            An :class:`DecoratorType` with the appropriate configuration based
            on the ``ExtensionDtype`` that was passed to this constructor.

        See Also
        --------
        ScalarType.from_dtype : The scalar equivalent of this method.

        Notes
        -----
        If a raw ``ExtensionDtype`` class is provided to this function, then it
        will return a *naked* :class:`DecoratorType`, meaning that it is not
        backed by an associated :class:`ScalarType` instance.  This technically
        breaks the `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
        limiting the usefulness of these types.

        Naked :class:`DecoratorTypes <DecoratorType>` act as wildcards when
        provided to :doc:`conversion functions </content/api/cast>` or
        :func:`typechecks <typecheck>`.  Additionally, new :func:`@dispatch
        <dispatch>` methods can be attached to :class:`DecoratorTypes
        <DecoratorType>` by providing a naked example of that type.
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
        return {
            k: self.replace(wrapped=v)
            for k, v in self.wrapped.backends.items()
        }

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Test whether `other` is a subtype of the given ScalarType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        For DecoratorTypes, this merely delegates to ScalarType.contains().
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(typ, include_subtypes=include_subtypes)
                for typ in other
            )

        return (
            isinstance(other, type(self)) and
            self.wrapped.contains(
                other.wrapped,
                include_subtypes=include_subtypes
            )
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
