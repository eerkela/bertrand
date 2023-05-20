"""This module describes a ``CompositeType`` object, which can be used to
group types into a set-like container.
"""
cimport numpy as np
import numpy as np

from pdcast cimport resolve
from pdcast import resolve
from pdcast.util.type_hints import type_specifier

from . cimport atomic
from . cimport adapter


cdef class CompositeType(BaseType):
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
        # null
        if types is None:
            self._types = set()

        # scalar
        elif isinstance(types, atomic.ScalarType):
            self._types = {types}

        # other composite
        elif isinstance(types, CompositeType):
            self._types = types._types.copy()
            if index is None:
                index = types.index

        else:
            bad_input = False
            if hasattr(types, "__iter__") and not isinstance(types, str):
                self._types = set()
                for typ in types:
                    if isinstance(typ, CompositeType):
                        self._types.update(t for t in typ)
                    elif isinstance(typ, atomic.BaseType):
                        self._types.add(typ)
                    else:
                        bad_input = True
                        break
            else:
                bad_input = True

            if bad_input:
                raise TypeError(
                    f"CompositeTypes can only contain other types, not "
                    f"{type(types)}"
                )

        # assign index
        self.index = index

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    def __getattr__(self, name: str) -> dict:
        """Implement the Composite design pattern with respect to the contained
        types.
        """
        return {typ: getattr(typ, name) for typ in self}

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

    def is_subtype(self, typespec: type_specifier, *args, **kwargs) -> bool:
        """Do a collective membership test involving the whole composite,
        rather than its individual components.
        """
        return resolve.resolve_type(typespec).contains(self, *args, **kwargs)

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    def collapse(self) -> CompositeType:
        """Return a copy with redundant subtypes removed.  A subtype is
        redundant if it is fully encapsulated within the other members of the
        CompositeType.
        """
        cdef atomic.ScalarType atomic_type
        cdef atomic.ScalarType t

        # for every type a, check if there is another type b such that a != b
        # and a in b.  If this is true, then the type is redundant.
        return CompositeType(
            a for a in self
            if not any(a != b and a in b for b in self._types)
        )

    def expand(self) -> CompositeType:
        """Expand the contained types to include each of their subtypes."""
        gen = (z for x in self for y in x.backends.values() for z in y.subtypes)
        return self.union(gen)

    cdef void forget_index(self):
        self.index = None

    # TODO: use Composite Pattern instead

    @property
    def subtypes(self) -> CompositeType:
        """An alias for `CompositeType.expand()` that excludes backends."""
        return self.union(y for x in self for y in x.subtypes)

    ####################################
    ####    STATIC WRAPPER (SET)    ####
    ####################################

    def add(self, typespec: type_specifier) -> None:
        """Add a type specifier to the CompositeType."""
        cdef CompositeType other
        cdef atomic.ScalarType typ

        other = resolve.resolve_type([typespec])
        self._types.update(typ for typ in other)
        self.forget_index()

    def clear(self) -> None:
        """Remove all types from the CompositeType."""
        self._types.clear()
        self.forget_index()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        return CompositeType(self)

    def discard(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        cdef CompositeType other
        cdef atomic.ScalarType typ

        other = resolve.resolve_type([typespec])
        for typ in other:
            self._types.discard(typ)

        self.forget_index()

    def pop(self) -> atomic.ScalarType:
        """Remove and return an arbitrary type from the CompositeType. Raises a
        KeyError if the CompositeType is empty.
        """
        self.forget_index()
        return self._types.pop()

    def remove(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        cdef CompositeType other
        cdef atomic.ScalarType typ

        other = resolve.resolve_type([typespec])
        for typ in other:
            self._types.remove(typ)

        self.forget_index()

    ##############################
    ####    SET OPERATIONS    ####
    ##############################

    def __or__(self, typespec: type_specifier) -> CompositeType:
        """Return the union of two CompositeTypes."""
        cdef CompositeType other

        other = resolve.resolve_type([typespec])
        return CompositeType(self._types | other._types)

    def __and__(self, typespec: type_specifier) -> CompositeType:
        """Return the intersection of two CompositeTypes."""
        cdef CompositeType other
        cdef atomic.ScalarType typ
        cdef set forward
        cdef set backward

        other = resolve.resolve_type([typespec])
        forward = {typ for typ in self if typ in other}
        backward = {typ for typ in other if typ in self}
        return CompositeType(forward | backward)

    def __sub__(self, typespec: type_specifier) -> CompositeType:
        """Return the difference of two CompositeTypes."""
        cdef CompositeType other
        cdef atomic.ScalarType typ
        cdef atomic.ScalarType other_type
        cdef set result

        other = resolve.resolve_type([typespec]).expand()
        has_child = lambda typ: any(other_type in typ for other_type in other)
        result = {typ for typ in self.expand() if not has_child(typ)}
        return CompositeType(result).collapse()

    def __xor__(self, typespec: type_specifier) -> CompositeType:
        """Return the symmetric difference of two CompositeTypes."""
        cdef CompositeType other

        other = resolve.resolve_type([typespec])
        return (self - other) | (other - self)

    ###############################
    ####    SET COMPARISONS    ####
    ###############################

    def __gt__(self, typespec: type_specifier) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        cdef BaseType other

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
        return self._types == other._types

    def __lt__(self, typespec: type_specifier) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        cdef atomic.BaseType other

        other = resolve.resolve_type(typespec)
        return self != other and self <= other

    def __le__(self, typespec: type_specifier) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        return resolve.resolve_type(typespec).contains(self)

    ####################
    ####    MISC    ####
    ####################

    def __contains__(self, typespec: type_specifier) -> bool:
        """Equivalent to ``self.contains(typespec)``."""
        return self.contains(typespec)

    def __iter__(self):
        """Iterate through the types contained within a CompositeType."""
        return iter(self._types)

    def __len__(self):
        """Return the number of types in the CompositeType."""
        return len(self._types)

    def __repr__(self) -> str:
        cdef str slugs = ", ".join(str(x) for x in self._types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        cdef str slugs = ", ".join(str(x) for x in self._types)
        return f"{{{slugs}}}"
