from types import MappingProxyType
from typing import Any, Iterator

from pdcast import resolve
from pdcast.util.structs cimport LRUDict
from pdcast.util.type_hints import type_specifier

from . cimport registry


##########################
####    PRIMITIVES    ####
##########################


cdef class ScalarType(registry.BaseType):
    """Base type for :class:`AtomicType` and :class:`AdapterType` objects.

    This allows inherited types to manage aliases and update them at runtime.
    """

    def __init__(self, **kwargs):
        self._kwargs = kwargs
        self._slug = self.slugify(**kwargs)
        self._hash = hash(self._slug)
        self._is_frozen = True  # no new attributes beyond this point

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
    def aliases(self) -> set:
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

    def unwrap(self) -> ScalarType:
        """Remove all :class:`AdapterTypes <pdcast.AdapterType>` from this
        :class:`ScalarType <pdcast.ScalarType>`.
        """
        return self

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def instance(cls, *args, **kwargs) -> ScalarType:
        """The preferred constructor for :class:`ScalarType` objects.

        This method is responsible for implementing the
        `flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_
        pattern for :class:`AtomicType` objects.

        Parameters
        ----------
        *args, **kwargs
            Arguments passed to this type's
            :meth:`slugify() <AtomicType.slugify>` and
            :class:`__init__ <AtomicType>` methods.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same inputs again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method should never be overriden.  Together with a corresponding
        :meth:`slugify() <AtomicType.slugify>` method, it ensures that no
        leakage occurs with the flyweight cache, which could result in
        uncontrollable memory consumption.

        Users should use this method in place of :class:`__init__ <AtomicType>`
        to ensure that the `flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_
        pattern is observed.  One can manually check the cache that is used for
        this method under a type's ``.flyweights`` attribute.
        """
        cdef str slug
        cdef ScalarType result

        slug = cls.slugify(*args, **kwargs)
        result = cls.flyweights.get(slug, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[slug] = result

        return result

    #######################
    ####    ALIASES    ####
    #######################

    @classmethod
    def register_alias(cls, alias: Any, overwrite: bool = False) -> None:
        """Register a new alias for this type.

        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.

        Parameters
        ----------
        alias : Any
            A string, ``dtype``, ``ExtensionDtype``, or scalar type to register
            as an alias of this type.
        overwrite : bool, default False
            Indicates whether to overwrite existing aliases (``True``) or
            raise an error (``False``) in the event of a conflict.
        """
        if alias in cls.registry.aliases:
            other = cls.registry.aliases[alias]
            if other is cls:
                return None
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to {other}"
                )
        cls.aliases.add(alias)
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def remove_alias(cls, alias: Any) -> None:
        """Remove an alias from this type.

        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.

        Parameters
        ----------
        alias : Any
            The alias to remove.  This can be a string, ``dtype``,
            ``ExtensionDtype``, or scalar type that is present in this type's
            ``.aliases`` attribute.
        """
        del cls.aliases[alias]
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def clear_aliases(cls) -> None:
        """Remove every alias that is registered to this type.

        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        cls.aliases.clear()
        cls.registry.flush()  # rebuild regex patterns

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        """Metaclass initializer for flyweight pattern."""
        # allow cooperative inheritance
        super(ScalarType, cls).__init_subclass__(**kwargs)

        # cls always aliases itself
        cls.aliases.add(cls)

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

    def __setattr__(self, name: str, value: Any) -> None:
        if self._is_frozen:
            raise AttributeError("ScalarType objects are read-only")
        else:
            self.__dict__[name] = value

    def __contains__(self, other: type_specifier) -> bool:
        """Implement the ``in`` keyword for membership checks.

        This is equivalent to calling ``self.contains(other)``.
        """
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        """Compare two types for equality."""
        other = resolve.resolve_type(other)
        return isinstance(other, ScalarType) and hash(self) == hash(other)

    def __hash__(self) -> int:
        """Return the hash of this type's slug."""
        return self._hash

    def __str__(self) -> str:
        return self._slug

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"
