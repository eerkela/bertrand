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


##########################
####    PRIMITIVES    ####
##########################


cdef class ScalarType(BaseType):
    """Base type for :class:`AtomicType` and :class:`AdapterType` objects.

    This allows inherited types to manage aliases and update them at runtime.
    """

    def __init__(self, **kwargs):
        self._kwargs = kwargs
        self._slug = self.instances.slugify((), kwargs)
        self._hash = hash(self._slug)

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
        raise NotImplementedError(
            f"'{type(self).__name__}' is missing an `aliases` field."
        )

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

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        """Metaclass initializer for flyweight pattern."""
        super(ScalarType, cls).__init_subclass__(**kwargs)
        cls.aliases = AliasManager(cls.aliases | {cls})

        # if cls.__init__ is ScalarType.__init__:
        #     parameters = ()
        # else:
        #     parameters = tuple(inspect.signature(cls).parameters.keys())
        parameters = ()
        cls.instances = NullFactory(cls, parameters)

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
        return self.instances(*args, **kwargs)

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


#########################
####    FACTORIES    ####
#########################


cdef class InstanceFactory:
    """An interface for controlling instance creation for
    :class:`ScalarType <pdcast.ScalarType>` objects.
    """

    def __init__(self, type base_class, tuple parameters):
        self.base_class = base_class
        if not isinstance(base_class.name, str):
            self.name = None
        else:
            self.name = base_class.name
        self.parameters = parameters

    def slugify(self, tuple args, dict kwargs) -> str:
        """Convert arbitrary arguments into a suitable slug identifier."""
        cdef object args_iter
        cdef object ordered
        cdef str params

        args_iter = iter(args)
        ordered = (
            str(kwargs[param]) if param in kwargs else str(next(args_iter))
            for param in self.parameters
        )
        params = ", ".join(ordered)
        if not params:
            return self.name
        return f"{self.name}[{params}]"

    def __call__(self, *args, **kwargs):
        raise NotImplementedError()


cdef class NullFactory(InstanceFactory):
    """An InstanceFactory that passes inputs to the normal class constructor."""

    def __call__(self, *args, **kwargs):
        return self.base_class(*args, **kwargs)


cdef class FlyweightFactory(InstanceFactory):
    """An InstanceFactory that implements the flyweight caching strategy."""

    def __init__(
        self,
        type base_class,
        tuple parameters,
        unsigned int cache_size
    ):
        super().__init__(base_class, parameters)
        self.cache_size = cache_size
        if not cache_size:
            self.instances = {}
        else:
            self.instances = LRUDict(maxsize=cache_size)

    def __call__(self, *args, **kwargs) -> ScalarType:
        cdef str slug
        cdef ScalarType instance

        slug = self.slugify(args, kwargs)
        instance = self.instances.get(slug, None)
        if instance is None:
            instance = self.base_class(*args, **kwargs)
            self.instances[slug] = instance
        return instance


cdef class ImplementationFactory(FlyweightFactory):
    """A special case of FlyweightFactory that accounts for backend-specific
    slug generation.
    """

    def __init__(
        self,
        type base_class,
        unsigned int cache_size,
        tuple parameters,
        str backend
    ):
        super().__init__(base_class, parameters, cache_size)
        self.backend = backend

    def slugify(self, tuple args, dict kwargs) -> str:
        cdef object args_iter
        cdef object ordered
        cdef str params

        args_iter = iter(args)
        ordered = (
            str(kwargs[param]) if param in kwargs else str(next(args_iter))
            for param in self.parameters
        )
        params = ", ".join(ordered)
        if not params:
            return f"{self.name}[{self.backend}]"
        return f"{self.name}[{self.backend}, {params}]"
