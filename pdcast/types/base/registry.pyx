"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any, Iterable

from pdcast.util.type_hints import type_specifier, dtype_like

from .composite cimport CompositeType
from .vector cimport VectorType
from .decorator cimport DecoratorType
from .scalar cimport AbstractType, ScalarType


# TODO: adding aliases during init_base automatically appends them to
# pinned_aliases and makes them available from registry.aliases.  This may not
# be desirable.
# -> Handle aliases during TypeRegistry.add()


######################
####    PUBLIC    ####
######################


def register(class_: type = None, *, cond: bool = True):
    """Register a :class:`VectorType <pdcast.VectorType>` subclass, adding it
    to the shared :class:`registry <pdcast.TypeRegistry>`.

    Parameters
    ----------
    class_ : type
        The type definition to register.  This must be a subclass of
        :class:`VectorType <pdcast.VectorType>`.
    cond : bool, default True
        Used to create :ref:`conditional types <register.conditional>`.  The
        type will only be registered if this evaluates to ``True``.

    Returns
    -------
    VectorType
        A base (unparametrized) instance of the decorated type.  This can be
        used interchangeably with its parent class in most cases.

    Raises
    ------
    TypeError
        If the type is invalid or its name conflicts with another registered
        type.
    ValueError
        If any of the type's aliases are already registered to another type.

    Notes
    -----
    This decorator must be listed at the top of a type definition for it to be
    recognized by :func:`detect_type <pdcast.detect_type>` and
    :func:`resolve_type <pdcast.resolve_type>`.  No other decorators should be
    placed above it.
    """
    def register_decorator(cls: type) -> type | VectorType:
        """Add the type to the registry and instantiate it."""
        if not issubclass(cls, VectorType):
            raise TypeError(
                f"@register can only be applied to VectorType subclasses, not "
                f"{cls}"
            )

        if issubclass(cls, DecoratorType):
            add_to_decorator_priority(cls)

        # short-circuit for conditional types
        if not cond:
            return cls

        # convert type into its base (non-parametrized) instance and register
        instance = cls()
        cls.registry.add(instance)
        return instance

    if class_ is None:
        return register_decorator
    return register_decorator(class_)


