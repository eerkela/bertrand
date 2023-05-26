"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Any

cimport numpy as np
import numpy as np
import pandas as pd

from pdcast.util.type_hints import type_specifier

from . import scalar


# TODO: we probably have to adjust @subtype/@implementation decorators to
# account for unregistered types.


# TODO: @subtype should be decoupled from @generic
# -> maybe separated into @generic, @supertype?  @supertype must be
# cooperative


# TODO: Whenever AliasManager.add is called, we add it to the registry.
# .remove(), .discard(), and .pop() remove it from the registry if empty.

# This allows us to store unique AliasManagers at the instance level.

# pdcast.resolve_type("object[int]") should not have any aliases, but
# pdcast.resolve_type("object") should.


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
        self.promises = {}
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

        self.base_types.add(instance)
        promises = self.promises.pop(type(instance), [])
        while promises:
            delayed = promises.pop()
            delayed(instance)

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
        This is a cached property tied to the current state of the registry.
        Whenever a new type is :meth:`added <pdcast.TypeRegistry.add>`,
        :meth:`removed <pdcast.TypeRegistry.remove>`, or
        :meth:`gains <pdcast.ScalarType.register_alias>`\ /
        :meth:`loses <pdcast.ScalarType.remove_alias>` an alias, it will be
        regenerated to reflect that change.
        """
        cached = self._aliases
        if not cached:
            cached = CacheValue({
                alias: typ for typ in self.base_types for alias in typ.aliases
            })
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
            if not self.base_types:
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

    def _validate_no_parameters(self, instance: scalar.ScalarType) -> None:
        """Ensure that a base type is not parametrized."""
        # TODO: inspect the type's kwargs by comparing against
        # instance._base_instance.  Currently, this fails for GenericTypes
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


cdef class AliasManager:
    """Interface for dynamically managing a type's aliases."""

    def __init__(self, set aliases):
        self.aliases = set()
        for alias in aliases:
            self.add(alias)

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
        self._check_specifier(alias)
        alias = self._normalize_specifier(alias)

        if alias in BaseType.registry.aliases:
            other = BaseType.registry.aliases[alias]
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to "
                    f"{repr(other)}"
                )

        self.aliases.add(alias)
        BaseType.registry.flush()  # rebuild regex patterns

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
        self._check_specifier(alias)
        self.aliases.remove(alias)
        BaseType.registry.flush()  # rebuild regex patterns

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
        BaseType.registry.flush()
        return value

    def clear(self) -> None:
        """Remove every alias that is registered to the managed type.

        Notes
        -----
        See the docs on the :ref:`type specification mini language
        <resolve_type.mini_language>` for more information on how aliases work.
        """
        self.aliases.clear()
        BaseType.registry.flush()  # rebuild regex patterns

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

    cdef int _check_specifier(self, alias: type_specifier) except -1:
        """Ensure that an alias is a valid type specifier."""
        if not isinstance(alias, type_specifier):
            raise TypeError(
                f"alias must be a valid type specifier: {repr(alias)}"
            )

    cdef object _normalize_specifier(self, alias: type_specifier):
        """Preprocess a type specifier, converting it into a recognizable
        format.
        """
        # ignore parametrized dtypes
        if isinstance(alias, (np.dtype, pd.api.extensions.ExtensionDtype)):
            return type(alias)

        return alias

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
    """A simple struct to hold cached values tied to the global state of the
    registry.
    """

    def __init__(self, object value):
        self.value = value
        self.hash = BaseType.registry.hash

    def __bool__(self) -> bool:
        """Indicates whether a cached registry value is out of date."""
        return self.hash == BaseType.registry.hash


# TODO: validate() is unused


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
