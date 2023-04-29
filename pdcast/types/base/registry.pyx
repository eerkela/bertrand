"""This module describes a ``TypeRegistry`` object, which tracks registered
types and the relationships between them.
"""
import inspect
import regex as re  # using alternate regex
from types import MappingProxyType
from typing import Callable

cimport numpy as np
import numpy as np
import pandas as pd

cimport pdcast.types.base.atomic as atomic


######################
####    PUBLIC    ####
######################


cdef class TypeRegistry:
    """A registry representing the current state of ``pdcast``'s typing
    infrastructure.

    This is a `global object <https://python-patterns.guide/python/module-globals/>`_
    that is attached to every ``pdcast`` data type.  It is responsible for
    validating and managing every type that has been registered through the
    :func:`@register() <register>` decorator, as well as updating their
    attributes as new types are added.

    Notes
    -----

    , recording their aliases,
    dispatched methods, subtypes, supertype, backends, root type, and generic
    equivalent.



    Whenever a new type definition is registered through the
    :func:`@register() <register>` decorator, it is added to this registry
    
    
    containing every validated type object recognized by ``pdcast`` features.

    This is a global object attached to the base AtomicType class definition.
    It can be accessed through any of its instances, and it is updated
    automatically whenever a class inherits from AtomicType.  The registry
    itself contains methods to validate and synchronize subclass behavior,
    including the maintenance of a set of regular expressions that account for
    every known AtomicType alias, along with their default arguments.  These
    lists are updated dynamically each time a new alias and/or type is added
    to the pool.
    """

    def __init__(self):
        self.atomic_types = []
        self.update_hash()

    #####################
    ####    STATE    ####
    #####################

    @property
    def hash(self) -> int:
        """Hash representing the current state of the ``pdcast`` type system.

        This hash is updated whenever a new type is :meth:`added
        <TypeRegistry.add>` or :meth:`removed <TypeRegistry.remove>` from the
        registry or :meth:`gains <AtomicType.register_alias>`/:meth:`loses
        <AtomicType.remove_alias>` an :attr:`alias <AtomicType.aliases>` or
        :func:`dispatched <dispatch>` attribute.
        """
        return self._hash

    cdef void update_hash(self):
        """Hash the registry's internal state, for use in cached properties."""
        self._hash = hash(tuple(self.atomic_types))

    def flush(self):
        """Reset the registry's internal state, forcing every property to be
        recomputed.
        """
        self._hash += 1  # this is overflow-safe

    def remember(self, val) -> CacheValue:
        return CacheValue(value=val, hash=self.hash)

    def needs_updating(self, prop) -> bool:
        """Check if a `remember()`-ed registry property is out of date."""
        return prop is None or prop.hash != self.hash

    ##########################
    ####    ADD/REMOVE    ####
    ##########################

    def add(self, new_type: type) -> None:
        """Add an AtomicType/AdapterType subclass to the registry."""
        # validate subclass has required fields
        self.validate_name(new_type)
        self.validate_aliases(new_type)
        self.validate_slugify(new_type)

        # add type to registry and update hash
        self.atomic_types.append(new_type)
        self.update_hash()

    def remove(self, old_type: type) -> None:
        """Remove an AtomicType subclass from the registry."""
        self.atomic_types.remove(old_type)
        self.update_hash()

    def clear(self):
        """Clear the AtomicType registry of all AtomicType subclasses."""
        self.atomic_types.clear()
        self.update_hash()

    #####################
    ####    REGEX    ####
    #####################

    @property
    def aliases(self) -> dict:
        """An up-to-date dictionary mapping every alias to its corresponding
        type.

        Notes
        -----
        This is a cached property that is tied to the current state of the
        registry.  Whenever a new type is added, removed, or has one of its
        aliases changed, it will be regenerated to reflect that change.

        .. note::

            This table can be manually updated by :func:`adding <register>` or
            :meth:`removing <TypeRegistry.remove>` whole types, or by
            :meth:`updating <AtomicType.register_alias>` individual aliases, both of
            which globally change the behavior of the :func:`resolve_type` factory
            function.
        """
        # check if cache is out of date
        if self.needs_updating(self._aliases):
            result = {k: c for c in self.atomic_types for k in c.aliases}
            self._aliases = self.remember(result)

        # return cached value
        return self._aliases.value

    @property
    def regex(self) -> re.Pattern:
        """Compile a regular expression to match any registered AtomicType
        name or alias, as well as any arguments that may be passed to its
        `resolve()` constructor.
        """
        # check if cache is out of date
        if self.needs_updating(self._regex):
            # fastfail: empty case
            if not self.atomic_types:
                result = re.compile(".^")  # matches nothing

            # update using string aliases from every registered subtype
            else:
                # automatically escape reserved regex characters
                string_aliases = [
                    re.escape(k) for k in self.aliases if isinstance(k, str)
                ]
                string_aliases.append(r"(?P<sized_unicode>U(?P<size>[0-9]*))$")

                # sort into reverse order based on length
                string_aliases.sort(key=len, reverse=True)

                # join with regex OR and compile regex
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
        # check if cache is out of date
        if self.needs_updating(self._resolvable):
            # wrap self.regex in ^$ to match the entire string and allow for
            # comma-separated repetition of AtomicType patterns.
            pattern = rf"(?P<atomic>{self.regex.pattern})(,\s*(?&atomic))*"
            lead = r"((CompositeType\(\{)|\{)?"
            follow = r"((\}\))|\})?"

            # compile regex
            result = re.compile(rf"{lead}(?P<body>{pattern}){follow}")

            # remember result
            self._resolvable = self.remember(result)

        # return cached value
        return self._resolvable.value

    #######################
    ####    PRIVATE    ####
    #######################

    cdef int validate_name(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a unique `name` attribute
        associated with it.
        """
        validate(subclass, "name", expected_type=str)

        # ensure subclass.name is unique or inherited from generic type
        if (issubclass(subclass, atomic.AtomicType) and (
            subclass._is_generic != False or
            subclass.name != subclass._generic.name
        )):
            observed_names = {x.name for x in self.atomic_types}
            if subclass.name in observed_names:
                raise TypeError(
                    f"{subclass.__name__}.name ({repr(subclass.name)}) must be "
                    f"unique (not one of {observed_names})"
                )

    cdef int validate_aliases(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has an `aliases` dictionary
        and that none of its aliases overlap with another registered
        AtomicType.
        """
        validate(subclass, "aliases", expected_type=set)

        # ensure that no aliases are already registered to another AtomicType
        for k in subclass.aliases:
            if k in self.aliases:
                raise TypeError(
                    f"{subclass.__name__} alias {repr(k)} is already "
                    f"registered to {self.aliases[k].__name__}"
                )

    cdef int validate_slugify(self, type subclass) except -1:
        """Ensure that a subclass of AtomicType has a `slugify()`
        classmethod and that its signature matches __init__.
        """
        validate(
            subclass,
            "slugify",
            expected_type="classmethod",
            signature=subclass
        )

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, val) -> bool:
        return val in self.atomic_types

    def __hash__(self) -> int:
        return self.hash

    def __iter__(self):
        return iter(self.atomic_types)

    def __len__(self) -> int:
        return len(self.atomic_types)

    def __str__(self) -> str:
        return str(self.atomic_types)

    def __repr__(self) -> str:
        return repr(self.atomic_types)


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
    type subclass,
    str name,
    object expected_type = None,
    object signature = None,
) except -1:
    """Ensure that a subclass defines a particular named attribute."""
    # ensure attribute exists
    if not hasattr(subclass, name):
        raise TypeError(
            f"{subclass.__name__} must define a `{name}` attribute"
        )

    # get attribute value
    attr = getattr(subclass, name)

    # if an expected type is given, check it
    if expected_type is not None:
        if expected_type in ("method", "classmethod"):
            bound = getattr(attr, "__self__", None)
            if expected_type == "method" and bound:
                raise TypeError(
                    f"{subclass.__name__}.{name}() must be an instance method"
                )
            elif expected_type == "classmethod" and bound != subclass:
                raise TypeError(
                    f"{subclass.__name__}.{name}() must be a classmethod"
                )
        elif not isinstance(attr, expected_type):
            raise TypeError(
                f"{subclass.__name__}.{name} must be of type {expected_type}, "
                f"not {type(attr)}"
            )

    # if attribute has a signature match, check it
    if signature is not None:
        if (
            isinstance(signature, type) and
            signature.__init__ == atomic.AtomicType.__init__
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
                f"{subclass.__name__}.{name}() must have the following "
                f"signature: {dict(expected)}, not {attr_sig}"
            )