cdef class TypeRegistry:
    """A global registry containing the current state of the ``pdcast`` type
    system.

    This object encodes all the types that are currently
    :func:`registered <pdcast.register>` with the ``pdcast`` type system.  It
    is responsible for caching base (unparametrized) instances for each type,
    as well as maintaining the links between them and controlling their
    creation through the :func:`detect_type() <pdcast.detect_type>` and
    :func:`resolve_type() <pdcast.resolve_type>` constructors.

    See Also
    --------
    register : Add a type to this registry as a class decorator.
    """

    def __init__(self):
        self.instances = {}
        self.pinned_aliases = []
        self.names = {}

        self.defaults = {}
        self.supertypes = {}
        self.subtypes = {}
        self.generics = {}
        self.implementations = {}

        self._decorator_priority = PriorityList()
        self.update_hash()

    ############################
    ####    REGISTRATION    ####
    ############################

    def add(self, typ: type | VectorType) -> None:
        """Validate a type and add it to the registry.

        Parameters
        ----------
        typ : type | VectorType
            A subclass or instance of :class:`VectorType <pdcast.VectorType>`
            to add to the registry.  If an instance is given, it must not be
            parametrized.

        Raises
        ------
        TypeError
            If the type is not a subclass or instance of
            :class:`VectorType <pdcast.VectorType>`, or if it is parametrized
            in some way.
        NotImplementedError
            If the type does not implement an appropriate
            :attr:`name <pdcast.VectorType.name>` attribute.
        ValueError
            If the type has an :attr:`aliases <pdcast.VectorType.aliases>`
            attribute and any of its aliases conflict with those of another
            registered type.

        See Also
        --------
        register : automatically call this method as a class decorator.
        TypeRegistry.remove : remove a type from the registry.

        Examples
        --------
        .. doctest::

            >>> class CustomType(pdcast.ScalarType):
            ...     name = "foo"
            ...     aliases = {"bar"}

            >>> pdcast.registry.add(CustomType)
            >>> CustomType in pdcast.registry
            True
            >>> pdcast.resolve_type("bar")
            CustomType()

        .. testcleanup::

            pdcast.registry.remove(CustomType)
        """
        # validate type is a subclass of VectorType
        if isinstance(typ, type):
            if not issubclass(typ, VectorType):
                raise TypeError(f"type must be a subclass of VectorType: {typ}")
            typ = typ() if not typ.base_instance else typ.base_instance

        elif not isinstance(typ, VectorType):
            raise TypeError(f"type must be an instance of VectorType: {typ}")

        # validate instance is not parametrized
        if typ != typ.base_instance:
            raise TypeError(f"{repr(typ)} must not be parametrized")

        # validate type is not already registered
        if type(typ) in self.instances:
            previous = self.instances[type(typ)]
            raise RuntimeError(
                f"{type(typ)} is already registered to {repr(previous)}"
            )

        # validate name is unique
        existing = self.names.get(typ.name, None)
        if existing is None:
            self.names[typ.name] = typ
        else:
            implementations = self.implementations.get(type(existing), {})
            if type(typ) not in implementations.values():
                raise TypeError(
                    f"{repr(typ)} name must be unique: '{typ.name}' is "
                    f"currently registered to {repr(existing)}"
                )

        self.instances[type(typ)] = typ
        self.update_hash()

    def remove(self, typ: type_specifier) -> None:
        """Remove a type from the registry.

        Parameters
        ----------
        typ : type_specifier
            A type to remove.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        Raises
        ------
        TypeError
            If the type is composite.
        KeyError
            If the type is not in the registry.

        See Also
        --------
        TypeRegistry.add : Add a type to the registry.

        Notes
        -----
        This method also removes all aliases associated with the removed type
        and automatically excludes it from any subtypes/implementations it
        may be linked to.

        Examples
        --------
        .. doctest::

            >>> class CustomType(pdcast.ScalarType):
            ...     name = "foo"
            ...     aliases = {"bar"}

            >>> pdcast.registry.add(CustomType)
            >>> CustomType in pdcast.registry
            True
            >>> pdcast.resolve_type("bar")
            CustomType()
            >>> pdcast.registry.remove(CustomType)
            >>> CustomType in pdcast.registry
            False
            >>> pdcast.resolve_type("bar")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'bar'
        """
        from pdcast.resolve import resolve_type

        typ = resolve_type(typ)
        if isinstance(typ, CompositeType):
            raise TypeError(f"type must not be composite: {typ}")

        del self.instances[type(typ)]
        typ.aliases.clear()
        if typ in self.names.values():
            del self.names[typ.name]

        # recur for each of the instance's children
        for typ in typ.subtypes:
            self.remove(typ)
        for backend, typ in getattr(typ, "implementations", {}).items():
            if backend is not None:
                self.remove(typ)

        self.update_hash()

    #####################
    ####    STATE    ####
    #####################

    @property
    def hash(self):
        """A hash representing the current state of the ``pdcast`` type system.

        Examples
        --------
        This is updated whenever a new type is
        :meth:`added <pdcast.TypeRegistry.add>` or
        :meth:`removed <pdcast.TypeRegistry.remove>` from the registry, as well
        as whenever a registered type :meth:`gains <pdcast.AliasManager.add>`
        or :meth:`loses <pdcast.AliasManager.remove>` an alias.

        .. doctest::

            >>> hash = pdcast.registry.hash
            >>> pdcast.IntegerType.aliases.add("foo")
            >>> hash == pdcast.registry.hash
            False

        .. testcleanup::

            pdcast.IntegerType.aliases.remove("foo")
        """
        return self._hash

    def flush(self):
        """Reset the registry's current hash, invalidating every
        :class:`CacheValue <pdcast.CacheValue>`.

        Examples
        --------
        This will force every property that depends on a
        :class:`CacheValue <pdcast.CacheValue>` to be recomputed the next time
        it is requested.

        .. doctest::

            >>> aliases = pdcast.registry.aliases
            >>> pdcast.registry.flush()
            >>> aliases is pdcast.registry.aliases
            False
        """
        self._hash += 1

    #########################
    ####    ACCESSORS    ####
    #########################

    @property
    def roots(self):
        """A :class:`CompositeType <pdcast.CompositeType>` containing the root
        nodes for every registered hierarchy.
        """
        if not self._roots:
            is_root = lambda typ: getattr(typ, "is_root", False)
            generic = lambda typ: (
                getattr(typ, "backend", NotImplemented) is None
            )
            result = CompositeType(
                typ for typ in self if is_root(typ) and generic(typ)
            )
            self._roots = CacheValue(result)

        return self._roots.value

    @property
    def leaves(self):
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        leaf nodes for every registered hierarchy.
        """
        if not self._leaves:
            is_leaf = lambda typ: getattr(typ, "is_leaf", False)
            result = CompositeType(typ for typ in self if is_leaf(typ))
            self._leaves = CacheValue(result)

        return self._leaves.value

    @property
    def families(self):
        """A read-only dictionary mapping backend specifiers to all their
        concrete implementations.
        """
        if not self._families:
            result = {}
            for typ in self:
                if not hasattr(typ, "backend"):
                    continue
                result.setdefault(typ.backend, CompositeType()).add(typ)

            self._families = CacheValue(MappingProxyType(result))

        return self._families.value

    @property
    def decorators(self):
        """A :class:`CompositeType` containing all the currently-registered
        :class:`DecoratorTypes <pdcast.DecoratorType>`.
        """
        if not self._decorators:
            result = CompositeType(
                typ for typ in self if isinstance(typ, DecoratorType)
            )
            self._decorators = CacheValue(result)

        return self._decorators.value

    @property
    def abstract(self):
        """A :class:`CompositeType` containing all the currently-registered
        :class:`AbstractTypes <pdcast.AbstractType>`.
        """
        if not self._abstract:
            result = CompositeType(
                typ for typ in self if isinstance(typ, AbstractType)
            )
            self._abstract = CacheValue(result)

        return self._abstract.value

    #####################
    ####    REGEX    ####
    #####################

    @property
    def aliases(self):
        """An up-to-date mapping of every alias to its corresponding type.

        Returns
        -------
        MappingProxyType
            A read-only dictionary with aliases as keys and registered type
            instances as values.  These are used directly by
            :func:`detect_type() <pdcast.detect_type>` and
            :func:`resolve_type() <pdcast.resolve_type>` to map specifiers to
            their respective instances.

        See Also
        --------
        TypeRegistry.regex : A regular expression to match strings in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.
        TypeRegistry.resolvable : A regular expression that matches any number
            of individual type specifiers.

        Examples
        --------
        .. doctest::

            >>> aliases = pdcast.registry.aliases
            >>> aliases[int]
            PythonIntegerType()
            >>> aliases["bool"]
            BooleanType()
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
    def regex(self):
        """A compiled regular expression that matches a single specifier in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        re.Pattern
            A compiled regular expression from the alternate Python
            `regex <https://pypi.org/project/regex/>`_ engine.

        See Also
        --------
        TypeRegistry.aliases : A complete map of every alias to its
            corresponding type.
        TypeRegistry.resolvable : A regular expression that matches any number
            of these expressions.

        Notes
        -----
        This expression uses PERL-style `recursive regular expressions
        <https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_
        to match nested type specifiers.  This is enabled by the alternate
        Python `regex <https://pypi.org/project/regex/>`_ engine, which is
        PERL-compatible.  It is otherwise equivalent to the base Python
        :mod:`re <python:re>` package.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.resolvable.match("datetime[pandas, US/Pacific]")
            <regex.Match object; span=(0, 28), match='datetime[pandas, US/Pacific]'>
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
    def resolvable(self):
        """A compiled regular expression that matches any number of specifiers
        in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        re.Pattern
            A compiled regular expression from the alternate Python
            `regex <https://pypi.org/project/regex/>`_ engine.

        See Also
        --------
        TypeRegistry.aliases : A complete map of every alias to its
            corresponding type.
        TypeRegistry.regex : A regular expression to match individual
            specifiers.

        Notes
        -----
        This expression uses PERL-style `recursive regular expressions
        <https://perldoc.perl.org/perlre#(?PARNO)-(?-PARNO)-(?+PARNO)-(?R)-(?0)>`_
        to match nested type specifiers.  This is enabled by the alternate
        Python `regex <https://pypi.org/project/regex/>`_ engine, which is
        PERL-compatible.  It is otherwise equivalent to the base Python
        :mod:`re <python:re>` package.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.resolvable.match("int, float, complex")
            <regex.Match object; span=(0, 19), match='int, float, complex'>
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

    #############################
    ####    RELATIONSHIPS    ####
    #############################

    def get_default(self, typ: AbstractType) -> ScalarType:
        """Get the default concretion for an
        :class:`AbstractType <pdcast.AbstractType>`.

        Parameters
        ----------
        typ : AbstractType
            An abstract, hierarchical type to check for.

        Returns
        -------
        ScalarType
            A concrete type that ``typ`` defaults to.

        Raises
        ------
        TypeError
            If the type is not an instance of
            :class:`AbstractType <pdcast.AbstractType>`.
        NotImplementedError
            If the type has no default implementation.

        See Also
        --------
        TypeRegistry.get_subtypes : Get a set of subtypes that the type can be
            delegated to.
        TypeRegistry.get_implementations : Get a map of implementations that
            the type can be delegated to.

        Notes
        -----
        This method is called to delegate the behavior of an
        :class:`AbstractType <pdcast.AbstractType>` to a particular subtype or
        implementation.  This allows the type to be used interchangeably with
        its default.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.get_default(pdcast.BooleanType)
            NumpyBooleanType()
        """
        default = self.defaults.get(type(typ), None)
        default = self.instances.get(default, None)
        if default is None:
            raise NotImplementedError(
                f"{repr(typ)} has no default implementation"
            )
        return default

    def get_supertype(self, typ: ScalarType) -> AbstractType:
        """Get a type's :attr:`supertype <pdcast.ScalarType.supertype>` if it
        is registered.

        Parameters
        ----------
        typ : ScalarType
            A concrete :class:`ScalarType <pdcast.ScalarType>` to check for.

        Returns
        -------
        AbstractType | None
            An abstract supertype that the
            :class:`ScalarType <pdcast.ScalarType>` is registered to, or
            :data:`None <python:None>` if none exists.

        Raises
        ------
        TypeError
            If the type is not an instance of
            :class:`ScalarType <pdcast.ScalarType>`.

        See Also
        --------
        TypeRegistry.get_subtypes : Get the set of subtypes that are registered
            to a supertype.

        Notes
        -----
        This method is called to implement
        :class:`ScalarType.supertype <pdcast.ScalarType.supertype>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.get_supertype(pdcast.Float32Type)
            FloatType()
        """
        result = self.supertypes.get(type(typ), None)
        return self.instances.get(result, None)

    def get_subtypes(self, typ: AbstractType) -> CompositeType:
        """Get all the registered :attr:`subtypes <pdcast.ScalarType.subtypes>`
        associated with an :class:`AbstractType <pdcast.AbstractType>`.

        Parameters
        ----------
        typ : AbstractType
            An abstract, hierarchical type to check for.

        Returns
        -------
        CompositeType
            A :class:`CompositeType <pdcast.CompositeType>` containing all the
            subtypes that the type is registered to.

        Raises
        ------
        TypeError
            If the type is not an instance of
            :class:`AbstractType <pdcast.AbstractType>`.

        See Also
        --------
        TypeRegistry.get_supertype : Get the supertype associated with a
            subtype.

        Notes
        -----
        This method is called to implement
        :class:`ScalarType.subtypes <pdcast.ScalarType.subtypes>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.get_subtypes(pdcast.FloatType)   # doctest: +SKIP
            CompositeType({float16, float32, float64, float80})
        """
        result = set()
        
        candidates = self.subtypes.get(type(typ), set())
        for subtype in candidates:
            instance = self.instances.get(subtype, None)
            if instance is None:
                continue
            result.add(instance)

        return CompositeType(result)

    def get_generic(self, typ: ScalarType) -> AbstractType:
        """Get a type's :attr:`generic <pdcast.ScalarType.generic>` if it is
        registered.

        Parameters
        ----------
        typ : ScalarType
            A concrete :class:`ScalarType <pdcast.ScalarType>` to check for.

        Returns
        -------
        AbstractType | None
            An abstract generic type that the
            :class:`ScalarType <pdcast.ScalarType>` is registered to, or
            :data:`None <python:None>` if none exists.

        Raises
        ------
        TypeError
            If the type is not an instance of
            :class:`ScalarType <pdcast.ScalarType>`.

        See Also
        --------
        TypeRegistry.get_implementations : Get a map of implementations that
            are registered to a generic.

        Notes
        -----
        This method is called to implement
        :class:`ScalarType.generic <pdcast.ScalarType.generic>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.get_generic(pdcast.NumpyFloat32Type)
            Float32Type()
        """
        result = self.generics.get(type(typ), None)
        if result is not None:
            result = self.instances.get(result, None)
        return result

    def get_implementations(self, typ: AbstractType) -> MappingProxyType:
        """Get a map of backend specifiers to the registered implementations
        associated for an :class:`AbstractType <pdcast.AbstractType>`.

        Parameters
        ----------
        typ : AbstractType
            An abstract, hierarchical type to check for.

        Returns
        -------
        MappingProxyType
            A read-only mapping backend strings to the registered
            implementations for the given type.

        Raises
        ------
        TypeError
            If the type is not an instance of
            :class:`AbstractType <pdcast.AbstractType>`.

        See Also
        --------
        TypeRegistry.get_generic : Get the generic type that an implementation
            is registered.

        Notes
        -----
        This method is called to implement
        :class:`ScalarType.implementations <pdcast.ScalarType.implementations>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry.get_implementations(pdcast.Float32Type)
            mappingproxy({'numpy': NumpyFloat32Type()})
        """
        result = {}
        candidates = self.implementations.get(type(typ), {})
        for backend, implementation in candidates.items():
            instance = self.instances.get(implementation, None)
            if instance is None:
                continue
            result[backend] = instance

        return MappingProxyType(result)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def decorator_priority(self):
        """A list describing the order of nested
        :class:`DecoratorTypes <pdcast.DecoratorType>`.

        Returns
        -------
        PriorityList
            A read-only list whose elements can be rearranged to change the
            desired order of nested decorators.

        Notes
        -----
        :class:`PriorityLists <pdcast.PriorityList>` behave like immutable
        sequences that cannot be appended to or removed from once created.

        .. currentmodule:: pdcast

        .. autosummary::
            :toctree: ../generated/

            PriorityList
            PriorityList.index
            PriorityList.move_up
            PriorityList.move_down
            PriorityList.move

        Examples
        --------
        This list dictates the order of nested decorators when constructed
        manually (through
        :meth:`DecoratorType.__call__ <pdcast.DecoratorType.__call__>`) or as
        supplied to :func:`resolve_type() <pdcast.resolve_type>`.

        .. doctest::

            >>> pdcast.registry.decorator_priority
            PriorityList([<class 'pdcast.types.sparse.SparseType'>, <class 'pdcast.types.categorical.CategoricalType'>])
            >>> pdcast.resolve_type("sparse[categorical]")
            SparseType(wrapped=CategoricalType(wrapped=None, levels=None), fill_value=None)
            >>> pdcast.resolve_type("categorical[sparse]")
            SparseType(wrapped=CategoricalType(wrapped=None, levels=None), fill_value=None)

        Rearranging its elements changes this order for any newly-constructed
        type.

        .. doctest::

            >>> pdcast.registry.decorator_priority.move(pdcast.SparseType, -1)
            >>> pdcast.registry.decorator_priority
            PriorityList([<class 'pdcast.types.categorical.CategoricalType'>, <class 'pdcast.types.sparse.SparseType'>])
            >>> pdcast.resolve_type("sparse[categorical]")
            CategoricalType(wrapped=SparseType(wrapped=None, fill_value=None), levels=None)
            >>> pdcast.resolve_type("categorical[sparse]")
            CategoricalType(wrapped=SparseType(wrapped=None, fill_value=None), levels=None)
        """
        return self._decorator_priority

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

    def __iter__(self):
        """Iterate through the registered types.

        Examples
        --------
        .. doctest::

            >>> len([typ for typ in pdcast.registry])
            74
        """
        return iter(self.instances.values())

    def __len__(self) -> int:
        """Get the total number of registered types.

        Examples
        --------
        .. doctest::

            >>> len(pdcast.registry)
            74
        """
        return len(self.instances)

    def __contains__(self, val) -> bool:
        """Check if a type is in the registry.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType in pdcast.registry
            True
        """
        if not isinstance(val, type):
            val = type(val)
        return val in self.instances

    def __getitem__(self, val) -> VectorType:
        """Get the base instance for a given type if it is registered.

        Examples
        --------
        .. doctest::

            >>> pdcast.registry[pdcast.BooleanType]
            BooleanType()
        """
        if not isinstance(val, type):
            val = type(val)
        return self.instances[val]

    def __str__(self) -> str:
        return str(set(self.instances.values()))

    def __repr__(self) -> str:
        return f"{type(self).__name__}{set(self.instances.values())}"


