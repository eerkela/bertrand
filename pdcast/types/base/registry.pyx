"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any

from pdcast.util.type_hints import type_specifier, dtype_like

from .vector cimport VectorType
from .atomic cimport ScalarType, AbstractType


######################
####    PUBLIC    ####
######################


def register(class_: type = None, *, cond: bool = True):
    """Validate a scalar type definition and add it to the registry.

    Parameters
    ----------
    class_ : type
        The type definition to register.
    cond : bool, default True
        Used to create :ref:`conditional types <tutorial.conditional>`.  The
        type will only be added to the registry if this evaluates ``True``.

    Returns
    -------
    VectorType
        A base (unparametrized) instance of the decorated type.  This is always
        equal to the direct output of ``class_.instance()``, without arguments.

    See Also
    --------
    generic :
        for creating :ref:`hierarchical types <tutorial.hierarchy>`, which can
        contain other types.

    Notes
    -----
    The properties that this decorator validates are as follows:

        *   :attr:`class_.name <ScalarType.name>`: this must be unique or
            inherited from a :func:`generic() <pdcast.generic>` type.
        *   :attr:`class_.aliases <ScalarType.aliases>`: these must contain
            only valid type specifiers, each of which must be unique.
        *   :meth:`class_.encode() <ScalarType.encode>`: this must be a
            classmethod whose signature matches the decorated class's
            ``__init__``.

    Examples
    --------
    TODO: take from tutorial

    """
    def register_decorator(cls: type) -> type | VectorType:
        """Add the type to the registry and instantiate it."""
        if not issubclass(cls, VectorType):
            raise TypeError(
                "`@register` can only be applied to ScalarType and "
                "DecoratorType subclasses"
            )

        # short-circuit for conditional types
        if not cond:
            return cls

        # convert type into its base (non-parametrized) instance and register
        instance = cls()
        cls.registry.add(instance)

        # collect aliases associated with type
        aliases = {cls}
        try:
            aliases |= object.__getattribute__(cls, "aliases")
            del cls.aliases
        except AttributeError:
            pass

        for alias in aliases:
            instance.aliases.add(alias)  # registers with resolve_type()

        return instance

    if class_ is None:
        return register_decorator
    return register_decorator(class_)


