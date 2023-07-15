"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any, Callable, Iterable, Iterator, Mapping

import numpy as np
import pandas as pd
from pdcast.util.type_hints import array_like, dtype_like, type_specifier

from .composite cimport CompositeType
from .vector cimport VectorType
from .decorator cimport DecoratorType
from .scalar cimport AbstractType, ScalarType


######################
####    PUBLIC    ####
######################


def register(
    class_: type = None,
    *,
    cond: bool = True
) -> Callable[[type], VectorType | type]:
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
    def register_decorator(cls: type) -> VectorType | type:
        """Add the type to the registry and instantiate it."""
        if not issubclass(cls, VectorType):
            raise TypeError(
                f"@register can only be applied to VectorType subclasses, not "
                f"{cls}"
            )

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

    See Also
    --------
    register : Add a type to this registry as a class decorator.

    Notes
    -----
    This object encodes all the types that are currently registered with the
    ``pdcast`` type system.  It is responsible for caching base
    (unparametrized) instances for each type, as well as maintaining the links
    between them and controlling their creation through the
    :func:`detect_type() <pdcast.detect_type>` and
    :func:`resolve_type() <pdcast.resolve_type>` constructors.
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

        self._priority = PrioritySet()
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

            >>> class CustomType(ScalarType):
            ...     name = "foo"
            ...     aliases = {"bar"}

            >>> registry.add(CustomType)
            >>> CustomType in registry
            True
            >>> resolve_type("bar")
            CustomType()

        .. testcleanup::

            pdcast.registry.remove(CustomType)
        """
        # validate type is a subclass of VectorType and instantiate it
        if isinstance(typ, type):
            if not issubclass(typ, VectorType):
                raise TypeError(f"type must be a subclass of VectorType: {typ}")
            typ = typ() if not typ._base_instance else typ._base_instance
        elif not isinstance(typ, VectorType):
            raise TypeError(f"type must be an instance of VectorType: {typ}")

        # validate type attributes
        self.validate_instance(typ)
        self.validate_name(typ)
        if not isinstance(typ, (AbstractType, DecoratorType)):
            self.validate_type_def(typ)
            self.validate_dtype(typ)
            self.validate_itemsize(typ)
            self.validate_min_max(typ)
            self.validate_na_value(typ)

        # ensure aliases are unique and pin them
        pin_aliases(typ.aliases)

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

            >>> class CustomType(ScalarType):
            ...     name = "foo"
            ...     aliases = {"bar"}

            >>> registry.add(CustomType)
            >>> CustomType in registry
            True
            >>> resolve_type("bar")
            CustomType()
            >>> registry.remove(CustomType)
            >>> CustomType in registry
            False
            >>> resolve_type("bar")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'bar'
        """
        from pdcast.resolve import resolve_type

        typ = resolve_type(typ)
        if isinstance(typ, CompositeType):
            raise TypeError(f"type must not be composite: {typ}")

        del self.instances[type(typ)]
        unpin_aliases(typ.aliases)
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

            >>> hash = registry.hash
            >>> IntegerType.aliases.add("foo")
            >>> hash == registry.hash
            False

        .. testcleanup::

            pdcast.IntegerType.aliases.remove("foo")
        """
        return self._hash

    def flush(self) -> None:
        """Reset the registry's current hash, invalidating every
        :class:`CacheValue <pdcast.CacheValue>`.

        Examples
        --------
        This will force every property that depends on a
        :class:`CacheValue <pdcast.CacheValue>` to be recomputed the next time
        it is requested.

        .. doctest::

            >>> aliases = registry.aliases
            >>> registry.flush()
            >>> aliases is registry.aliases
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

            >>> aliases = registry.aliases
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

            >>> registry.resolvable.match("datetime[pandas, US/Pacific]")
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

            >>> registry.resolvable.match("int, float, complex")
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

            >>> registry.get_default(BooleanType)
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

            >>> registry.get_supertype(Float32Type)
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

            >>> registry.get_subtypes(FloatType)   # doctest: +SKIP
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

    def get_generic(self, typ: ScalarType) -> ScalarType:
        """Get a type's :attr:`generic <pdcast.ScalarType.generic>` equivalent.

        Parameters
        ----------
        typ : ScalarType
            A type to check for.

        Returns
        -------
        ScalarType
            The generic equivalent of the type.  If the type is an
            :meth:`implementation <pdcast.AbstractType.implementation>` of
            another type, then this will be a reference to that type.
            Otherwise, it will be a reference to the type itself.

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

            >>> registry.get_generic(NumpyFloat32Type)
            Float32Type()
            >>> registry.get_generic(Float32Type)
            Float32Type()
        """
        result = self.generics.get(type(typ), None)
        result = self.instances.get(result, None)
        if result is None:
            return typ
        return result

    def get_implementations(self, typ: AbstractType) -> Mapping[str, ScalarType]:
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

            >>> registry.get_implementations(Float32Type)
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
    def priority(self):
        """A set of edges ``(A, B)`` where ``A`` is always considered to be
        less than ``B``.

        Returns
        -------
        PrioritySet
            A :class:`set <python:set>`-like object containing pairs of types
            as tuples where the first element is always considered to be less
            than the second.  This behaves just like an ordinary
            :class:`set <python:set>`, except that it can only contain tuples
            of two type objects, and it is not possible to add tuples that
            would violate the overall ordering.

        See Also
        --------
        ScalarType.__lt__ : implement the ``<`` operator for scalar types.
        ScalarType.__gt__ : implement the ``>`` operator for scalar types.
        DecoratorType.__lt__ : implement the ``<`` operator for decorator
            types.
        DecoratorType.__gt__ : implement the ``>`` operator for decorator
            types.

        Notes
        -----
        This attribute contains overrides for the :meth:`< <ScalarType.__lt__>`
        and :meth:`> <ScalarType.__gt__>` operators for
        :class:`ScalarType <pdcast.ScalarType>` and
        :class:`DecoratorType <pdcast.DecoratorType>` objects.  If an edge
        ``(A, B)`` is present in this set, then ``A < B`` will always be
        ``True``, as will ``B > A``.

        Users should never override the operators directly, as doing so can
        cause sorts to become inconsistent.  Instead, they should add edges to
        this set to override the default ordering.

        Examples
        --------
        .. doctest::

            >>> registry.priority
            PrioritySet({...})
            >>> (SparseType, CategoricalType) in registry.priority
            True
            >>> SparseType < CategoricalType
            True
            >>> CategoricalType > SparseType
            True
            >>> CategoricalType < SparseType
            False
        """
        return self._priority

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self._hash = hash(tuple(self.instances))

    cdef void validate_instance(self, typ):
        """Check that a type is not parametrized and not already present in the
        registry.
        """
        # validate instance is not parametrized
        if typ.is_parametrized:
            raise TypeError(f"{repr(typ)} must not be parametrized")

        # validate type is not already registered
        if type(typ) in self.instances:
            previous = self.instances[type(typ)]
            raise RuntimeError(
                f"{type(typ)} is already registered to {repr(previous)}"
            )

    cdef void validate_name(self, typ):
        """Ensure that a type's name is unique.
        """
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

    cdef void validate_type_def(self, typ):
        """Ensure that a type's type_def is a valid class object.
        """
        # validate type_def is a class object
        if not isinstance(typ.type_def, type):
            raise TypeError(
                f"{repr(typ)} type_def must be a class object: "
                f"{repr(typ.type_def)}"
            )

    cdef void validate_dtype(self, typ):
        """Ensure that a type's dtype is a valid numpy/pandas dtype object.
        """
        # validate dtype is a numpy/pandas dtype object
        if not isinstance(typ.dtype, dtype_like):
            raise TypeError(
                f"{repr(typ)} dtype must be a numpy/pandas dtype object: "
                f"{repr(typ.dtype)}"
            )

    cdef void validate_itemsize(self, typ):
        """Ensure that a type's itemsize is a positive integer or infinity.
        """
        # validate itemsize > 0 or inf
        if (
            typ.itemsize <= 0 or
            typ.itemsize != np.inf and not isinstance(typ.itemsize, int)
        ):
            raise TypeError(
                f"{repr(typ)} itemsize must be a positive integer or infinity: "
                f"{repr(typ.itemsize)}"
            )

    cdef void validate_min_max(self, typ):
        """Ensure that a type's min/max are integers or infinity.
        """
        # validate min/max are integers or inf
        if typ.min != -np.inf and not isinstance(typ.min, int):
            raise TypeError(
                f"{repr(typ)} min must be an integer or negative infinity: "
                f"{repr(typ.min)}"
            )
        if typ.max != np.inf and not isinstance(typ.max, int):
            raise TypeError(
                f"{repr(typ)} max must be an integer or infinity: "
                f"{repr(typ.max)}"
            )

    cdef void validate_na_value(self, typ):
        """Ensure that a type's na_value passes a pd.isna() check.
        """
        # validate na_value passes pd.isna()
        if not pd.isna(typ.na_value):
            raise TypeError(
                f"{repr(typ)} na_value must pass pandas.isna(): "
                f"{repr(typ.na_value)}"
            )

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __iter__(self) -> Iterator[VectorType]:
        """Iterate through the registered types.

        Examples
        --------
        .. doctest::

            >>> len([typ for typ in registry])
            74
        """
        return iter(self.instances.values())

    def __len__(self) -> int:
        """Get the total number of registered types.

        Examples
        --------
        .. doctest::

            >>> len(registry)
            74
        """
        return len(self.instances)

    def __contains__(self, typ: VectorType | type) -> bool:
        """Check if a type is in the registry.

        Examples
        --------
        .. doctest::

            >>> BooleanType in registry
            True
        """
        if not isinstance(typ, type):
            typ = type(typ)
        return typ in self.instances

    def __getitem__(self, typ: VectorType | type) -> VectorType:
        """Get the base instance for a given type if it is registered.

        Examples
        --------
        .. doctest::

            >>> registry[BooleanType]
            BooleanType()
        """
        if not isinstance(typ, type):
            typ = type(typ)
        return self.instances[typ]

    def __str__(self) -> str:
        return str(set(self.instances.values()))

    def __repr__(self) -> str:
        return f"{type(self).__name__}{set(self.instances.values())}"