cdef class AliasManager:
    """A set-like interface that holds :attr:`aliases <pdcast.Type.aliases>`
    for a given :class:`Type <pdcast.Type>`.

    These objects are attached to every :class:`Type <pdcast.Type>` that
    ``pdcast`` generates, enabling users to modify the behavior of
    :func:`detect_type() <pdcast.detect_type>` and
    :func:`resolve_type() <pdcast.resolve_type>` at runtime.
    """

    def __init__(self, Type instance):
        self.instance = instance
        self.aliases = set()

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(self, alias: type_specifier, overwrite: bool = False) -> None:
        """Register a type specifier as an alias of the managed
        :class:`Type <pdcast.Type>`.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to register.
        overwrite : bool, default False
            Indicates whether to overwrite existing aliases (``True``) or
            raise an error (``False``) in the event of a conflict.

        Raises
        ------
        TypeError
            If the alias is not of a recognizable type.
        ValueError
            If ``overwrite=False`` and the alias conflicts with another type.

        Notes
        -----
        See the :ref:`API docs <Type.aliases>` for more information on how
        aliases work.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.BooleanType.aliases.add("foo")
            >>> pdcast.resolve_type("foo")
            BooleanType()

        .. testcleanup::

            pdcast.BooleanType.aliases.remove("foo")
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

        # register aliases with global registry
        if not self:
            self.pin()

        self.aliases.add(alias)
        registry.flush()  # rebuild regex patterns

    def remove(self, alias: type_specifier) -> None:
        """Remove an alias from the managed type.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to remove.

        Raises
        ------
        TypeError
            If the alias is not of a recognizable type.
        KeyError
            If the alias is not a member of the set.

        Notes
        -----
        See the :ref:`API docs <Type.aliases>` for more information on how
        aliases work.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.resolve_type("boolean")
            BooleanType()
            >>> pdcast.BooleanType.aliases.remove("boolean")
            >>> pdcast.resolve_type("boolean")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'boolean'
        """
        alias = self.normalize_specifier(alias)
        self.aliases.remove(alias)

        # remove aliases from global registry
        if not self:
            self.unpin()

        Type.registry.flush()  # rebuild regex patterns

    def discard(self, alias: type_specifier) -> None:
        """Remove an alias from the managed type if it is present.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to remove.

        Raises
        ------
        TypeError
            If the alias is not of a recognizable type.

        Notes
        -----
        See the :ref:`API docs <Type.aliases>` for more information on how
        aliases work.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.BooleanType.aliases.discard("boolean")
            >>> pdcast.BooleanType.aliases    # doctest: +SKIP
            AliasManager({'bool', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.BooleanType.aliases.discard("foo")
            >>> pdcast.BooleanType.aliases    # doctest: +SKIP
            AliasManager({'bool', 'bool_', 'bool8', 'b1', '?'})
        """
        try:
            self.remove(alias)
        except KeyError:
            pass

    def pop(self) -> type_specifier:
        """Pop an alias from the set.

        Returns
        -------
        type_specifier
            A random alias from the set.

        Raises
        ------
        KeyError
            If the set is empty.

        Notes
        -----
        See the :ref:`API docs <Type.aliases>` for more information on how
        aliases work.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.BooleanType.aliases.pop()   # doctest: +SKIP
            "bool"
            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'boolean', 'bool_', 'bool8', 'b1', '?'})
        """
        value = self.aliases.pop()

        # remove aliases from global registry
        if not self:
            self.unpin()

        Type.registry.flush()  # rebuild regex patterns
        return value

    def clear(self) -> None:
        """Remove every alias from the managed type.

        Notes
        -----
        See the :ref:`API docs <Type.aliases>` for more information on how
        aliases work.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.BooleanType.aliases.clear()
            >>> pdcast.BooleanType.aliases
            AliasManager(set())
        """
        # remove aliases from global registry
        if self:
            self.unpin()

        self.aliases.clear()
        Type.registry.flush()  # rebuild regex patterns

    ##############################
    ####    SET OPERATIONS    ####
    ##############################

    def __or__(self, other: set) -> set:
        """Set-like union operator."""
        return self.aliases | other

    def __and__(self, other: set) -> set:
        """Set-like intersection operator."""
        return self.aliases & other

    def __sub__(self, other: set) -> set:
        """Set-like difference operator."""
        return self.aliases - other

    def __xor__(self, other: set) -> set:
        """Set-like symmetric difference operator."""
        return self.aliases ^ other

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

    def __str__(self):
        return str(self.aliases)

    def __repr__(self):
        return f"{type(self).__name__}({self.aliases})"


cdef class Type:
    """Base class for all ``pdcast`` type objects.

    Notes
    -----
    This does relatively little on its own, mainly serving to anchor
    inheritance and distribute the global
    :class:`TypeRegistry <pdcast.TypeRegistry>` to all ``pdcast`` type objects.
    It also provides a unified interface for managing their
    :ref:`aliases <pdcast.Type.aliases>` and customizing their creation via
    the :func:`detect_type() <pdcast.detect_type>` and
    :func:`resolve_type <pdcast.resolve_type>` constructors.
    """

    registry: TypeRegistry = TypeRegistry()

    def __init__(self):
        self._aliases = AliasManager(self)

    #######################
    ####    ALIASES    ####
    #######################

    @property
    def aliases(self):
        """A set of unique aliases for this type.
    
        Aliases are used by :func:`detect_type` and :func:`resolve_type` to map
        specifiers to their corresponding types.

        Returns
        -------
        AliasManager
            A set-like container holding all the aliases that are associated
            with this type.

        Notes
        -----
        :class:`AliasManagers <pdcast.AliasManager>` behave like
        :class:`sets <python:set>` with the following interface:

        .. autosummary::
            :toctree: ../generated/

            AliasManager
            AliasManager.add
            AliasManager.remove
            AliasManager.discard
            AliasManager.pop
            AliasManager.clear

        They can accept specifiers of a variety of kinds, including:

            *   Strings, which are interpreted according to the
                :ref:`type specification mini-language <resolve_type.mini_language>`.
            *   Numpy/pandas :class:`dtype <numpy.dtype>`\ /
                :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
                objects, which are translated directly into the ``pdcast`` type
                system.
            *   Python class objects, which are used for vectorized inference.

        For more information on how these are used, see the
        :ref:`API docs <Type.constructors>`.

        Examples
        --------
        .. doctest::

            >>> pdcast.BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> pdcast.resolve_type("?")
            BooleanType()
            >>> pdcast.BooleanType.aliases.add("foo")
            >>> pdcast.resolve_type("foo")
            BooleanType()

        .. testcleanup::

            pdcast.BooleanType.aliases.remove("foo")
        """
        return self._aliases

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, *args: str) -> Type:
        """Construct a :class:`Type <pdcast.Type>` from a string in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Parameters
        ----------
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        Type
            An instance of the associated type.
    
        See Also
        --------
        Type.from_dtype : Resolve a :class:`Type <pdcast.Type>` from a
            numpy/pandas :class:`dtype <numpy.dtype>`\ /
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
            object.
        Type.from_scalar : Detect a :class:`Type <pdcast.Type>` from a scalar
            example object.

        Examples
        --------
        This method is automatically called by
        :func:`resolve_type() <pdcast.resolve_type>` whenever it encounters a
        string specifier.

        .. doctest::

            >>> pdcast.resolve_type("float")
            FloatType()
            >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))

        This directly translates to:

        .. doctest::

            >>> pdcast.FloatType.from_string()
            FloatType()
            >>> pdcast.DatetimeType.from_string("pandas", "US/Pacific")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))
        """
        return NotImplementedError(
            f"{type(self).__qualname__} cannot be constructed from a string"
        )

    def from_dtype(self, dtype: dtype_like) -> Type:
        """Construct a :class:`Type` from a numpy/pandas
        :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            A numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.

        Returns
        -------
        Type
            An instance of the associated type.

        See Also
        --------
        Type.from_string : Resolve a :class:`Type <pdcast.Type>` from a string
            in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.
        Type.from_scalar : Detect a :class:`Type <pdcast.Type>` from a scalar
            example object.

        Examples
        --------
        This method is automatically called by
        :func:`resolve_type() <pdcast.resolve_type>` whenever it encounteres
        a numpy/pandas dtype specifier.

        .. doctest::

            >>> import numpy as np
            >>> import pandas as pd

            >>> pdcast.resolve_type(np.dtype("bool"))
            NumpyBooleanType()
            >>> pdcast.resolve_type(pd.Int64Dtype())
            PandasInt64Type()

        This directly translates to:

        .. doctest::

            >>> pdcast.NumpyBooleanType.from_dtype(np.dtype("bool"))
            NumpyBooleanType()
            >>> pdcast.PandasInt64Type.from_dtype(pd.Int64Dtype())
            PandasInt64Type()

        It is also called whenever :func:`detect_type() <pdcast.detect_type>`
        encounters data with an appropriate ``.dtype`` field.

        .. doctest::

            >>> pdcast.detect_type(np.array([True, False, True]))
            NumpyBooleanType()
            >>> pdcast.detect_type(pd.Series([1, 2, 3], dtype=pd.Int64Dtype()))
            PandasInt64Type()

        Which follows the same pattern as above.  This allows
        :func:`detect_type() <pdcast.detect_type>` to do *O(1)* inference on
        properly-labeled, numpy-compatible data.
        """
        return NotImplementedError(
            f"{type(self).__qualname__} cannot be constructed from a "
            f"numpy/pandas dtype object"
        )

    def from_scalar(self, example: Any) -> Type:
        """Construct a :class:`Type` from scalar example data.

        Parameters
        ----------
        example : Any
            A scalar example of this type (e.g. ``1``, ``42.0``, ``"foo"``,
            etc.).

        Returns
        -------
        Type
            An instance of the associated type.

        See Also
        --------
        Type.from_string : Resolve a :class:`Type <pdcast.Type>` from a string
            in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.
        Type.from_dtype : Resolve a :class:`Type <pdcast.Type>` from a
            numpy/pandas :class:`dtype <numpy.dtype>`\ /
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
            object.

        Notes
        -----
        In order for this method to be called, the output of
        :class:`type() <python:type>` on the example must be registered as one
        of this type's :attr:`aliases <Type.aliases>`.

        Examples
        --------
        This method is automatically called by
        :func:`detect_type() <pdcast.detect_type>` whenever it encounters data
        that lacks a proper ``.dtype`` field.  In this case, we iterate
        over the input data, calling this method at every index.

        .. doctest::

            >>> import pandas as pd

            >>> pdcast.detect_type([True, False, True])
            PythonBooleanType()
            >>> pdcast.detect_type(pd.Series([1, 2, 3], dtype=object))
            PythonIntegerType()

        At a high level, this translates to:

        .. doctest::

            >>> {pdcast.PythonBooleanType.from_scalar(x) for x in [True, False, True]}.pop()
            PythonBooleanType()
            >>> {pdcast.PythonIntegerType.from_scalar(x) for x in [1, 2, 3]}.pop()
            PythonIntegerType()

        This can be naturally extended to support data of mixed type, yielding
        a :class:`composite <pdcast.CompositeType>` result.

        .. doctest::

            >>> mixed = pdcast.detect_type([False, 1, 2.0])
            >>> mixed   # doctest: +SKIP
            CompositeType({bool[python], int[python], float64[python]})

        The result records the observed type at every index:

        .. doctest::

            >>> mixed.index
            array([PythonBooleanType(), PythonIntegerType(), PythonFloatType()],
                  dtype=object)
        """
        raise NotImplementedError(
            f"{type(self).__qualname__} cannot be constructed from example "
            f"data"
        )

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
        typecheck : :func:`isinstance() <python:isinstance>`-like hierarchical
            checks within the ``pdcast`` type system.
        dispatch : Multiple dispatch based on argument membership.

        Notes
        -----
        This method also controls the behavior of the ``in`` keyword on type
        objects.

        Examples
        --------
        .. doctest::

            >>> pdcast.resolve_type("int").contains("int32")
            True
            >>> pdcast.resolve_type("datetime").contains("M8[5ns]")
            True
            >>> pdcast.resolve_type("sparse").contains("sparse[bool[numpy]]")
            True
            >>> pdcast.resolve_type("int, float, complex").contains("float16")
            True
            >>> pdcast.resolve_type("complex").contains(["complex64", "complex128"])
            True

        Using the ``in`` keyword reverses its behavior:

        .. doctest::

            >>> "int32" in pdcast.resolve_type("int")
            True
            >>> "M8[5ns]" in pdcast.resolve_type("datetime")
            True
            >>> "sparse[bool[numpy]]" in pdcast.resolve_type("sparse")
            True
            >>> "float16" in pdcast.resolve_type("int, float, complex")
            True
            >>> ["complex64", "complex128"] in pdcast.resolve_type("complex")
            True

        :func:`typecheck() <pdcast.typecheck>` allows this method to be called
        on example data.

        .. doctest::

            >>> pdcast.typecheck([1, 2, 3], "int")
            True
            >>> pdcast.typecheck([1, 2.0, 3+0j], "int, float, complex")
            True

        Which is semantically equivalent to:

        .. doctest::

            >>> pdcast.resolve_type("int").contains(pdcast.detect_type([1, 2, 3]))
            True
            >>> pdcast.resolve_type("int, float, complex").contains(pdcast.detect_type([1, 2.0, 3+0j]))
            True
        """
        raise NotImplementedError(
            f"{repr(self)} does not support hierarchical membership checks"
        )

    def __contains__(self, other: type_specifier) -> bool:
        """Implement the ``in`` keyword for type objects.

        This is semantically equivalent to calling
        :meth:`self.contains(other) <pdcast.Type.contains>`.
        """
        return self.contains(other)


