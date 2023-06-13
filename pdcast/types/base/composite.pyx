"""This module describes a ``CompositeType`` object, which can be used to
group types into a set-like container.
"""
cimport numpy as np
import numpy as np

from pdcast.resolve import resolve_type
from pdcast.util.type_hints import type_specifier

from .vector cimport VectorType
from .scalar cimport ScalarType
from .registry cimport AliasManager, Type


cdef class CompositeType(Type):
    """:class:`set <python:set>`-like container for ``pdcast`` type objects.

    :class:`CompositeTypes <pdcast.CompositeType>` describe collections of
    types.  They can be created by providing multiple type specifiers in a call
    to :func:`resolve_type() <pdcast.resolve_type>` or a vector with mixed
    elements in a call to :func:`detect_type() <pdcast.detect_type>`.

    Parameters
    ----------
    types : Type | Iterable[Type]
        A sequence of :class:`Types <pdcast.Type>` to use as elements.
    """

    def __init__(
        self,
        object types = None,
        ScalarType[:] index = None
    ):
        """TODO"""
        super().__init__()  # init aliases

        if types is None:
            self.types = set()

        elif isinstance(types, VectorType):
            self.types = {types}

        elif isinstance(types, CompositeType):
            self.types = types.types.copy()
            if index is None:
                index = types._index

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

        self._index = index

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, *args: str) -> CompositeType:
        """Construct a :class:`CompositeType <pdcast.CompositeType>` from a
        string in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Parameters
        ----------
        *args : str
            Positional arguments supplied to this type.  These must be empty.

        Returns
        -------
        Type
            A named :class:`CompositeType <pdcast.CompositeType>`.

        Raises
        ------
        TypeError
            If any parametrized arguments are given.

        See Also
        --------
        Type.from_string :
            For more information on how this method is called.

        Examples
        --------
        :class:`CompositeTypes <pdcast.CompositeType>` support the addition of
        dynamic aliases at runtime.  These can be used to 'pin' specific
        composites by name, allowing users to refer to them directly.

        .. doctest::

            >>> numeric = pdcast.resolve_type(["bool", "int", "float", "complex"])
            >>> numeric.aliases.add("numeric")
            >>> pdcast.resolve_type("numeric")   # doctest: +SKIP
            CompositeType({bool, int, float, complex})
            >>> pdcast.resolve_type("numeric") is numeric
            True

        If the alias is removed, then the composite will be inaccessible.

        .. doctest::

            >>> numeric.aliases.remove("numeric")
            >>> pdcast.resolve_type("numeric")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'numeric'
        """
        if args:
            raise TypeError("CompositeTypes cannot be parametrized")

        return self

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    @property
    def index(self) -> np.ndarray:
        """TODO"""
        return np.asarray(self._index, dtype=object)

    cdef void forget_index(self):
        self._index = None

    def expand(self, expand_generics: bool = True) -> CompositeType:
        """Expand the contained types to include each of their subtypes."""
        cdef CompositeType result
        cdef VectorType original
        cdef VectorType typ

        result = self.copy()
        if expand_generics:
            for original in self:
                for typ in original.backends.values():
                    result.add(typ)

        for typ in result.copy():
            result |= traverse_subtypes(typ)

        return result

    def collapse(self) -> CompositeType:
        """Return a copy with redundant subtypes removed.  A subtype is
        redundant if it is fully encapsulated within the other members of the
        CompositeType.
        """
        cdef VectorType atomic_type
        cdef VectorType t

        # for every type a in self, check if there is another type b such that
        # a != b and b contains a.  If true, the type is redundant.
        return CompositeType(
            a for a in self
            if not any(a != b and a in b for b in self.types)
        )

    def contains(self, other: type_specifier) -> bool:
        """Check whether ``other`` is a member of the composite or any of its
        hierarchies.

        Parameters
        ----------
        other : type_specifier
            The type to check for.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``other`` is a member of the composite hierarchy.
            ``False`` otherwise.

        See Also
        --------
        Type.contains :
            For more information on how this method is called.

        Examples
        --------
        This method extends membership tests to the whole composite, rather
        than its individual components.  It returns ``True`` if the given type
        forms a subset of the composite.

        .. doctest::

            >>> pdcast.resolve_type("int, float, complex").contains("complex64")
            True
            >>> pdcast.resolve_type("int, float, complex").contains("int64, complex64")
            True
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(other_typ) for other_typ in other)

        return any(typ.contains(other) for typ in self)

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(self, typespec: type_specifier) -> None:
        """Add a type specifier to the CompositeType."""
        cdef CompositeType other
        cdef VectorType typ

        other = resolve_type([typespec])
        self.types.update(typ for typ in other)
        self.forget_index()

    def remove(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType.  Raises a
        KeyError if `typespec` is not contained in the set.
        """
        cdef CompositeType other
        cdef VectorType typ

        other = resolve_type([typespec])
        for typ in other:
            self.types.remove(typ)

        self.forget_index()

    def discard(self, typespec: type_specifier) -> None:
        """Remove the given type specifier from the CompositeType if it is
        present.
        """
        cdef CompositeType other
        cdef VectorType typ

        other = resolve_type([typespec])
        for typ in other:
            self.types.discard(typ)

        self.forget_index()

    def pop(self) -> VectorType:
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
        cdef VectorType typ

        result = self.expand()

        has_child = lambda typ: any(other_type in typ for other_type in other)
        for item in others:
            other = resolve_type([item]).expand()
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

        other = resolve_type([typespec])
        return CompositeType(self.types | other.types)

    def __and__(self, typespec: type_specifier) -> CompositeType:
        """Return the intersection of two CompositeTypes."""
        cdef CompositeType other
        cdef VectorType typ
        cdef set forward
        cdef set backward

        other = resolve_type([typespec])
        forward = {typ for typ in self if typ in other}
        backward = {typ for typ in other if typ in self}
        return CompositeType(forward | backward)

    def __sub__(self, typespec: type_specifier) -> CompositeType:
        """Return the difference of two CompositeTypes."""
        cdef CompositeType result
        cdef CompositeType other
        cdef VectorType typ
        cdef VectorType other_type

        result = self.expand()

        other = resolve_type([typespec]).expand()
        has_child = lambda typ: any(other_type in typ for other_type in other)
        result = CompositeType(typ for typ in result if not has_child(typ))

        return result.collapse()

    def __xor__(self, typespec: type_specifier) -> CompositeType:
        """Return the symmetric difference of two CompositeTypes."""
        cdef CompositeType other

        other = resolve_type([typespec])
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

    def __ior__(self, other: type_specifier) -> CompositeType:
        """Merge the contents of another
        :class:`CompositeType <pdcast.CompositeType>` into this one.
        """
        result = self | other
        self.types = result.types
        return self

    def __iand__(self, other: type_specifier) -> CompositeType:
        """Keep only types found in both this and another
        :class:`CompositeType <pdcast.CompositeType>`.
        """
        result = self & other
        self.types = result.types
        return self

    def __isub__(self, other: type_specifier) -> CompositeType:
        """Remove types found in another
        :class:`CompositeType <pdcast.CompositeType>` from this one.
        """
        result = self - other
        self.types = result.types
        return self

    def __ixor__(self, other: type_specifier) -> CompositeType:
        """Keep only types found in either this or another
        :class:`CompositeType <pdcast.CompositeType>`, but not both.
        """
        result = self ^ other
        self.types = result.types
        return self

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

        other = resolve_type(typespec)
        return self != other and self >= other

    def __ge__(self, typespec: type_specifier) -> bool:
        """Test whether every element in `other` is contained within `self`.
        """
        return self.contains(typespec)

    def __eq__(self, typespec: type_specifier) -> bool:
        """Test whether `self` and `other` contain identical types."""
        cdef CompositeType other

        other = resolve_type([typespec])
        return self.types == other.types

    def __lt__(self, typespec: type_specifier) -> bool:
        """Test whether `self` is a proper subset of `other`
        (``self <= other and self != other``).
        """
        cdef Type other

        other = resolve_type(typespec)
        return self != other and self <= other

    def __le__(self, typespec: type_specifier) -> bool:
        """Test whether every element in `self` is contained within `other`."""
        return resolve_type(typespec).contains(self)

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


cdef void _traverse_subtypes(
    VectorType typ,
    set result
):
    """Recursive inner function for `traverse_subtypes()`."""
    cdef CompositeType subtypes = typ.subtypes

    result.update(typ.subtypes)
    for subtype in subtypes:
        _traverse_subtypes(subtype, result)


cdef CompositeType traverse_subtypes(VectorType typ):
    """Traverse through a scalar type's subtype tree, recursively flattening
    all its children.
    """
    cdef set result = set()

    _traverse_subtypes(typ, result)
    return CompositeType(result)
