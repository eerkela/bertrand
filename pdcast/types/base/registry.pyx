"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any

# from . cimport atomic
from . cimport scalar


# TODO: we probably have to adjust @subtype/@implementation decorators to
# account for unregistered types.


# TODO: @subtype should be decoupled from @generic
# -> maybe separated into @generic, @supertype?  @supertype must be
# cooperative


# TODO: @register should transform the decorated type into its base instance.
# -> what do we do for AdapterTypes?



# TODO: Using GenericType for both subtypes and backends causes
# int[numpy].generic to return a circular reference rather than pointing to int



# TODO: @backend should be an independent decorator, which resolves the given
# type and checks that it is generic

# @subtype("signed[numpy]")
# @backend("int8", "numpy")

# This could be used alongside `cond` argument of @register if that
# short-circuits the class definition.  If the type specifier could not be
# interpreted, we either silently ignore or say so manually.  This could avoid
# manual TypeRegistry interactions in 


######################
####    PUBLIC    ####
######################


def register(
    class_: type | scalar.ScalarType | None = None,
    *,
    cond: bool = True
) -> scalar.ScalarType:
    """Validate a scalar type definition and add it to the registry.

    Parameters
    ----------
    class_ : type | GenericType | None
        The type definition to register.
    cond : bool, default True
        Used to create :ref:`conditional types <tutorial.conditional>`.  The
        type will only be added to the registry if this evaluates ``True``.

    Returns
    -------
    ScalarType
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

        *   :attr:`class_.name <AtomicType.name>`: this must be unique or
            inherited from a :func:`generic() <pdcast.generic>` type.
        *   :attr:`class_.aliases <AtomicType.aliases>`: these must contain
            only valid type specifiers, each of which must be unique.
        *   :meth:`class_.slugify() <AtomicType.slugify>`: this must be a
            classmethod whose signature matches the decorated class's
            ``__init__``.

    Examples
    --------
    TODO: take from tutorial

    """
    # TODO: one consequence of instantiating every type is that aliases might
    # be tied to instances of that type rather than classes, which would enable
    # flexible naming.  You could assign an alias for "pacific" that points to
    # datetime[pandas, US/Pacific].  Aliases might then apply to composites
    # as well.  "numeric" could be an alias for [bool, int, float, complex,
    # decimal, ...].  You could thus create your own collections of types, and
    # 'pin' them in a sense.

    def register_decorator(cls: type | scalar.ScalarType) -> scalar.ScalarType:
        """Add the type to the registry and instantiate it."""
        if isinstance(cls, scalar.ScalarType):
            instance = cls
        else:
            if not issubclass(cls, scalar.ScalarType):
                raise TypeError(
                    "`@register` can only be applied to AtomicType and "
                    "AdapterType subclasses"
                )
            instance = cls()

        if cond:
            cls.registry.add(instance)
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
        self.base_types = set()
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
        :meth:`gains <pdcast.ScalarType.register_alias>` or
        :meth:`loses <pdcast.ScalarType.remove_alias>` an alias.
        """
        return self._hash

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self._hash = hash(tuple(self.base_types))

    def flush(self):
        """Reset the registry's internal state, forcing every property to be
        recomputed.
        """
        self._hash += 1

    def remember(self, val: Any) -> CacheValue:
        """Record a value, tying it to the registry's internal state.

        Parameters
        ----------
        val : Any
            A value to cache

        Returns
        -------
        CacheValue
            A wrapper around the value that records the observed state of the
            registry at the time it was cached.
        """
        return CacheValue(val, self.hash)

    def needs_updating(self, val: CacheValue | None) -> bool:
        """Check if a :meth:`remembered <pdcast.TypeRegistry.remember>` value
        is out of date.
        """
        return val is None or val.hash != self.hash

    ##########################
    ####    ADD/REMOVE    ####
    ##########################

    def add(self, instance: scalar.ScalarType) -> None:
        """Validate a base type and add it to the registry.

        Parameters
        ----------
        instance : ScalarType
            An instance of a :class:`ScalarType <pdcast.ScalarType>` to add to
            the registry.  This instance must not be parametrized, and it must
            implement at least the :attr:`name <pdcast.ScalarType.name>` and
            :attr:`aliases <pdcast.ScalarType.aliases>` attributes  to be
            considered valid.

        Raises
        ------
        TypeError
            If the instance is malformed in some way.  This can happen if the
            type is parametrized, does not have an appropriate
            :attr:`name <pdcast.ScalarType.name>` or
            :attr:`aliases <pdcast.ScalarType.aliases>`, or if the signature of
            its :meth:`slugify <pdcast.ScalarType.slugify>` method does not
            match its constructor.

        See Also
        --------
        register : automatically call this method as a class decorator.
        TypeRegistry.remove : remove a type from the registry.
        TypeRegistry.clear : remove all types from the registry.
        """
        self._validate_no_parameters(instance)
        self._validate_name(instance)
        # self._validate_slugify(instance)
        self.base_types.add(instance)
        self.update_hash()

    def remove(self, instance: scalar.ScalarType) -> None:
        """Remove a base type from the registry.

        Parameters
        ----------
        instance : ScalarType
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
        self.base_types.remove(instance)
        self.update_hash()

    def clear(self):
        """Clear the AtomicType registry, removing every type at once.

        See Also
        --------
        TypeRegistry.add : add a type to the registry.
        TypeRegistry.remove : remove a single type from the registry.
        """
        self.base_types.clear()
        self.update_hash()

    #####################
    ####    REGEX    ####
    #####################

    @property
    def aliases(self) -> dict:
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
        This is a :meth:`remembered <pdcast.TypeRegistry.remember>` property
        that is tied to the current state of the registry.  Whenever a new type
        is :meth:`added <pdcast.TypeRegistry.add>`,
        :meth:`removed <pdcast.TypeRegistry.remove>`, or
        :meth:`gains <pdcast.ScalarType.register_alias>`\ /
        :meth:`loses <pdcast.ScalarType.remove_alias>` an alias, it will be
        regenerated to reflect that change.
        """
        # check if cache is out of date
        if self.needs_updating(self._aliases):
            result = {
                alias: typ for typ in self.base_types for alias in typ.aliases
            }
            self._aliases = self.remember(result)

        # return cached value
        return self._aliases.value

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
        # check if cache is out of date
        if self.needs_updating(self._regex):
            # trivial case: empty registry
            if not self.base_types:
                result = re.compile(".^")  # matches nothing
            else:
                # automatically escape reserved regex characters
                string_aliases = [
                    re.escape(alias) for alias in self.aliases
                    if isinstance(alias, str)
                ]

                # special case for sized unicode in numpy syntax
                string_aliases.append(r"(?P<sized_unicode>U(?P<size>[0-9]*))$")

                # sort into reverse order based on length
                string_aliases.sort(key=len, reverse=True)

                # join with regex OR and compile
                result = re.compile(
                    rf"(?P<type>{'|'.join(string_aliases)})"
                    rf"(?P<nested>\[(?P<args>([^\[\]]|(?&nested))*)\])?"
                )

            # remember result
            self._regex = self.remember(result)

        # return cached value
        return self._regex.value

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
        # check if cache is out of date
        if self.needs_updating(self._resolvable):
            # wrap self.regex in ^$ to match the entire string and allow for
            # comma-separated repetition.
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

            # compile regex
            result = re.compile(rf"({lead})?(?P<body>{pattern})({follow})?")

            # remember result
            self._resolvable = self.remember(result)

        # return cached value
        return self._resolvable.value

    #######################
    ####    PRIVATE    ####
    #######################

    def _validate_no_parameters(self, instance: scalar.ScalarType) -> None:
        """Ensure that a base type is not parametrized."""
        # TODO: inspect the type's kwargs
        pass

    def _validate_name(self, instance: scalar.ScalarType) -> None:
        """Ensure that a base type has a unique name attribute."""
        if not isinstance(instance.name, str):
            raise TypeError(f"{instance.__qualname__}.name must be a string")

        # ensure typ.name is unique or inherited from generic type
        # if (
        #     isinstance(instance, atomic.AtomicType) and
        #     instance._is_generic != False or
        #    instance.name != instance._generic.name
        #):
        #    observed_names = {x.name for x in self.base_types}
        #    if instance.name in observed_names:
        #        raise TypeError(
        #            f"name must be unique, not one of {observed_names}"
        #        )

    def _validate_slugify(self, instance: scalar.ScalarType) -> None:
        """Ensure that a base type has a slugify() classmethod and that its
        signature matches __init__.
        """
        validate(
            instance,
            "slugify",
            expected_type="classmethod",
            signature=type(instance)
        )

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, val) -> bool:
        return val in self.base_types

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self):
        return iter(self.base_types)

    def __len__(self) -> int:
        return len(self.base_types)

    def __str__(self) -> str:
        return str(self.base_types)

    def __repr__(self) -> str:
        return repr(self.base_types)


cdef class BaseType:
    """Base type for all type objects.

    This has no interface of its own.  It simply serves to anchor inheritance
    and distribute the shared type registry to all ``pdcast`` types.
    """

    registry: TypeRegistry = TypeRegistry()


#######################
####    PRIVATE    ####
#######################


cdef class CacheValue:
    """A simple struct to hold cached values in TypeRegistry.

    Note: this can't be an *actual* struct because it stores a python object
    in its ``value`` field.
    """

    def __init__(self, object value, long long hash):
        self.value = value
        self.hash = hash


cdef int validate(
    object typ,
    str name,
    object expected_type = None,
    object signature = None,
) except -1:
    """Ensure that a subclass defines a particular named attribute."""
    # ensure attribute exists
    if not hasattr(typ, name):
        raise TypeError(f"{typ.__name__} must define a `{name}` attribute")

    # get attribute value
    attr = getattr(typ, name)

    # if an expected type is given, check it
    if expected_type is not None:
        if expected_type in ("method", "classmethod"):
            bound = getattr(attr, "__self__", None)
            if expected_type == "method" and bound:
                raise TypeError(
                    f"{typ.__name__}.{name}() must be an instance method"
                )
            elif expected_type == "classmethod" and bound != typ:
                raise TypeError(
                    f"{typ.__name__}.{name}() must be a classmethod"
                )
        elif not isinstance(attr, expected_type):
            raise TypeError(
                f"{typ.__name__}.{name} must be of type {expected_type}, not "
                f"{type(attr)}"
            )

    # if attribute has a signature match, check it
    if signature is not None:
        if (
            isinstance(signature, type) and
            signature.__init__ == scalar.ScalarType.__init__
        ):
            expected = MappingProxyType({})
        else:
            expected = inspect.signature(signature).parameters

        try:
            attr_sig = inspect.signature(attr).parameters
        except ValueError:  # cython methods aren't introspectable
            attr_sig = MappingProxyType({})

        if attr_sig != expected:
            raise TypeError(
                f"{typ.__name__}.{name}() must have the following signature: "
                f"{dict(expected)}, not {attr_sig}"
            )
