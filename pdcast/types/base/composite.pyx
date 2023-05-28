"""This module describes a ``CompositeType`` object, which can be used to
group types into a set-like container.
"""
cimport numpy as np
import numpy as np

from pdcast import resolve
from pdcast.util.type_hints import type_specifier

from .scalar cimport ScalarType
from .registry cimport AliasManager, Type


# TODO: if supporting dynamic aliases, CompositeType must implement
# from_string, from_dtype


cdef class CompositeType(Type):
    """Set-like container for type objects.

    Implements the same interface as the built-in set type, but is restricted
    to contain only AtomicType and AdapterType objects.  Also extends
    subset/superset/membership checks to include subtypes for each of the
    contained scalar types.
    """

    def __init__(
        self,
        object types = None,
        np.ndarray[object] index = None
    ):
        super().__init__()

        if types is None:
            self.types = set()

        elif isinstance(types, ScalarType):
            self.types = {types}

        elif isinstance(types, CompositeType):
            self.types = types.types.copy()
            if index is None:
                index = types.index

        else:
            invalid = False
            if hasattr(types, "__iter__") and not isinstance(types, str):
                self.types = set()
                for typ in types:
                    if isinstance(typ, CompositeType):
                        self.types.update(t for t in typ)
                    elif isinstance(typ, Type):
                        self.types.add(typ)
                    else:
                        invalid = True
                        break
            else:
                invalid = True

            if invalid:
                raise TypeError(
                    f"CompositeTypes can only contain other types, not "
                    f"{type(types)}"
                )

        self.index = index

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    cdef void forget_index(self):
        self.index = None

    def expand(self, expand_generics: bool = True) -> CompositeType:
        """Expand the contained types to include each of their subtypes."""
        cdef CompositeType result
        cdef ScalarType original
        cdef ScalarType typ

        result = self.copy()
        if expand_generics:
            for original in self:
                backends = getattr(original, "backends", {None: original})
                for typ in backends.values():
                    result.add(typ)

        for typ in result.copy():
            result |= traverse_subtypes(typ, result)

        return result

    def collapse(self) -> CompositeType:
        """Return a copy with redundant subtypes removed.  A subtype is
        redundant if it is fully encapsulated within the other members of the
        CompositeType.
        """
        cdef ScalarType atomic_type
        cdef ScalarType t

        # for every type a in self, check if there is another type b such that
        # a != b and b contains a.  If true, the type is redundant.
        return CompositeType(
            a for a in self
            if not any(a != b and a in b for b in self.types)
        )

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Do a collective membership test involving the whole composite,
        rather than its individual components.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(other_typ, include_subtypes=include_subtypes)
                for other_typ in other
            )

        return any(
            typ.contains(other, include_subtypes=include_subtypes)
            for typ in self
        )

    def is_subtype(
        self,
        other: type_specifier,
        *args,
        **kwargs
    ) -> bool:
        """Do a collective membership test involving the whole composite,
        rather than its individual components.
        """
        other = resolve.resolve_type(other)
        return other.contains(self, *args, **kwargs)

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(self, typespec: type_specifier) -> None:
        """Add a type specifier to the CompositeType."""
        cdef CompositeType other
        cdef ScalarType typ

        other = resolve.resolve_type([typespec])
        self.types.update(typ for typ in other)
        self.forget_index()

    def remove(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        cdef CompositeType other
        cdef ScalarType typ

        other = resolve.resolve_type([typespec])
        for typ in other:
            self.types.remove(typ)

        self.forget_index()

    def discard(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        cdef CompositeType other
        cdef ScalarType typ

        other = resolve.resolve_type([typespec])
        for typ in other:
            self.types.discard(typ)

        self.forget_index()

    def pop(self) -> ScalarType:
        """Remove and return an arbitrary type from the CompositeType. Raises a
        KeyError if the CompositeType is empty.
        """
        self.forget_index()
        return self.types.pop()

    def clear(self) -> None:
        """Remove all types from the CompositeType."""
        self.types.clear()
        self.forget_index()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        return CompositeType(self)

    ##########################
    ####    OPERATIONS    ####
    ##########################

    def union(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with the
        contents of this and all others.
        """
        cdef CompositeType result

        result = self.copy()
        for other in others:
            result |= other
        return result

    def intersection(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with
        types in common to this and all others.
        """
        cdef CompositeType result

        result = self.copy()
        for other in others:
            result &= other
        return result

    def difference(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with
        types that are not in any of the others.
        """
        cdef CompositeType result
        cdef CompositeType other
        cdef ScalarType typ

        result = self.expand()

        has_child = lambda typ: any(other_type in typ for other_type in other)
        for item in others:
            other = resolve.resolve_type([item]).expand()
            result = CompositeType(typ for typ in result if not has_child(typ))

        return result.collapse()

    def symmetric_difference(self, other: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with
        types that are in either this or ``other``, but not both.
        """
        return self ^ other

    def __or__(self, typespec: type_specifier) -> CompositeType:
        """Return the union of two CompositeTypes."""
        cdef CompositeType other

        other = resolve.resolve_type([typespec])
        return CompositeType(self.types | other.types)

    def __and__(self, typespec: type_specifier) -> CompositeType:
        """Return the intersection of two CompositeTypes."""
        cdef CompositeType other
        cdef ScalarType typ
        cdef set forward
        cdef set backward

        other = resolve.resolve_type([typespec])
        forward = {typ for typ in self if typ in other}
        backward = {typ for typ in other if typ in self}
        return CompositeType(forward | backward)

    def __sub__(self, typespec: type_specifier) -> CompositeType:
        """Return the difference of two CompositeTypes."""
        cdef CompositeType result
        cdef CompositeType other
        cdef ScalarType typ
        cdef ScalarType other_type

        result = self.expand()

        other = resolve.resolve_type([typespec]).expand()
        has_child = lambda typ: any(other_type in typ for other_type in other)
        result = CompositeType(typ for typ in result if not has_child(typ))

        return result.collapse()

    def __xor__(self, typespec: type_specifier) -> CompositeType:
        """Return the symmetric difference of two CompositeTypes."""
        cdef CompositeType other

        other = resolve.resolve_type([typespec])
        return (self - other) | (other - self)

    ###################################
    ####    IN-PLACE OPERATIONS    ####
    ###################################

    def update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        adding the contents of all others.
        """
        for other in others:
            self |= other

    def intersection_update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        keeping types found in it and all others.
        """
        for other in others:
            self &= other

    def difference_update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        removing types from any of the others.
        """
        for other in others:
            self -= other

    def symmetric_difference_update(self, other: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        keeping types that are found in it or ``other``, but not both.
        """
        self ^= other

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def issubset(self, other: type_specifier) -> bool:
        """TODO"""
        raise NotImplementedError()

    def issuperset(self, other: type_specifier) -> bool:
        """TODO"""
        raise NotImplementedError()

    def isdisjoint(self, other: type_specifier) -> bool:
        """TODO"""
        raise NotImplementedError()

    def __gt__(self, typespec: type_specifier) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        cdef Type other

        other = resolve.resolve_type(typespec)
        return self != other and self >= other

    def __ge__(self, typespec: type_specifier) -> bool:
        """Test whether every element in `other` is contained within `self`.
        """
        return self.contains(typespec)

    def __eq__(self, typespec: type_specifier) -> bool:
        """Test whether `self` and `other` contain identical types."""
        cdef CompositeType other

        other = resolve.resolve_type([typespec])
        return self.types == other.types

    def __lt__(self, typespec: type_specifier) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        cdef Type other

        other = resolve.resolve_type(typespec)
        return self != other and self <= other

    def __le__(self, typespec: type_specifier) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        return resolve.resolve_type(typespec).contains(self)

    ####################
    ####    MISC    ####
    ####################

    def __bool__(self) -> bool:
        """True if this :class:`CompositeType <pdcast.CompositeType>` is empty.
        """
        return bool(self.types)

    def __contains__(self, typespec: type_specifier) -> bool:
        """Equivalent to ``self.contains(typespec)``."""
        return self.contains(typespec)

    def __iter__(self):
        """Iterate through the types contained within a CompositeType."""
        return iter(self.types)

    def __len__(self):
        """Return the number of types in the CompositeType."""
        return len(self.types)

    def __repr__(self) -> str:
        cdef str slugs = ", ".join(str(x) for x in self.types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        cdef str slugs = ", ".join(str(x) for x in self.types)
        return f"{{{slugs}}}"


#######################
####    PRIVATE    ####
#######################


cdef CompositeType traverse_subtypes(
    ScalarType typ,
    CompositeType result
):
    """Traverse through a scalar type's subtype tree, recursively gathering
    all its children.
    """
    cdef CompositeType subtypes

    subtypes = getattr(typ, "subtypes", CompositeType())
    return result.union(
        traverse_subtypes(subtype, result) for subtype in subtypes
    )
