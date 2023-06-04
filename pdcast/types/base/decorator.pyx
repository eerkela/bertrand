"""This module describes an ``DecoratorType`` object, which can be subclassed
to create a dynamic wrapper around an ``ScalarType``.
"""
from types import MappingProxyType
from typing import Any, Iterator

cimport numpy as np
import numpy as np
import pandas as pd

from pdcast import resolve  # importing directly causes an ImportError
from pdcast.util.type_hints import dtype_like, type_specifier

from .registry cimport AliasManager
from .scalar cimport ScalarType
from .vector cimport VectorType, ArgumentEncoder, InstanceFactory
from .composite cimport CompositeType


cdef class DecoratorType(VectorType):
    """Abstract base class for all `Decorator pattern
    <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_ type
    objects.

    These are used to dynamically modify the behavior of other types, for
    instance by marking them as sparse or categorical.  They can be nested to
    form a singly-linked list that can be iteratively unwrapped using a type's
    :attr:`.decorators <DecoratorType.decorators>` attribute.  Conversions and other
    type-related functionality will automatically take these adapters into
    account, unwrapping them to the appropriate level before re-packaging the
    result.

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
        return self  # NOTE: By default, types ignore extension metadata

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

        This is a recursive method that traverses the `adapters` linked list
        in reverse order (from the inside out).  At the first level, the
        unwrapped series is passed as input to that adapter's
        `transform()` method, which may be overridden as needed.  That
        method must return a properly-wrapped copy of the original, which is
        passed to the next adapter and so on.  Thus, if an DecoratorType seeks to
        change any aspect of the series it adapts (as is the case with
        sparse/categorical types), then it must override this method and invoke
        it *before* applying its own logic, like so:

        ```
        series = super().apply_adapters(series)
        ```

        This pattern maintains the inside-out resolution order of this method.
        """
        return series

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Remove an adapter from an example series."""
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
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
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
        """Create a parametrized decorator for the given type."""
        if isinstance(wrapped, DecoratorType):
            # trivial case: wrapping an instance of self
            if isinstance(wrapped, type(self)):
                return type(self)(wrapped.wrapped, *args, **kwargs)

            # insert into nested stack (sorted)
            priority = self.registry.decorators
            threshold = priority.index(type(self))
            encountered = []
            for curr in wrapped.decorators:
                encountered.append(curr)
                duplicate = isinstance(curr.wrapped, type(self))
                insort = priority.index(type(curr)) > threshold
                if duplicate or insort:
                    if duplicate:
                        curr = curr.wrapped.wrapped
                    result = type(self)(curr, *args, **kwargs)
                    for prev in reversed(encountered):
                        result = prev.replace(wrapped=result)
                    return result

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

        # TODO: this fails because types are callable

        # decorate callables to return DecoratorTypes
        if callable(val):
            def sticky_wrapper(*args, **kwargs):
                result = val(*args, **kwargs)
                if isinstance(result, VectorType):
                    result = self.replace(wrapped=result)
                elif isinstance(result, CompositeType):
                    result = CompositeType(
                        {self.replace(wrapped=t) for t in result}
                    )
                return result

            return sticky_wrapper

        # wrap properties as DecoratorTypes
        if isinstance(val, VectorType):
            val = self.replace(wrapped=val)
        elif isinstance(val, CompositeType):
            val = CompositeType({self.replace(wrapped=t) for t in val})

        return val