cdef class Type:
    """Base class for all ``pdcast`` type objects.

    Notes
    -----
    This mainly serves to anchor inheritance and distribute the global
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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> resolve_type("?")
            BooleanType()
            >>> BooleanType.aliases.add("foo")
            >>> resolve_type("foo")
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

            >>> resolve_type("float")
            FloatType()
            >>> resolve_type("datetime[pandas, US/Pacific]")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))

        This directly translates to:

        .. doctest::

            >>> FloatType.from_string()
            FloatType()
            >>> DatetimeType.from_string("pandas", "US/Pacific")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))
        """
        return NotImplementedError(
            f"{type(self).__qualname__} cannot be constructed from a string"
        )

    def from_dtype(
        self,
        dtype: dtype_like,
        array: array_like | None = None
    ) -> Type:
        """Construct a :class:`Type` from a numpy/pandas
        :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            A numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.
        array : array_like | None, default None
            An optional array of values to use for inference.  This is
            supplied by :func:`detect_type() <pdcast.detect_type>` whenever
            an array of the associated type is encountered.  It will always
            have the same dtype as above.

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

            >>> resolve_type(np.dtype("bool"))
            NumpyBooleanType()
            >>> resolve_type(pd.Int64Dtype())
            PandasInt64Type()

        This directly translates to:

        .. doctest::

            >>> NumpyBooleanType.from_dtype(np.dtype("bool"))
            NumpyBooleanType()
            >>> PandasInt64Type.from_dtype(pd.Int64Dtype())
            PandasInt64Type()

        It is also called whenever :func:`detect_type() <pdcast.detect_type>`
        encounters data with an appropriate ``.dtype`` field.

        .. doctest::

            >>> detect_type(np.array([True, False, True]))
            NumpyBooleanType()
            >>> detect_type(pd.Series([1, 2, 3], dtype=pd.Int64Dtype()))
            PandasInt64Type()

        Which follows the same pattern as above.  This allows
        :func:`detect_type() <pdcast.detect_type>` to do *O(1)* inference on
        properly-labeled data.
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

            >>> detect_type([True, False, True])
            PythonBooleanType()
            >>> detect_type(pd.Series([1, 2, 3], dtype=object))
            PythonIntegerType()

        At a high level, this translates to:

        .. doctest::

            >>> {PythonBooleanType.from_scalar(x) for x in [True, False, True]}.pop()
            PythonBooleanType()
            >>> {PythonIntegerType.from_scalar(x) for x in [1, 2, 3]}.pop()
            PythonIntegerType()

        This can be naturally extended to support data of mixed type, yielding
        a :class:`composite <CompositeType>` result.

        .. doctest::

            >>> mixed = detect_type([False, 1, 2.0])
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

            >>> resolve_type("int").contains("int32")
            True
            >>> resolve_type("datetime").contains("M8[5ns]")
            True
            >>> resolve_type("sparse").contains("sparse[bool[numpy]]")
            True
            >>> resolve_type("int, float, complex").contains("float16")
            True
            >>> resolve_type("complex").contains(["complex64", "complex128"])
            True

        Using the ``in`` keyword reverses its behavior:

        .. doctest::

            >>> "int32" in resolve_type("int")
            True
            >>> "M8[5ns]" in resolve_type("datetime")
            True
            >>> "sparse[bool[numpy]]" in resolve_type("sparse")
            True
            >>> "float16" in resolve_type("int, float, complex")
            True
            >>> ["complex64", "complex128"] in resolve_type("complex")
            True

        :func:`typecheck() <pdcast.typecheck>` allows this method to be called
        on example data.

        .. doctest::

            >>> typecheck([1, 2, 3], "int")
            True
            >>> typecheck([1, 2.0, 3+0j], "int, float, complex")
            True

        Which is semantically equivalent to:

        .. doctest::

            >>> resolve_type("int").contains(detect_type([1, 2, 3]))
            True
            >>> resolve_type("int, float, complex").contains(detect_type([1, 2.0, 3+0j]))
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

        >>> foo = CacheValue(1)

        >>> def compute():
        ...     if foo:
        ...         print("foo is valid")
        ...     else:
        ...         print("foo is invalid")

        >>> compute()
        foo is valid
        >>> registry.flush()
        >>> compute()
        foo is invalid
    """

    def __init__(self, value: Any):
        self.value = value
        self.hash = Type.registry.hash

    def __bool__(self) -> bool:
        """Indicates whether a cached registry value is out of date."""
        return self.hash == Type.registry.hash