#######################
####    PRIVATE    ####
#######################


cdef class CacheValue:
    """A simple struct to hold values that are tied to the current state of the
    ``pdcast`` type system.

    Attributes
    ----------
    value : Any
        The cached value.
    hash : int
        The observed :class:`TypeRegistry <pdcast.TypeRegistry>` hash at the
        time this value was created.

    Methods
    -------
    __bool__()
        Check whether :attr:`hash <pdcast.CacheValue.hash>` matches the current
        registry hash.

    Examples
    --------
    .. doctest::

        >>> foo = pdcast.CacheValue(1)

        >>> def compute():
        ...     if foo:
        ...         print("foo is valid")
        ...     else:
        ...         print("foo is invalid")

        >>> compute()
        foo is valid
        >>> pdcast.registry.flush()
        >>> compute()
        foo is invalid
    """

    def __init__(self, value: Any):
        self.value = value
        self.hash = Type.registry.hash

    def __bool__(self) -> bool:
        """Indicates whether a cached registry value is out of date."""
        return self.hash == Type.registry.hash


cdef class PriorityList:
    """A doubly-linked list whose elements can be rearranged to represent a
    a precedence order during sort operations.

    The list is read-only when accessed from Python.

    Examples
    --------
    .. doctest::

        >>> foo = pdcast.PriorityList([1, 2, 3])
        >>> foo
        PriorityList([1, 2, 3])
        >>> foo.index(2)
        1
        >>> foo.move_up(2)
        >>> foo
        PriorityList([2, 1, 3])
        >>> foo.move_down(2)
        >>> foo
        PriorityList([1, 2, 3])
        >>> foo.move(2, -1)
        >>> foo
        PriorityList([1, 3, 2])
    """

    def __init__(self, items: Iterable = None):
        self.head = None
        self.tail = None
        self.items = {}
        if items is not None:
            for item in items:
                self.append(item)

    cdef void append(self, object item):
        """Add an item to the list.

        This method is inaccessible from Python.
        """
        node = PriorityNode(item)
        self.items[item] = node
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.tail.next = node
            node.prev = self.tail
            self.tail = node

    cdef void remove(self, object item):
        """Remove an item from the list.

        This method is inaccessible from Python.
        """
        node = self.items[item]

        if node.prev is None:
            self.head = node.next
        else:
            node.prev.next = node.prev

        if node.next is None:
            self.tail = node.prev
        else:
            node.next.prev = node.next

        del self.items[item]

    cdef int normalize_index(self, int index):
        """Allow negative indexing and enforcing boundschecking."""
        if index < 0:
            index = index + len(self)

        if not 0 <= index < len(self):
            raise IndexError("list index out of range")

        return index

    def index(self, item: Any) -> int:
        """Get the index of an item within the list.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.index(2)
            1
        """
        if isinstance(item, VectorType):
            item = type(item)

        for idx, typ in enumerate(self):
            if item == typ:
                return idx

        raise ValueError(f"{repr(item)} is not contained in the list")

    def move_up(self, item: Any) -> None:
        """Move an item up one level in priority.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move_up(2)
            >>> foo
            PriorityList([2, 1, 3])
        """
        if isinstance(item, VectorType):
            item = type(item)

        node = self.items[item]
        prev = node.prev
        if prev is not None:
            node.prev = prev.prev

            if node.prev is None:
                self.head = node
            else:
                node.prev.next = node

            if node.next is None:
                self.tail = prev
            else:
                node.next.prev = prev

            prev.next = node.next
            node.next = prev
            prev.prev = node

    def move_down(self, item: Any) -> None:
        """Move an item down one level in priority.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move_down(2)
            >>> foo
            PriorityList([1, 3, 2])
        """
        if isinstance(item, VectorType):
            item = type(item)

        node = self.items[item]
        next = node.next
        if next is not None:
            node.next = next.next

            if node.next is None:
                self.tail = node
            else:
                node.next.prev = node

            if node.prev is None:
                self.head = next
            else:
                node.prev.next = next

            next.prev = node.prev
            node.prev = next
            next.next = node

    def move(self, item: Any, index: int) -> None:
        """Move an item to the specified index.

        Notes
        -----
        This method can accept negative indices.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move(2, -1)
            >>> foo
            PriorityList([1, 3, 2])
        """
        if isinstance(item, VectorType):
            item = type(item)

        curr_index = self.index(item)
        index = self.normalize_index(index)

        node = self.items[item]
        if index < curr_index:
            for _ in range(curr_index - index):
                self.move_up(item)
        else:
            for _ in range(index - curr_index):
                self.move_down(item)

    def __len__(self) -> int:
        """Get the total number of items in the list."""
        return len(self.items)

    def __iter__(self):
        """Iterate through the list items in order."""
        node = self.head
        while node is not None:
            yield node.item
            node = node.next

    def __reversed__(self):
        """Iterate through the list in reverse order."""
        node = self.tail
        while node is not None:
            yield node.item
            node = node.prev

    def __bool__(self) -> bool:
        """Treat empty lists as boolean False."""
        return bool(self.items)

    def __contains__(self, item: type | VectorType) -> bool:
        """Check if the item is contained in the list."""
        if isinstance(item, VectorType):
            item = type(item)

        return item in self.items

    def __getitem__(self, key):
        """Index into the list using standard syntax."""
        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(len(self))
            return PriorityList(self[i] for i in range(start, stop, step))

        key = self.normalize_index(key)

        # count from nearest end
        if key < len(self) // 2:
            node = self.head
            for _ in range(key):
                node = node.next
        else:
            node = self.tail
            for _ in range(len(self) - key - 1):
                node = node.prev

        return node.item

    def __str__(self):
        return str(list(self))

    def __repr__(self):
        return f"{type(self).__name__}({list(self)})"


cdef class PriorityNode:
    """A node containing an individual element of a PriorityList."""

    def __init__(self, object item):
        self.item = item
        self.next = None
        self.prev = None


cdef void add_to_decorator_priority(type typ):
    """C-level helper function to add a decorator type to the priority list."""
    cdef PriorityList prio = Type.registry.decorator_priority

    prio.append(typ)  # this can't be done from normal Python
