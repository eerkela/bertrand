"""This module describes an ``AdapterType`` object, which can be subclassed
to create a dynamic wrapper around an ``AtomicType``.
"""
from types import MappingProxyType
from typing import Any, Iterator

cimport numpy as np
import numpy as np
import pandas as pd

from pdcast import resolve
from pdcast.util.type_hints import type_specifier

from .registry cimport AliasManager
from .atomic cimport AtomicType
from .scalar cimport ScalarType, SlugFactory, InstanceFactory
from .composite cimport CompositeType


cdef class AdapterType(ScalarType):
    """Abstract base class for all `Decorator pattern
    <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_ type
    objects.

    These are used to dynamically modify the behavior of other types, for
    instance by marking them as sparse or categorical.  They can be nested to
    form a singly-linked list that can be iteratively unwrapped using a type's
    :attr:`.adapters <AdapterType.adapters>` attribute.  Conversions and other
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
    wrapped : AtomicType | AdapterType
        The type object to wrap.  Any attributes that are not caught by the
        :class:`AdapterType` itself will be automatically delegated to this
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
        This is used in conjunction with :meth:`slugify()
        <AdapterType.slugify>` to generate string representations of the
        associated type.
    aliases : set[str | ExtensionDtype]
        A set of unique aliases for this type, which must be defined at the
        class level.  These are used by :func:`detect_type` and
        :func:`resolve_type` to map aliases onto their corresponding types.

        .. note::

            These work slightly differently from :attr:`AtomicType.aliases` in
            that they do not support ``type`` or ``np.dtype`` objects.  This is
            because :class:`AdapterTypes <AdapterType>` cannot describe scalar
            (non-vectorized) data, and there is no direct equivalent within
            the numpy typing system.  Strings and ``ExtensionDtypes`` work
            identically to their :class:`AtomicType` equivalents.

    Notes
    -----
    .. _adapter_type.inheritance:

    :class:`AdapterTypes <AdapterType>` are `metaclasses <https://peps.python.org/pep-0487/>`_
    that are limited to **first-order inheritance**.  This means that they must
    inherit from :class:`AdapterType` *directly*, and cannot have any children
    of their own.  For example:

    .. code:: python

        class Type1(pdcast.AdapterType):   # valid
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

        class Type1(Mixin, pdcast.AdapterType):
            ...

        class Type2(Mixin, pdcast.AdapterType):
            ...

    .. note::

        Note that ``Mixin`` comes **before** :class:`AdapterType` in each
        inheritance signature.  This ensures correct `Method Resolution Order
        (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.

    .. _adapter_type.allocation:

    :class:`AdapterTypes <AdapterType>` are **not** cached as `flyweights
    <https://python-patterns.guide/gang-of-four/flyweight/>`_, unlike
    :class:`AtomicType`.
    """

    init_subclass = True
    priority = 0

    def __init__(self, wrapped: ScalarType, **kwargs):
        if isinstance(wrapped, AdapterType):
            wrapped, kwargs = self._insort(self, wrapped, kwargs)

        self._wrapped = wrapped
        kwargs = {"wrapped": wrapped} | kwargs
        super().__init__(**kwargs)

    cdef void init_base(self):
        subclass = type(self)

        # TODO: might do validation at this level rather than in registry.add

        # parse subclass fields
        self._aliases = AliasManager(subclass.aliases | {subclass})
        self._slugify = SlugFactory(subclass.name, tuple(self._kwargs))
        self._slug = self._slugify((), self._kwargs)
        self._instances = InstanceFactory(subclass)
        self._insort = PrioritySorter(subclass, priority=subclass.priority)

        # clean up subclass fields
        del subclass.aliases  # pass to AtomicType.aliases

        subclass._base_instance = self

    cdef void init_parametrized(self):
        base = type(self)._base_instance

        self._aliases = base._aliases
        self._slugify = base._slugify
        self._slug = self._slugify((), self._kwargs)
        self._instances = base._instances
        self._insort = base._insort

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def resolve(cls, wrapped: str = None, *args: str) -> AdapterType:
        """Construct a new :class:`AdapterType` in the :ref:`type specification
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
        AdapterType
            An :class:`AdapterType` with the appropriate configuration based
            on the arguments that were passed to this constructor.

        See Also
        --------
        AtomicType.resolve : The scalar equivalent of this method.

        Notes
        -----
        If ``wrapped`` is not specified, then this method will return a *naked*
        :class:`AdapterType`, meaning that it is not backed by an associated
        :class:`AtomicType` instance.  This technically breaks the `Decorator
        Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
        limiting the usefulness of these types.

        Naked :class:`AdapterTypes <AdapterType>` act as wildcards when
        provided to :doc:`conversion functions </content/api/cast>` or
        :func:`typechecks <typecheck>`.  Additionally, new :func:`@dispatch
        <dispatch>` methods can be attached to :class:`AdapterTypes
        <AdapterType>` by providing a naked example of that type.
        """
        if wrapped is None:
            return cls()

        cdef ScalarType instance = resolve.resolve_type(wrapped)

        # insert into sorted adapter stack according to priority
        for x in instance.adapters:
            if x._priority <= cls._priority:  # initial
                break
            if getattr(x.wrapped, "_priority", -np.inf) <= cls._priority:
                x.wrapped = cls(x.wrapped, *args)
                return instance

        # add to front of stack
        return cls(instance, *args)

    @classmethod
    def from_dtype(cls, dtype: pd.api.extension.ExtensionDtype) -> AdapterType:
        """Construct an :class:`AdapterType` from a corresponding pandas
        ``ExtensionDtype``.

        Override this if a type must parse the attributes of an associated
        ``ExtensionDtype``.

        Parameters
        ----------
        dtype : ExtensionDtype
            The pandas ``ExtensionDtype`` to parse.

        Returns
        -------
        AdapterType
            An :class:`AdapterType` with the appropriate configuration based
            on the ``ExtensionDtype`` that was passed to this constructor.

        See Also
        --------
        AtomicType.from_dtype : The scalar equivalent of this method.

        Notes
        -----
        If a raw ``ExtensionDtype`` class is provided to this function, then it
        will return a *naked* :class:`AdapterType`, meaning that it is not
        backed by an associated :class:`AtomicType` instance.  This technically
        breaks the `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_,
        limiting the usefulness of these types.

        Naked :class:`AdapterTypes <AdapterType>` act as wildcards when
        provided to :doc:`conversion functions </content/api/cast>` or
        :func:`typechecks <typecheck>`.  Additionally, new :func:`@dispatch
        <dispatch>` methods can be attached to :class:`AdapterTypes
        <AdapterType>` by providing a naked example of that type.
        """
        return cls()  # NOTE: By default, types ignore extension metadata

    def replace(self, **kwargs) -> AdapterType:
        # extract kwargs pertaining to AdapterType
        adapter_kwargs = {}
        atomic_kwargs = {}
        for k, v in kwargs.items():
            if k in self.kwargs:
                adapter_kwargs[k] = v
            else:
                atomic_kwargs[k] = v

        # merge adapter_kwargs with self.kwargs and get wrapped type
        adapter_kwargs = {**self.kwargs, **adapter_kwargs}
        wrapped = adapter_kwargs.pop("wrapped")

        # pass non-adapter kwargs down to wrapped.replace()
        wrapped = wrapped.replace(**atomic_kwargs)

        # construct new AdapterType
        return type(self)(wrapped=wrapped, **adapter_kwargs)

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def adapters(self) -> Iterator[AdapterType]:
        """Iterate through every AdapterType that is attached to the wrapped
        AtomicType.
        """
        frame = self
        while isinstance(frame, AdapterType):
            yield frame
            frame = frame.wrapped

    @property
    def atomic_type(self) -> AtomicType:
        """Access the underlying AtomicType instance with every adapter removed
        from it.
        """
        result = self.wrapped
        while isinstance(result, AdapterType):
            result = result.wrapped
        return result

    @atomic_type.setter
    def atomic_type(self, val: ScalarType) -> None:
        lowest = self
        while isinstance(lowest.wrapped, AdapterType):
            lowest = lowest.wrapped
        lowest.wrapped = val

    @property
    def backends(self) -> MappingProxyType:
        return {
            k: self.replace(wrapped=v)
            for k, v in self.wrapped.backends.items()
        }

    @property
    def wrapped(self) -> ScalarType:
        """Access the type object that this AdapterType modifies."""
        return self._wrapped

    @wrapped.setter
    def wrapped(self, val: ScalarType) -> None:
        """Change the type object that this AdapterType modifies."""
        self._wrapped = val
        self.kwargs = self.kwargs | {"wrapped": val}
        self._slug = self.slugify((), self.kwargs)
        self._hash = hash(self._slug)

    #######################
    ####    METHODS    ####
    #######################

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Test whether `other` is a subtype of the given AtomicType.
        This is functionally equivalent to `other in self`, except that it
        applies automatic type resolution to `other`.

        For AdapterTypes, this merely delegates to AtomicType.contains().
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

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Remove an adapter from an example series."""
        return series.astype(self.wrapped.dtype, copy=False)

    def transform(self, series: pd.Series) -> pd.Series:
        """Given an unwrapped conversion result, apply all the necessary logic
        to bring it into alignment with this AdapterType and all its children.

        This is a recursive method that traverses the `adapters` linked list
        in reverse order (from the inside out).  At the first level, the
        unwrapped series is passed as input to that adapter's
        `transform()` method, which may be overridden as needed.  That
        method must return a properly-wrapped copy of the original, which is
        passed to the next adapter and so on.  Thus, if an AdapterType seeks to
        change any aspect of the series it adapts (as is the case with
        sparse/categorical types), then it must override this method and invoke
        it *before* applying its own logic, like so:

        ```
        series = super().apply_adapters(series)
        ```

        This pattern maintains the inside-out resolution order of this method.
        """
        return series

    def unwrap(self) -> AtomicType:
        """Strip any AdapterTypes that have been attached to this AtomicType.
        """
        return self.atomic_type

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __dir__(self) -> list:
        result = dir(type(self))
        result += list(self.__dict__.keys())
        result += [x for x in dir(self.wrapped) if x not in result]
        return result

    def __getattr__(self, name: str) -> Any:
        try:
            return self.kwargs[name]
        except KeyError as err:
            val = getattr(self.wrapped, name)

        # decorate callables to return AdapterTypes
        if callable(val):
            def sticky_wrapper(*args, **kwargs):
                result = val(*args, **kwargs)
                if isinstance(result, ScalarType):
                    result = self.replace(wrapped=result)
                elif isinstance(result, CompositeType):
                    result = CompositeType(
                        {self.replace(wrapped=t) for t in result}
                    )
                return result

            return sticky_wrapper

        # wrap properties as AdapterTypes
        if isinstance(val, ScalarType):
            val = self.replace(wrapped=val)
        elif isinstance(val, CompositeType):
            val = CompositeType({self.replace(wrapped=t) for t in val})

        return val

    def __getitem__(self, key: Any) -> AdapterType:
        if isinstance(key, tuple):
            wrapped = resolve.resolve_type(key[0])
            return self(wrapped, *key[1:])

        return self(resolve.resolve_type(key))


#######################
####    PRIVATE    ####
#######################


cdef class DecoratorSorter:
    """Interface for controlling the insertion and sorting of nested
    AdapterTypes.
    """

    def __init__(self, type base_class):
        self.base_class = base_class

    cdef dict copy_parameters(self, AdapterType wrapper, dict kwargs):
        """Copy the parameters from one type to another"""
        return {
            param: getattr(wrapper, param) for param, value in kwargs.items()
            if value is None
        }

    def __call__(
        self,
        AdapterType instance,
        AdapterType stack,
        dict kwargs
    ) -> tuple:
        """Search the stack for """
        raise NotImplementedError(f"{type(self)} does not implement insort()")


# TODO: SparseType.resolve still uses _priority.
# -> some of this can be offloaded to parent


cdef class PrioritySorter(DecoratorSorter):
    """A DecoratorSorter that sorts nested decorators based on a given
    priority level.
    """

    def __init__(self, type base_class, int priority):
        super().__init__(base_class)
        self.priority = priority

    def __call__(
        self,
        AdapterType instance,
        AdapterType stack,
        dict kwargs
    ) -> tuple:
        # 1st order duplicate
        if isinstance(stack, self.base_class):
            return stack.wrapped, self.copy_parameters(stack, kwargs)

        # 2nd order duplicate
        for decorator in stack.adapters:
            next_ = decorator.wrapped
            if isinstance(next_, self.base_class):
                decorator.wrapped = instance
                return next_.wrapped, self.copy_parameters(next_, kwargs)

        # unique
        return stack, kwargs