cdef class AliasManager:
    """A set-like interface that holds :attr:`aliases <pdcast.Type.aliases>`
    for a given :class:`Type <pdcast.Type>`.

    These objects are attached to every :class:`Type <pdcast.Type>` that
    ``pdcast`` generates, enabling users to modify the behavior of
    :func:`detect_type() <pdcast.detect_type>` and
    :func:`resolve_type() <pdcast.resolve_type>` at runtime.
    """

    def __init__(self, instance: Type):
        self.instance = instance
        self.aliases = set()
        self.pinned = False

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(
        self,
        alias: type_specifier,
        overwrite: bool = False,
        pin: bool = True
    ) -> None:
        """Register a type specifier as an alias of the managed
        :class:`Type <pdcast.Type>`.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to register.
        overwrite : bool, default False
            Indicates whether to overwrite existing aliases (``True``) or
            raise an error (``False``) in the event of a conflict.
        pin : bool, default True
            Indicates whether to pin the aliases to the global
            :class:`TypeRegistry <pdcast.TypeRegistry>` and make them available
            to :func:`detect_type() <pdcast.detect_type>` and
            :func:`resolve_type() <pdcast.resolve_type>`.  If this is
            ``False``, then the alias will be added to the manager without
            changing its pinned/unpinned status.  In general, this is only used
            during initialization.

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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> BooleanType.aliases.add("foo")
            >>> resolve_type("foo")
            BooleanType()

        .. testcleanup::

            pdcast.BooleanType.aliases.remove("foo")
        """
        alias = self.normalize_specifier(alias)

        registry = Type.registry
        if self.pinned and alias in registry.aliases:
            other = registry.aliases[alias]
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to "
                    f"{repr(other)}"
                )

        # register aliases with global registry
        if pin and not self.pinned:
            pin_aliases(self)

        self.aliases.add(alias)
        registry.flush()  # rebuild regex patterns

    def remove(self, alias: type_specifier, pin: bool = True) -> None:
        """Remove an alias from the managed type.

        Parameters
        ----------
        alias : type_specifier
            A valid type specifier to remove.
        pin : bool, default True
            Indicates whether to pin the aliases to the global
            :class:`TypeRegistry <pdcast.TypeRegistry>` and make them available
            to :func:`detect_type() <pdcast.detect_type>` and
            :func:`resolve_type() <pdcast.resolve_type>`.  If this is
            ``False``, then the alias will be removed from the manager without
            changing its pinned/unpinned status.  In general, this is only used
            during initialization.

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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> resolve_type("boolean")
            BooleanType()
            >>> BooleanType.aliases.remove("boolean")
            >>> resolve_type("boolean")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'boolean'
        """
        alias = self.normalize_specifier(alias)
        self.aliases.remove(alias)

        # remove aliases from global registry
        if pin and not self.pinned:
            unpin_aliases(self)

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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> BooleanType.aliases.discard("boolean")
            >>> BooleanType.aliases    # doctest: +SKIP
            AliasManager({'bool', 'bool_', 'bool8', 'b1', '?'})
            >>> BooleanType.aliases.discard("foo")
            >>> BooleanType.aliases    # doctest: +SKIP
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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> BooleanType.aliases.pop()   # doctest: +SKIP
            "bool"
            >>> BooleanType.aliases   # doctest: +SKIP
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

            >>> BooleanType.aliases   # doctest: +SKIP
            AliasManager({'bool', 'boolean', 'bool_', 'bool8', 'b1', '?'})
            >>> BooleanType.aliases.clear()
            >>> BooleanType.aliases
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

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __bool__(self) -> bool:
        return bool(self.aliases)

    def __len__(self) -> int:
        return len(self.aliases)

    def __contains__(self, alias: type_specifier) -> bool:
        return alias in self.aliases

    def __iter__(self) -> Iterator[type_specifier]:
        return iter(self.aliases)

    def __str__(self) -> str:
        return str(self.aliases)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.aliases})"


