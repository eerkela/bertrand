cimport numpy as np
import numpy as np

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

cimport pdcast.types.base.atomic as atomic
cimport pdcast.types.base.adapter as adapter

from pdcast.util.type_hints import type_specifier


# contains() should accept an `exact` argument which disregards subtypes.


cdef class CompositeType(BaseType):
    """Set-like container for type objects.

    Implements the same interface as the built-in set type, but is restricted
    to contain only AtomicType and AdapterType objects.  Also extends
    subset/superset/membership checks to include subtypes for each of the
    contained scalar types.
    """

    def __init__(
        self,
        atomic_types = None,
        np.ndarray[object] index = None
    ):
        # parse argument
        if atomic_types is None:  # empty
            self.atomic_types = set()
        elif isinstance(atomic_types, atomic.ScalarType):  # wrap
            self.atomic_types = {atomic_types}
        elif isinstance(atomic_types, CompositeType):  # copy
            self.atomic_types = atomic_types.atomic_types.copy()
            if index is None:
                index = atomic_types.index
        elif (
            hasattr(atomic_types, "__iter__") and
            not isinstance(atomic_types, str)
        ):  # build
            self.atomic_types = set()
            for val in atomic_types:
                if isinstance(val, atomic.ScalarType):
                    self.atomic_types.add(val)
                elif isinstance(val, CompositeType):
                    self.atomic_types.update(x for x in val)
                else:
                    raise TypeError(
                        f"CompositeType objects can only contain scalar "
                        f"types, not {type(val)}"
                    )
        else:
            raise TypeError(
                f"CompositeType objects can only contain scalar types, "
                f"not {type(atomic_types)}"
            )

        # assign index
        self.index = index
    
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
            if not any(a != b and a in b for b in self.atomic_types)
        )

    def contains(self, other: type_specifier, exact: bool = False) -> bool:
        """Test whether a given type specifier is fully contained within `self`
        or any combination of its elements.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(o, exact=exact) for o in other)
        return any(x.contains(other, exact=exact) for x in self)

    def expand(self) -> CompositeType:
        """Expand the contained types to include each of their subtypes."""
        gen = (z for x in self for y in x.backends.values() for z in y.subtypes)
        return self.union(gen)

    cdef void forget_index(self):
        self.index = None

    @property
    def subtypes(self) -> CompositeType:
        """An alias for `CompositeType.expand()` that excludes backends."""
        return self.union(y for x in self for y in x.subtypes)

    ####################################
    ####    STATIC WRAPPER (SET)    ####
    ####################################

    def add(self, typespec: type_specifier) -> None:
        """Add a type specifier to the CompositeType."""
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, atomic.ScalarType):
            self.atomic_types.add(resolved)
        else:
            self.atomic_types.update(t for t in resolved)

        # throw out index
        self.forget_index()

    def clear(self) -> None:
        """Remove all types from the CompositeType."""
        self.atomic_types.clear()
        self.forget_index()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the CompositeType."""
        return CompositeType(self)

    def difference(self, *others) -> CompositeType:
        """Return a new CompositeType with types that are not in any of the
        others.
        """
        cdef atomic.ScalarType x
        cdef atomic.ScalarType y
        cdef CompositeType other
        cdef CompositeType result = self.expand()

        # search expanded set and reject types that contain an item in other
        for item in others:
            other = CompositeType(resolve.resolve_type(item)).expand()
            result = CompositeType(
                x for x in result if not any(y in x for y in other)
            )

        # expand/reduce called only once
        return result.collapse()

    def difference_update(self, *others) -> None:
        """Update a CompositeType in-place, removing types that can be found in
        others.
        """
        self.atomic_types = self.difference(*others).atomic_types
        self.forget_index()

    def discard(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, atomic.ScalarType):
            self.atomic_types.discard(resolved)
        else:
            for t in resolved:
                self.atomic_types.discard(t)

        # throw out index
        self.forget_index()

    def intersection(self, *others) -> CompositeType:
        """Return a new CompositeType with types in common to this
        CompositeType and all others.
        """
        cdef CompositeType other
        cdef CompositeType result = self.copy()

        # include any type in self iff it is contained in other.  Do the same
        # in reverse.
        for item in others:
            other = CompositeType(resolve.resolve_type(other))
            result = CompositeType(
                {a for a in result if a in other} |
                {t for t in other if t in result}
            )

        # return compiled result
        return result

    def intersection_update(self, *others) -> None:
        """Update a CompositeType in-place, keeping only the types found in it
        and all others.
        """
        self.atomic_types = self.intersection(*others).atomic_types
        self.forget_index()

    def isdisjoint(self, other: type_specifier) -> bool:
        """Return `True` if the CompositeType has no types in common with
        `other`.

        CompositeTypes are disjoint if and only if their intersection is the
        empty set.
        """
        return not self.intersection(other)

    def issubset(self, other: type_specifier) -> bool:
        """Test whether every type in the CompositeType is also in `other`."""
        return self in resolve.resolve_type(other)

    def issuperset(self, other: type_specifier) -> bool:
        """Test whether every type in `other` is contained within the
        CompositeType.
        """
        return self.contains(other)

    def pop(self) -> atomic.ScalarType:
        """Remove and return an arbitrary type from the CompositeType. Raises a
        KeyError if the CompositeType is empty.
        """
        self.forget_index()
        return self.atomic_types.pop()

    def remove(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        # resolve input
        resolved = resolve.resolve_type(typespec)

        # update self.atomic_types
        if isinstance(resolved, atomic.ScalarType):
            self.atomic_types.remove(resolved)
        else:
            for t in resolved:
                self.atomic_types.remove(t)

        # throw out index
        self.forget_index()

    def symmetric_difference(self, other: type_specifier) -> CompositeType:
        """Return a new CompositeType with types that are in either the
        original CompositeType or `other`, but not both.
        """
        resolved = CompositeType(resolve.resolve_type(other))
        return (self.difference(resolved)) | (resolved.difference(self))

    def symmetric_difference_update(self, other: type_specifier) -> None:
        """Update a CompositeType in-place, keeping only types that are found
        in either `self` or `other`, but not both.
        """
        self.atomic_types = self.symmetric_difference(other).atomic_types
        self.forget_index()

    def union(self, *others) -> CompositeType:
        """Return a new CompositeType with all the types from this
        CompositeType and all others.
        """
        return CompositeType(
            self.atomic_types.union(*[
                CompositeType(resolve.resolve_type(o)).atomic_types
                for o in others
            ])
        )

    def update(self, *others) -> None:
        """Update the CompositeType in-place, adding types from all others."""
        self.atomic_types = self.union(*others).atomic_types
        self.forget_index()

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __and__(self, other: type_specifier) -> CompositeType:
        """Return a new CompositeType containing the types common to `self` and
        all others.
        """
        return self.intersection(other)

    def __contains__(self, other: type_specifier) -> bool:
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        """Test whether `self` and `other` contain identical types."""
        resolved = CompositeType(resolve.resolve_type(other))
        return self.atomic_types == resolved.atomic_types

    def __ge__(self, other: type_specifier) -> bool:
        """Test whether every element in `other` is contained within `self`.
        """
        return self.issuperset(other)

    def __gt__(self, other: type_specifier) -> bool:
        """Test whether `self` is a proper superset of `other`
        (``self >= other and self != other``).
        """
        return self != other and self >= other

    def __iand__(self, other: type_specifier) -> CompositeType:
        """Update a CompositeType in-place, keeping only the types found in it
        and all others.
        """
        self.intersection_update(other)
        return self

    def __ior__(self, other: type_specifier) -> CompositeType:
        """Update a CompositeType in-place, adding types from all others."""
        self.update(other)
        return self

    def __isub__(self, other: type_specifier) -> CompositeType:
        """Update a CompositeType in-place, removing types that can be found in
        others.
        """
        self.difference_update(other)
        return self

    def __iter__(self):
        """Iterate through the types contained within a CompositeType."""
        return iter(self.atomic_types)

    def __ixor__(self, other: type_specifier) -> CompositeType:
        """Update a CompositeType in-place, keeping only types that are found
        in either `self` or `other`, but not both.
        """
        self.symmetric_difference_update(other)
        return self

    def __le__(self, other: type_specifier) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        return self.issubset(other)

    def __lt__(self, other: type_specifier) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        return self != other and self <= other

    def __len__(self):
        """Return the number of types in the CompositeType."""
        return len(self.atomic_types)

    def __or__(self, other: type_specifier) -> CompositeType:
        """Return a new CompositeType containing the types of `self` and all
        others.
        """
        return self.union(other)

    def __repr__(self) -> str:
        slugs = ", ".join(str(x) for x in self.atomic_types)
        return f"{type(self).__name__}({{{slugs}}})"

    def __str__(self) -> str:
        slugs = ", ".join(str(x) for x in self.atomic_types)
        return f"{{{slugs}}}"

    def __sub__(self, other: type_specifier) -> CompositeType:
        """Return a new CompositeType with types that are not in the others."""
        return self.difference(other)

    def __xor__(self, other: type_specifier) -> CompositeType:
        """Return a new CompositeType with types that are in either `self` or
        `other` but not both.
        """
        return self.symmetric_difference(other)