cdef class TypeRegistry:
    """A registry containing the current state of the ``pdcast`` type system.

    See Also
    --------
    register : add a type to this registry.

    Notes
    -----
    This is a global object attached to every type that ``pdcast`` generates.
    It is responsible for caching base (unparametrized) instances for every
    type that can be returned by the :func:`detect_type() <pdcast.detect_type>`
    and :func:`resolve_type() <pdcast.resolve_type>` constructors.
    
    It also provides individual types a way of tying cached values to the
    global state of the type system more generally.  We can use this to compute
    properties only once, and automatically update them whenever a new type is
    added to the system.  This mechanism is used to synchronize aliases,
    subtypes, larger/smaller implementations, etc.
    """

    def __init__(self):
        self.instances = {}
        self.supertypes = {}
        self.subtypes = {}
        self.generics = {}
        self.implementations = {}
        self.defaults = {}
        self.pinned_aliases = []
        self.update_hash()

    #####################
    ####    STATE    ####
    #####################

    @property
    def hash(self) -> int:
        """A hash representing the current state of the ``pdcast`` type system.

        Notes
        -----
        This is updated whenever a new type is
        :meth:`added <pdcast.TypeRegistry.add>` or
        :meth:`removed <pdcast.TypeRegistry.remove>` from the registry.  It is
        also updated whenever a registered type
        :meth:`gains <pdcast.VectorType.register_alias>` or
        :meth:`loses <pdcast.VectorType.remove_alias>` an alias.
        """
        return self._hash

    def flush(self):
        """Reset the registry's internal state, forcing every property to be
        recomputed.
        """
        self._hash += 1

    def add(self, instance: VectorType) -> None:
        """Validate a base type and add it to the registry.

        Parameters
        ----------
        instance : VectorType
            An instance of a :class:`VectorType <pdcast.VectorType>` to add to
            the registry.  This instance must not be parametrized, and it must
            implement at least the :attr:`name <pdcast.VectorType.name>` and
            :attr:`aliases <pdcast.VectorType.aliases>` attributes  to be
            considered valid.

        Raises
        ------
        TypeError
            If the instance is malformed in some way.  This can happen if the
            type is parametrized, does not have an appropriate
            :attr:`name <pdcast.VectorType.name>` or
            :attr:`aliases <pdcast.VectorType.aliases>`, or if the signature of
            its :meth:`encode <pdcast.VectorType.encode>` method does not
            match its constructor.

        See Also
        --------
        register : automatically call this method as a class decorator.
        TypeRegistry.remove : remove a type from the registry.
        TypeRegistry.clear : remove all types from the registry.
        """
        # validate instance is not parametrized
        if instance != instance.base_instance:
            raise TypeError(f"{repr(instance)} must not be parametrized")

        # validate type is not already registered
        if type(instance) in self.instances:
            previous = self.instances[type(instance)]
            raise RuntimeError(
                f"{type(instance)} is already registered to {repr(previous)}"
            )

        # validate identifier is unique
        slug = str(instance)
        observed = {str(typ): typ for typ in self.instances.values()}
        if slug in observed:
            existing = observed[slug]
            raise TypeError(
                f"{repr(instance)} slug must be unique: '{slug}' is currently "
                f"registered to {repr(existing)}"
            )

        self.instances[type(instance)] = instance
        self.update_hash()

    def remove(self, instance: VectorType) -> None:
        """Remove a base type from the registry.

        Parameters
        ----------
        instance : VectorType
            The type to remove.

        Raises
        ------
        KeyError
            If the instance is not in the registry.  This will also be raised
            if the instance is parametrized.

        See Also
        --------
        TypeRegistry.add : add a type to the registry.
        TypeRegistry.clear : remove all types from the registry.
        """
        del self.instances[type(instance)]

        # remove all aliases
        instance.aliases.clear()

        # recur for each of the instance's children
        for typ in instance.subtypes:
            self.remove(typ)
        for typ in getattr(instance, "backends", {}).values():
            self.remove(typ)

        self.update_hash()

    #####################
    ####    LINKS    ####
    #####################

    def get_supertype(self, ScalarType typ) -> AbstractType:
        """Get a type's supertype if it is registered."""
        result = self.supertypes.get(type(typ), None)
        return self.instances.get(result, None)

    def get_subtypes(self, AbstractType typ) -> set:
        """Get all the registered subtypes associated with a type."""
        result = set()
        
        candidates = self.subtypes.get(type(typ), set())
        for subtype in candidates:
            instance = self.instances.get(subtype, None)
            if instance is None:
                continue
            result.add(instance)

        return result

    def get_generic(self, ScalarType typ) -> AbstractType:
        """Get a type's generic implementation if it is registered."""
        result = self.generics.get(type(typ), None)
        if result is not None:
            result = self.instances.get(result, None)
        return result

    def get_implementations(self, AbstractType typ) -> dict:
        """Get all the registered implementations associated with a type."""
        result = {}
        candidates = self.implementations.get(type(typ), {})
        for backend, implementation in candidates.items():
            instance = self.instances.get(implementation, None)
            if instance is None:
                continue
            result[backend] = instance

        return result

    def get_default(self, AbstractType typ) -> ScalarType:
        """Get the default implementation for a hierarchical type."""
        default = self.defaults.get(type(typ), None)
        default = self.instances.get(default, None)
        if default is None:
            raise NotImplementedError(
                f"{repr(typ)} has no default implementation"
            )
        return default

    #####################
    ####    REGEX    ####
    #####################

    @property
    def aliases(self) -> MappingProxyType:
        """An up-to-date mapping of every alias to its corresponding type.

        This encodes every specifier recognized by both the
        :func:`detect_type() <pdcast.detect_type>` and
        :func:`resolve_type() <pdcast.resolve_type>` constructors.

        See Also
        --------
        TypeRegistry.regex :
            A regular expression to match strings in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.
        TypeRegistry.resolvable :
            A regular expression that matches any number of individual type
            specifiers.

        Notes
        -----
        This is a cached property tied to the current state of the registry.
        Whenever a new type is :meth:`added <pdcast.TypeRegistry.add>`,
        :meth:`removed <pdcast.TypeRegistry.remove>`, or
        :meth:`gains <pdcast.VectorType.register_alias>`\ /
        :meth:`loses <pdcast.VectorType.remove_alias>` an alias, it will be
        regenerated to reflect that change.
        """
        cached = self._aliases
        if not cached:
            result = {
                alias: manager.instance
                for manager in self.pinned_aliases for alias in manager
            }
            cached = CacheValue(MappingProxyType(result))
            self._aliases = cached

        return cached.value

    @property
    def regex(self) -> re.Pattern:
        """A compiled regular expression that matches strings in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        See Also
        --------
        TypeRegistry.aliases :
            A complete map of every alias to its corresponding type.
        TypeRegistry.resolvable :
            A regular expression that matches any number of these expressions.

        Notes
        -----
        This expression uses `recursive regular expressions
        <https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_
        to match nested type specifiers.  This is enabled by the alternate
        Python `regex <https://pypi.org/project/regex/>`_ engine, which is
        PERL-compatible.  It is otherwise equivalent to the base Python
        :mod:`re <python:re>` package.
        """
        cached = self._regex
        if not cached:
            # trivial case: empty registry
            if not self.aliases:
                result = re.compile(".^")  # matches nothing
            else:
                # escape regex characters
                alias_strings = [
                    re.escape(alias) for alias in self.aliases
                    if isinstance(alias, str)
                ]

                # special case for sized unicode in numpy syntax
                alias_strings.append(r"(?P<sized_unicode>U(?P<size>[0-9]*))$")

                # sort longest first and join with regex OR
                alias_strings.sort(key=len, reverse=True)
                result = re.compile(
                    rf"(?P<type>{'|'.join(alias_strings)})"
                    rf"(?P<nested>\[(?P<args>([^\[\]]|(?&nested))*)\])?"
                )

            cached = CacheValue(result)
            self._regex = cached

        return cached.value

    @property
    def resolvable(self) -> re.Pattern:
        """A compiled regular expression that matches any number of specifiers
        in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        See Also
        --------
        TypeRegistry.aliases :
            A complete map of every alias to its corresponding type.
        TypeRegistry.regex :
            A regular expression to match individual specifiers.

        Notes
        -----
        This expression uses `recursive regular expressions
        <https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_
        to match nested type specifiers.  This is enabled by the alternate
        Python `regex <https://pypi.org/project/regex/>`_ engine, which is
        PERL-compatible.  It is otherwise equivalent to the base Python
        :mod:`re <python:re>` package.
        """
        cached = self._resolvable
        if not cached:
            # match full string and allow for comma-separated repetition
            pattern = rf"(?P<atomic>{self.regex.pattern})(,\s*(?&atomic))*"

            # various prefixes/suffixes to be ignored
            lead = "|".join([
                r"CompositeType\(\{",
                r"\{",
            ])
            follow = "|".join([
                r"\}\)",
                r"\}",
            ])
            pattern = rf"({lead})?(?P<body>{pattern})({follow})?"

            cached = CacheValue(re.compile(pattern))
            self._resolvable = cached

        return cached.value

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self._hash = hash(tuple(self.instances))

    cdef void pin(self, Type instance, AliasManager aliases):
        """Pin a type to the global alias namespace if it is not already being
        tracked.
        """
        for manager in self.pinned_aliases:
            if manager.instance is instance:
                break
        else:
            self.pinned_aliases.append(aliases)

    cdef void unpin(self, Type instance):
        """Unpin a type from the global alias namespace."""
        self.pinned_aliases = [
            manager for manager in self.pinned_aliases
            if manager.instance is not instance
        ]

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, val) -> bool:
        if not isinstance(val, type):
            val = type(val)
        return val in self.instances

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self):
        return iter(self.instances.values())

    def __len__(self) -> int:
        return len(self.instances)

    def __str__(self) -> str:
        return str(set(self.instances.values()))

    def __repr__(self) -> str:
        return repr(set(self.instances.values()))