cdef class PrioritySet(set):
    """A subclass of set that stores pairs of VectorTypes ``(A, B)``, where
    ``A`` is always considered to be less than ``B``.
    """

    def _get_types(self, item: tuple) -> tuple:
        """Convert a pair into their types if they are given as instances and
        ensure that they are subclasses of VectorType.
        """
        # unpack
        small, large = item

        # get types
        if not isinstance(small, type):
            small = type(small)
        if not isinstance(large, type):
            large = type(large)

        # ensure subclass of VectorType
        if not issubclass(small, VectorType):
            raise TypeError(
                f"'{small.__qualname__}' must be a subclass of VectorType"
            )
        if not issubclass(large, VectorType):
            raise TypeError(
                f"'{large.__qualname__}' is not a subclass of VectorType"
            )

        return (small, large)

    def add(self, item: tuple) -> None:
        """Add a pair of types to the set as a directed edge.

        Parameters
        ----------
        item : tuple
            A pair of types ``(A, B)`` where ``A < B``.

        Raises
        ------
        ValueError
            If ``A`` and ``B`` are the same type or if the reverse edge
            ``(B, A)`` already exists.

        See Also
        --------
        PrioritySet.remove : Remove a pair of types from the set.
        PrioritySet.discard : Remove a pair of types from the set if it exists.
        PrioritySet.pop : Remove and return a pair of types from the set.
        PrioritySet.update : Add multiple pairs of types to the set.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.add((IntegerType, FloatType))
            >>> edges
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>)})
        """
        small, large = self._get_types(item)

        # ensure types are different
        if small is large:
            raise ValueError(
                f"types must be different: '{small.__qualname__}' == "
                f"'{large.__qualname__}'"
            )

        # ensure reverse edge does not exist
        if (large, small) in self:
            raise ValueError(
                f"edge already exists: '{small.__qualname__}' < "
                f"'{large.__qualname__}'"
            )

        super().add((small, large))

    def remove(self, item: tuple) -> None:
        """Remove an edge from the set.

        Parameters
        ----------
        item : tuple
            A pair of types ``(A, B)`` where ``A < B``.

        Raises
        ------
        KeyError
            If the edge ``(A, B)`` does not exist.

        See Also
        --------
        PrioritySet.add : Add a pair of types to the set.
        PrioritySet.discard : Remove a pair of types from the set if it exists.
        PrioritySet.pop : Remove and return a pair of types from the set.
        PrioritySet.update : Add multiple pairs of types to the set.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.add((IntegerType, FloatType))
            >>> edges
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>)})
            >>> edges.remove((IntegerType, FloatType))
            >>> edges
            PrioritySet(set())
        """
        super().remove(self._get_types(item))

    def discard(self, item: tuple) -> None:
        """Remove an edge from the set if it exists.

        Parameters
        ----------
        item : tuple
            A pair of types ``(A, B)`` where ``A < B``.

        See Also
        --------
        PrioritySet.add : Add a pair of types to the set.
        PrioritySet.remove : Remove a pair of types from the set.
        PrioritySet.pop : Remove and return a pair of types from the set.
        PrioritySet.update : Add multiple pairs of types to the set.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.add((IntegerType, FloatType))
            >>> edges
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>)})
            >>> edges.discard((IntegerType, FloatType))
            >>> edges
            PrioritySet(set())
            >>> edges.discard((BooleanType, ComplexType))
            >>> edges
            PrioritySet(set())
        """
        super().discard(self._get_types(item))

    def update(
        self,
        *others: Iterable[Tuple[Any, Any]]
    ) -> None:
        """Update the set with a collection of edges.

        Parameters
        ----------
        *others : Iterable[Tuple[Any, Any]]
            Any number of collections of pairs of types ``(A, B)`` where ``A < B``.

        See Also
        --------
        PrioritySet.add : Add a pair of types to the set.
        PrioritySet.remove : Remove a pair of types from the set.
        PrioritySet.discard : Remove a pair of types from the set if it exists.
        PrioritySet.pop : Remove and return a pair of types from the set.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.update([(IntegerType, FloatType), (BooleanType, ComplexType)])
            >>> edges  # doctest: +SKIP
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>), (<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>)})
        """
        for other in others:
            for item in other:
                self.add(item)

    def intersection_update(
        self,
        *others: Iterable[Tuple[Any, Any]]
    ) -> None:
        """Update the set with the intersection of itself and another.

        Parameters
        ----------
        *others : Iterable[Tuple[Any, Any]]
            Any number of collections of pairs of types ``(A, B)`` where ``A < B``.

        See Also
        --------
        PrioritySet.update : Add multiple edges to the set.
        PrioritySet.difference_update : Remove a collection of pairs from the
            set.
        PrioritySet.symmetric_difference_update : Update the set with the
            symmetric difference of itself and another.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.update([(IntegerType, FloatType), (BooleanType, ComplexType)])
            >>> edges  # doctest: +SKIP
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>), (<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>)})
            >>> edges.intersection_update([(IntegerType, FloatType), (FloatType, ComplexType)])
            >>> edges
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>)})
        """
        for other in others:
            other = set(self._get_types(item) for item in other)
            for item in self:
                if item not in other:
                    self.remove(item)

    def difference_update(
        self,
        *others: Iterable[Tuple[Any, Any]]
    ) -> None:
        """Remove a collection of edges from the set.

        Parameters
        ----------
        *others : Iterable[Tuple[Any, Any]]
            Any number of collections of pairs of types ``(A, B)`` where ``A < B``.

        See Also
        --------
        PrioritySet.update : Add multiple edges to the set.
        PrioritySet.intersection_update : Update the set with the intersection
            of itself and another.
        PrioritySet.symmetric_difference_update : Update the set with the
            symmetric difference of itself and another.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.update([(IntegerType, FloatType), (BooleanType, ComplexType)])
            >>> edges   # doctest: +SKIP
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>), (<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>)})
            >>> edges.difference_update([(IntegerType, FloatType), (FloatType, ComplexType)])
            >>> edges
            PrioritySet({(<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>)})
        """
        for other in others:
            other = set(self._get_types(item) for item in other)
            for item in other:
                self.discard(item)

    def symmetric_difference_update(
        self,
        other: Iterable[Tuple[Any, Any]]
    ) -> None:
        """Update the set with the symmetric difference of itself and another.

        Parameters
        ----------
        other : Iterable
            A collection of pairs of types ``(A, B)`` where ``A < B``.

        See Also
        --------
        PrioritySet.update : Add multiple edges to the set.
        PrioritySet.intersection_update : Update the set with the intersection
            of itself and another.
        PrioritySet.difference_update : Remove a collection of edges from the
            set.

        Examples
        --------
        .. doctest::

            >>> edges = PrioritySet()
            >>> edges.update([(IntegerType, FloatType), (BooleanType, ComplexType)])
            >>> edges   # doctest: +SKIP
            PrioritySet({(<class 'pdcast.types.integer.IntegerType'>, <class 'pdcast.types.float.FloatType'>), (<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>)})
            >>> edges.symmetric_difference_update([(IntegerType, FloatType), (FloatType, ComplexType)])
            >>> edges   # doctest: +SKIP
            PrioritySet({(<class 'pdcast.types.boolean.BooleanType'>, <class 'pdcast.types.complex.ComplexType'>), (<class 'pdcast.types.float.FloatType'>, <class 'pdcast.types.integer.IntegerType'>)})
        """
        other = set(self._get_types(item) for item in other)
        for item in other:
            if item in self:
                self.remove(item)
            else:
                self.add(item)

    def __ior__(self, other: Iterable[Tuple[Any, Any]]) -> PrioritySet:
        """Update the set with the union of itself and another."""
        self.update(other)
        return self

    def __iand__(self, other: Iterable[Tuple[Any, Any]]) -> PrioritySet:
        """Update the set with the intersection of itself and another."""
        self.intersection_update(other)
        return self

    def __isub__(self, other: Iterable[Tuple[Any, Any]]) -> PrioritySet:
        """Remove a collection of pairs from the set."""
        self.difference_update(other)
        return self

    def __ixor__(self, other: Iterable[Tuple[Any, Any]]) -> PrioritySet:
        """Update the set with the symmetric difference of itself and another.
        """
        self.symmetric_difference_update(other)
        return self

    def __contains__(self, item: tuple) -> bool:
        """Check if a pair of types is contained in the set."""
        return super().__contains__(self._get_types(item))

    def __str__(self) -> str:
        return str(set(self))

    def __repr__(self) -> str:
        return f"{type(self).__name__}({str(self)})"


cdef void pin_aliases(AliasManager manager):
    """Pin the aliases to the global TypeRegistry.
    """
    cdef TypeRegistry registry = Type.registry

    # validate aliases are unique
    for alias in manager:
        if alias in registry.aliases:
            raise ValueError(
                f"{repr(manager.instance)} alias must be unique: '{alias}' is "
                f"already registered to {repr(registry.aliases[alias])}"
            )

    # append to the pinned_aliases list if it is not already present
    for existing in registry.pinned_aliases:
        if existing.instance is manager.instance:
            break
    else:
        registry.pinned_aliases.append(manager)

    # set the pinned flag
    manager.pinned = True


cdef void unpin_aliases(AliasManager manager):
    """Unpin the aliases from the global TypeRegistry.
    """
    cdef TypeRegistry registry = Type.registry

    # remove from the pinned_aliases list if it is present
    registry.pinned_aliases = [
        existing for existing in registry.pinned_aliases
        if existing.instance is not manager.instance
    ]

    # reset the pinned flag
    manager.pinned = False
