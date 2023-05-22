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

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        """Metaclass initializer for flyweight pattern."""
        # allow cooperative inheritance
        super(ScalarType, cls).__init_subclass__(**kwargs)

        # cls always aliases itself
        cls.aliases = AliasManager(cls.aliases | {cls})

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

    def __getitem__(self, key: Any) -> ScalarType:
        if isinstance(key, tuple):
            return self.instance(*key)

        return self.instance(key)

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


#######################
####    PRIVATE    ####
#######################


cdef class AliasManager:
    """Interface for dynamically managing a :class:`ScalarType`'s aliases."""

    def __init__(self, set aliases):
        for alias in aliases:
            if alias in ScalarType.registry.aliases:
                raise TypeError(
                    f"alias {repr(alias)} is already registered to "
                    f"{str(ScalarType.registry.aliases[alias])}"
                )
        self._aliases = aliases

    def _check_type_specifier(self, alias: type_specifier) -> None:
        """Ensure that an alias is a valid type specifier."""
        if not isinstance(alias, type_specifier):
            raise TypeError(
                f"alias must be a valid type specifier: {repr(alias)}"
            )

    def add(self, alias: type_specifier, overwrite: bool = False) -> None:
        """Alias a type specifier to the managed type.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to register as an alias of the managed type.
        overwrite : bool, default False
            Indicates whether to overwrite existing aliases (``True``) or
            raise an error (``False``) in the event of a conflict.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        self._check_type_specifier(alias)

        if alias in ScalarType.registry.aliases:
            other = ScalarType.registry.aliases[alias]
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to {other}"
                )

        self._aliases.add(alias)
        ScalarType.registry.flush()  # rebuild regex patterns

    def remove(self, alias: type_specifier) -> None:
        """Remove an alias from the managed type.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to remove from the managed type's aliases.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        self._check_type_specifier(alias)
        self._aliases.remove(alias)
        ScalarType.registry.flush()  # rebuild regex patterns

    def discard(self, alias: type_specifier) -> None:
        """Remove an alias from the managed type if it is present.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to remove from the managed type's aliases.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        try:
            self.remove(alias)
        except KeyError:
            pass

    def pop(self) -> type_specifier:
        """Pop an alias from the managed type.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        value = self._aliases.pop()
        ScalarType.registry.flush()
        return value

    def clear(self) -> None:
        """Remove every alias that is registered to the managed type.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        self._aliases.clear()
        ScalarType.registry.flush()  # rebuild regex patterns

    def __or__(self, aliases: set) -> set:
        return self._aliases | aliases

    def __and__(self, aliases: set) -> set:
        return self._aliases & aliases

    def __sub__(self, aliases: set) -> set:
        return self._aliases - aliases

    def __xor__(self, aliases: set) -> set:
        return self._aliases ^ aliases

    def __contains__(self, alias: type_specifier) -> bool:
        return alias in self._aliases

    def __iter__(self):
        return iter(self._aliases)

    def __repr__(self):
        return repr(self._aliases)
