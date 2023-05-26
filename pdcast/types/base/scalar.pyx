"""This module describes a ScalarType object, which represents a homogenous
vector type in the pdcast type system.
"""
import inspect
from types import MappingProxyType
from typing import Any, Iterator

cimport numpy as np
import numpy as np
import pandas as pd

# from pdcast import resolve  # TODO: causes import error
from pdcast.util.structs import LRUDict
from pdcast.util.type_hints import type_specifier

from .registry cimport BaseType, AliasManager


# TODO: @subtype can be attached directly to ScalarType using the manager
# pattern, and therefore separated from @generic.  We just create a
# TypeHierarchy interface with SubtypeHierarchy as a concretion.


# TODO: remove non top-level imports in __eq__, is_subtype


# TODO: how does this interact with HierarchicalType?


##########################
####    PRIMITIVES    ####
##########################


cdef class ScalarType(BaseType):
    """Base type for :class:`AtomicType` and :class:`AdapterType` objects.

    This allows inherited types to manage aliases and update them at runtime.
    """

    def __init__(self, **kwargs):
        self._kwargs = kwargs

        if hasattr(self, "_base_instance"):
            self.init_parametrized()
        else:
            self.init_base()

        self._hash = hash(self._slug)

    cdef void init_base(self):
        """Initialize a base (non-parametrized) instance of this type.

        Notes
        -----
        This is automatically called by @register, @generic, and @supertype
        to transform the decorated class into a non-parametrized instance.  By
        assigning attributes to this base instance, we can replicate the
        behavior of an __init_subclass__ method without actually requiring a
        metaclass.

        Doing it this way guarantees cython compatibility and avoids confusion
        with the special semantics around __init_subclass__ and metaclasses in
        general.

        .. note::

            In Cython 0.29.x, __init_subclass__ applies only to python classes
            that inherit from an extension type.  It will never be invoked when
            an extension type inherits from another extension type.

            As of Cython 3.0.x, any extension type that defines an
            __init_subclass__ method will raise a compilation error instead.

        """
        raise NotImplementedError(
            f"{repr(type(self).__qualname__)} must implement an init_base() "
            f"method"
        )

    cdef void init_parametrized(self):
        """Initialize a parametrized instance of this type with attributes
        from the base instance.

        Notes
        -----
        This allows us to copy attributes from the base instance (from
        init_base) without requiring manipulation of class attributes or
        metaclasses.

        Every attribute that is assigned in init_base should also be assigned
        here.
        """
        raise NotImplementedError(
            f"{repr(type(self).__qualname__)} must implement an "
            "init_parametrized() method"
        )

    ##########################
    ####    ATTRIBUTES    ####
    ##########################

    @property
    def name(self) -> str:
        """A unique name for each type.

        This must be defined at the **class level**.  It is used in conjunction
        with :meth:`slugify() <AtomicType.slugify>` to generate string
        representations of the associated type, which use this as their base.

        Returns
        -------
        str
            A unique string identifying each type.

        Notes
        -----
        Names can also be inherited from :func:`generic <generic>` types via
        :meth:`@AtomicType.register_backend <AtomicType.register_backend>`.
        """
        raise NotImplementedError(
            f"'{type(self).__name__}' is missing a `name` field."
        )

    @property
    def aliases(self) -> AliasManager:
        """A set of unique aliases for this type.
    
        These must be defined at the **class level**, and are used by
        :func:`detect_type` and :func:`resolve_type` to map aliases onto their
        corresponding types.

        Returns
        -------
        set[str | type | numpy.dtype]
            A set containing all the aliases that are associated with this
            type.

        Notes
        -----
        Special significance is given to the type of each alias:

            *   Strings are used by the :ref:`type specification mini-language
                <resolve_type.mini_language>` to trigger :meth:`resolution
                <AtomicType.resolve>` of the associated type.
            *   Numpy/pandas :class:`dtype <numpy.dtype>`\ /\
                :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
                objects are used by :func:`detect_type` for *O(1)* type
                inference.  In both cases, parametrized dtypes can be handled
                by adding a root dtype to :attr:`aliases <AtomicType.aliases>`.
                For numpy :class:`dtypes <numpy.dtype>`, this will be the
                root of their :func:`numpy.issubdtype` hierarchy.  For pandas
                :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>`,
                it is its :class:`type() <python:type>` directly.  When either
                of these are encountered, they will invoke the type's
                :meth:`from_dtype() <AtomicType.from_dtype>` constructor.
            *   Raw Python types are used by :func:`detect_type` for scalar or
                unlabeled vector inference.  If the type of a scalar element
                appears in :attr:`aliases <AtomicType.aliases>`, then the
                associated type's :meth:`detect() <AtomicType.detect>` method
                will be called on it.

        All aliases are recognized by :func:`resolve_type` and the set always
        includes the :class:`AtomicType` itself.
        """
        return self._aliases

    @property
    def instances(self) -> InstanceFactory:
        """"""
        return self._instances

    @property
    def kwargs(self) -> MappingProxyType:
        """For parametrized types, the value of each parameter.

        Returns
        -------
        MappingProxyType
            A read-only view on the parameter values for this
            :class:`ScalarType`.

        Notes
        -----
        This is conceptually similar to the ``_metadata`` field of numpy/pandas
        :class:`dtype <numpy.dtype>`\ /
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.
        """
        return MappingProxyType(self._kwargs)

    @property
    def adapters(self) -> Iterator:
        """An iterator that yields each :class:`AdapterType` that is attached
        to this :class:`ScalarType <pdcast.ScalarType>`.
        """
        yield from ()

    #######################
    ####    METHODS    ####
    #######################

    def unwrap(self) -> ScalarType:
        """Remove all :class:`AdapterTypes <pdcast.AdapterType>` from this
        :class:`ScalarType <pdcast.ScalarType>`.
        """
        return self

    def replace(self, **kwargs) -> ScalarType:
        """Return a modified copy of a type with the values specified in
        ``**kwargs``.

        Parameters
        ----------
        **kwargs : dict
            keyword arguments corresponding to attributes of this type.  Any
            arguments that are not specified will be replaced with the current
            values for this type.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method respects the immutability of :class:`AtomicType` objects.
        It always returns a flyweight with the new values.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self(**merged)

    def is_subtype(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Reverse of :meth:`AtomicType.contains`.

        Parameters
        ----------
        other : type specifier
            The type to check for membership.  This can be in any
            representation recognized by :func:`resolve_type`.
        include_subtypes : bool, default True
            Controls whether to include subtypes for this comparison.  If this
            is set to ``False``, then subtypes will be excluded.  Backends will
            still be considered, but only at the top level.

        Returns
        -------
        bool
            ``True`` if ``self`` is a member of ``other``\'s hierarchy.
            ``False`` otherwise.

        Notes
        -----
        This method performs the same check as :meth:`AtomicType.contains`,
        except in reverse.  It is functionally equivalent to
        ``other.contains(self)``.
        """
        from pdcast.resolve import resolve_type

        other = resolve_type(other)
        return other.contains(self, include_subtypes=include_subtypes)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getattr__(self, name: str) -> Any:
        """Pass attribute lookups to :attr:`kwargs <pdcast.ScalarType.kwargs>`.
        """
        try:
            return self.kwargs[name]
        except KeyError as err:
            err_msg = (
                f"{repr(type(self).__name__)} object has no attribute: "
                f"{repr(name)}"
            )
            raise AttributeError(err_msg) from err

    def __setattr__(self, str name, object value) -> None:
        """Make :class:`ScalarType <pdcast.ScalarType>` instances read-only
        after ``__init__``.

        Explicit @property setters will still be invoked as normal.
        """
        # respect @property setters, if present
        prop = getattr(type(self), name, None)
        if hasattr(prop, "__set__"):
            prop.__set__(self, value)

        # prevent assignment outside __init__()
        elif self._is_frozen:
            raise AttributeError("ScalarType objects are read-only")
        else:
            self.__dict__[name] = value

    def __call__(self, *args, **kwargs) -> ScalarType:
        """Constructor for parametrized types."""
        return self._instances(*args, **kwargs)

    def __getitem__(self, key: Any) -> ScalarType:
        """Return a parametrized type in the same syntax as the type
        specification mini-language.
        """
        if not isinstance(key, tuple):
            key = (key,)

        return self(*key)

    def __contains__(self, other: type_specifier) -> bool:
        """Implement the ``in`` keyword for membership checks.

        This is equivalent to calling ``self.contains(other)``.
        """
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        """Compare two types for equality."""
        from pdcast.resolve import resolve_type

        other = resolve_type(other)
        return isinstance(other, ScalarType) and hash(self) == hash(other)

    def __hash__(self) -> int:
        """Return the hash of this type's slug."""
        return self._hash

    def __str__(self) -> str:
        return self._slug

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"