cdef class AliasManager:
    """Interface for dynamically managing a type's aliases."""

    def __init__(self, Type instance):
        self.instance = instance
        self.aliases = set()

    #############################
    ####    SET INTERFACE    ####
    #############################

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
        alias = self.normalize_specifier(alias)

        registry = Type.registry
        if alias in registry.aliases:
            other = registry.aliases[alias]
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to "
                    f"{repr(other)}"
                )

        if not self:
            self.pin()
        self.aliases.add(alias)
        registry.flush()  # rebuild regex patterns

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
        alias = self.normalize_specifier(alias)

        self.aliases.remove(alias)
        if not self:
            self.unpin()
        Type.registry.flush()  # rebuild regex patterns

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
        value = self.aliases.pop()
        if not self:
            self.unpin()
        Type.registry.flush()
        return value

    def clear(self) -> None:
        """Remove every alias that is registered to the managed type.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        if self:
            self.unpin()
        self.aliases.clear()
        Type.registry.flush()  # rebuild regex patterns

    ##############################
    ####    SET OPERATIONS    ####
    ##############################

    def __or__(self, aliases: set) -> set:
        return self.aliases | aliases

    def __and__(self, aliases: set) -> set:
        return self.aliases & aliases

    def __sub__(self, aliases: set) -> set:
        return self.aliases - aliases

    def __xor__(self, aliases: set) -> set:
        return self.aliases ^ aliases

    #######################
    ####    PRIVATE    ####
    #######################

    cdef object normalize_specifier(self, alias: type_specifier):
        """Preprocess a type specifier, converting it into a recognizable
        format.
        """
        if not isinstance(alias, type_specifier):
            raise TypeError(
                f"alias must be a valid type specifier: {repr(alias)}"
            )

        # ignore parametrized dtypes
        if isinstance(alias, dtype_like):
            return type(alias)

        return alias

    cdef void pin(self):
        """Pin the associated instance to the global alias namespace."""
        cdef TypeRegistry registry = Type.registry

        registry.pin(self.instance, self)

    cdef void unpin(self):
        cdef TypeRegistry registry = Type.registry

        registry.unpin(self.instance)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __bool__(self) -> bool:
        return bool(self.aliases)

    def __len__(self) -> int:
        return len(self.aliases)

    def __contains__(self, alias: type_specifier) -> bool:
        return alias in self.aliases

    def __iter__(self):
        return iter(self.aliases)

    def __repr__(self):
        return repr(self.aliases)

    def __str__(self):
        return str(self.aliases)


cdef class Type:
    """Base type for all type objects.

    This has no interface of its own.  It simply serves to anchor inheritance
    and distribute the shared type registry to all ``pdcast`` types.
    """

    registry: TypeRegistry = TypeRegistry()

    def __init__(self):
        self._aliases = AliasManager(self)

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
                <ScalarType.resolve>` of the associated type.
            *   Numpy/pandas :class:`dtype <numpy.dtype>`\ /\
                :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
                objects are used by :func:`detect_type` for *O(1)* type
                inference.  In both cases, parametrized dtypes can be handled
                by adding a root dtype to :attr:`aliases <ScalarType.aliases>`.
                For numpy :class:`dtypes <numpy.dtype>`, this will be the
                root of their :func:`numpy.issubdtype` hierarchy.  For pandas
                :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>`,
                it is its :class:`type() <python:type>` directly.  When either
                of these are encountered, they will invoke the type's
                :meth:`from_dtype() <ScalarType.from_dtype>` constructor.
            *   Raw Python types are used by :func:`detect_type` for scalar or
                unlabeled vector inference.  If the type of a scalar element
                appears in :attr:`aliases <ScalarType.aliases>`, then the
                associated type's :meth:`detect() <ScalarType.detect>` method
                will be called on it.

        All aliases are recognized by :func:`resolve_type` and the set always
        includes the :class:`ScalarType` itself.
        """
        return self._aliases


#######################
####    PRIVATE    ####
#######################


cdef class CacheValue:
    """A simple struct to hold cached values tied to the global state of the
    registry.
    """

    def __init__(self, object value):
        self.value = value
        self.hash = Type.registry.hash

    def __bool__(self) -> bool:
        """Indicates whether a cached registry value is out of date."""
        return self.hash == Type.registry.hash
