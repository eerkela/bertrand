"""This module describes a VectorType object, which represents a homogenous
vector type in the pdcast type system.
"""
from types import MappingProxyType
from typing import Any, Iterator

cimport cython
cimport numpy as np
import numpy as np

# from pdcast import resolve  # TODO: causes import error
from pdcast.util.structs cimport LRUDict
from pdcast.util.type_hints import type_specifier

from .registry cimport Type, AliasManager


# TODO: remove non top-level imports in __eq__, is_subtype


######################
####    PUBLIC    ####
######################


cdef Exception READ_ONLY_ERROR = (
    AttributeError("VectorType objects are read-only")
)


cdef class VectorType(Type):
    """Base type for :class:`ScalarType` and :class:`DecoratorType` objects.

    This allows inherited types to manage aliases and update them at runtime.
    """

    _encoder: ArgumentEncoder = None
    _cache_size: int = 0
    base_instance: VectorType = None

    def __init__(self, **kwargs):
        super().__init__()
        self._kwargs = kwargs

        if not type(self).base_instance:
            self.init_base()
        else:
            self.init_parametrized()

        self._hash = hash(self._slug)
        self._read_only = True

    @classmethod
    def set_encoder(cls, ArgumentEncoder encoder) -> None:
        """Inject a custom ArgumentEncoder to generate string identifiers for
        instances of this type.

        The output from these encoders directly determine how flyweights are
        identifed.
        """
        cls._encoder = encoder

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
        if not isinstance(type(self).name, str):
            raise TypeError(
                f"{repr(self)}.name must be a string, not {repr(self.name)}"
            )

        type(self).base_instance = self

        # pass name, parameters to encoder
        self.encoder = type(self)._encoder
        if self.encoder is None:
            self.encoder = ArgumentEncoder()
        self.encoder.set_name(self.name)
        self.encoder.set_kwargs(self._kwargs)
        self._slug = self.encoder((), {})  # encode self

        # create instance manager
        if not self._cache_size:
            self.instances = InstanceFactory(type(self))
        else:
            self.instances = FlyweightFactory(
                type(self),
                self.encoder,
                self._cache_size
            )
            self.instances._add(self._slug, self)

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
        cdef VectorType base = type(self).base_instance

        self.encoder = base.encoder
        self._slug = self.encoder((), self._kwargs)
        self.instances = base.instances

    ##########################
    ####    ATTRIBUTES    ####
    ##########################

    @property
    def name(self) -> str:
        """A unique name for each type.

        This must be defined at the **class level**.  It is used in conjunction
        with :meth:`encode() <ScalarType.encode>` to generate string
        representations of the associated type, which use this as their base.

        Returns
        -------
        str
            A unique string identifying each type.

        Notes
        -----
        Names can also be inherited from :func:`generic <generic>` types via
        :meth:`@ScalarType.register_backend <ScalarType.register_backend>`.
        """
        raise NotImplementedError(
            f"'{type(self).__name__}' is missing a `name` field."
        )

    @property
    def kwargs(self) -> MappingProxyType:
        """For parametrized types, the value of each parameter.

        Returns
        -------
        MappingProxyType
            A read-only view on the parameter values for this
            :class:`VectorType`.

        Notes
        -----
        This is conceptually similar to the ``_metadata`` field of numpy/pandas
        :class:`dtype <numpy.dtype>`\ /
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.
        """
        return MappingProxyType({} if self._kwargs is None else self._kwargs)

    @property
    def decorators(self) -> Iterator:
        """An iterator that yields each :class:`DecoratorType` that is attached
        to this :class:`VectorType <pdcast.VectorType>`.
        """
        yield from ()

    #######################
    ####    METHODS    ####
    #######################

    def unwrap(self) -> VectorType:
        """Remove all :class:`DecoratorTypes <pdcast.DecoratorType>` from this
        :class:`VectorType <pdcast.VectorType>`.
        """
        return self

    def replace(self, **kwargs) -> VectorType:
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
        ScalarType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method respects the immutability of :class:`ScalarType` objects.
        It always returns a flyweight with the new values.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self(**merged)

    def is_subtype(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Reverse of :meth:`ScalarType.contains`.

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
        This method performs the same check as :meth:`ScalarType.contains`,
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
        """Pass attribute lookups to :attr:`kwargs <pdcast.VectorType.kwargs>`.
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
        """Make :class:`VectorType <pdcast.VectorType>` instances read-only
        after ``__init__``.

        Explicit @property setters will still be invoked as normal.
        """
        # respect @property setters, if present
        prop = getattr(type(self), name, None)
        if hasattr(prop, "__set__"):
            prop.__set__(self, value)

        # prevent assignment outside __init__()
        elif self._read_only:
            raise READ_ONLY_ERROR
        else:
            self.__dict__[name] = value

    def __call__(self, *args, **kwargs) -> VectorType:
        """Constructor for parametrized types."""
        return self.instances(args, kwargs)

    def __contains__(self, other: type_specifier) -> bool:
        """Implement the ``in`` keyword for membership checks.

        This is equivalent to calling ``self.contains(other)``.
        """
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        """Compare two types for equality."""
        from pdcast.resolve import resolve_type

        other = resolve_type(other)
        return isinstance(other, VectorType) and hash(self) == hash(other)

    def __hash__(self) -> int:
        """Return the hash of this type's string identifier."""
        return self._hash

    def __str__(self) -> str:
        """Return this type's string identifier."""
        return self._slug

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"


##############################
####    IDENTIFICATION    ####
##############################


cdef class ArgumentEncoder:
    """An interface for creating string representations of a type based on its
    base name and parameters.
    """

    cdef void set_name(self, str name):
        """Set this encoder's base name, which will be prepended to every
        string it generates.
        """
        self.name = name

    cdef void set_kwargs(self, dict kwargs):
        """Set this encoder's expected arguments, which will be concatenated
        into a comma-separated list.
        """
        self.parameters = tuple(kwargs)
        self.defaults = {k: str(v) for k, v in kwargs.items()}

    @cython.wraparound(False)
    def __call__(self, tuple args, dict kwargs) -> str:
        """Construct a string representation with the given *args, **kwargs."""
        cdef unsigned int arg_length = len(args)
        cdef unsigned int kwarg_length = len(kwargs)
        cdef dict ordered = self.defaults.copy()
        cdef unsigned int i
        cdef str param

        for i in range(arg_length + kwarg_length):
            param = self.parameters[i]
            if i < arg_length:
                ordered[param] = str(args[i])
            else:
                ordered[param] = str(kwargs[param])

        if not ordered:
            return self.name
        return f"{self.name}[{', '.join(ordered.values())}]"


cdef class BackendEncoder:
    """A ArgumentEncoder that automatically appends a type's backend specifier as
    the first parameter of the returned slug.
    """

    def __init__(self, str backend):
        self.backend = backend

    @cython.wraparound(False)
    def __call__(self, tuple args, dict kwargs) -> str:
        """Construct a string representation with the given *args, **kwargs."""
        cdef unsigned int arg_length = len(args)
        cdef unsigned int kwarg_length = len(kwargs)
        cdef dict ordered = self.defaults.copy()
        cdef unsigned int i
        cdef str param

        for i in range(arg_length + kwarg_length):
            param = self.parameters[i]
            if i < arg_length:
                ordered[param] = str(args[i])
            else:
                ordered[param] = str(kwargs[param])

        if not ordered:
            return f"{self.name}[{self.backend}]"
        return f"{self.name}[{self.backend}, {', '.join(ordered.values())}]"


#############################
####    INSTANTIATION    ####
#############################


cdef class InstanceFactory:
    """An interface for controlling instance creation for
    :class:`VectorType <pdcast.VectorType>` objects.
    """

    def __init__(self, type base_class):
        self.base_class = base_class

    def __call__(self, tuple args, dict kwargs):
        return self.base_class(*args, **kwargs)


cdef class FlyweightFactory(InstanceFactory):
    """An InstanceFactory that implements the flyweight caching strategy."""

    def __init__(
        self,
        type base_class,
        ArgumentEncoder encoder,
        int cache_size
    ):
        super().__init__(base_class)
        self.encoder = encoder
        if cache_size < 0:
            self.cache = {}
        else:
            self.cache = LRUDict(maxsize=cache_size)

    def _add(self, str key, VectorType value) -> None:
        """Private method to manually add a key to the flyweight cache."""
        self.cache[key] = value

    def keys(self):
        """Dict-like ``keys()`` indexer."""
        return self.cache.keys()

    def values(self):
        """Dict-like ``values()`` indexer."""
        return self.cache.values()

    def items(self):
        """Dict-like ``items()`` indexer."""
        return self.cache.items()

    def __call__(self, tuple args, dict kwargs) -> VectorType:
        """Retrieve a previous instance or generate a new one according to the
        flyweight pattern.
        """
        cdef str slug
        cdef VectorType instance

        slug = self.encoder(args, kwargs)
        instance = self.cache.get(slug, None)
        if instance is None:
            instance = self.base_class(*args, **kwargs)
            self.cache[slug] = instance
        return instance

    def __contains__(self, str key) -> bool:
        """Check if the given identifier corresponds to a cached instance."""
        return key in self.cache

    def __getitem__(self, str key) -> VectorType:
        """Get an instance by its identifier."""
        return self.cache[key]

    def __len__(self) -> int:
        """Get the total number of cached instances."""
        return len(self.cache)

    def __iter__(self):
        """Iterate through the cached identifiers."""
        return iter(self.cache)

    def __repr__(self) -> str:
        return repr(self.cache)
