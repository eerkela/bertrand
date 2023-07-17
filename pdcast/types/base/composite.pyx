"""This module describes a ``CompositeType`` object, which can be used to
group types into a set-like container.
"""
from typing import Iterator

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
        A :class:`Type <pdcast.Type>` or sequence of
        :class:`Types <pdcast.Type>` to use as elements.
    """

    def __init__(
        self,
        object types = None,
        np.ndarray index = None
    ):
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

    @property
    def index(self):
        """The inferred type at every index of a mixed iterable.

        Returns
        -------
        numpy.ndarray | None
            a 1D :class:`array <numpy.ndarray>` of
            :class:`ScalarTypes <pdcast.ScalarType>` contained within this
            composite if it was created by
            :func:`detect_type() <pdcast.detect_type>`.
            Otherwise, :data:`None <python:None>`.

        See Also
        --------
        detect_type : For more information on how this property is generated.

        Notes
        -----
        This attribute uses `Run Length Encoding (RLE)
        <https://en.wikipedia.org/wiki/Run-length_encoding>`_ to compress the
        inferred types.  This makes them extremely space efficient, especially
        for large arrays with elements of repeated type.

        Additionally, since :class:`ScalarTypes <pdcast.ScalarType>` are
        :ref:`flyweights <ScalarType.constructors>`, multiple runs can
        reference the same object in memory.  They are thus efficient even in
        the worst case, where runs start and stop frequently.

        Examples
        --------
        .. doctest::

            >>> vector = [0, True, 2, 3, 4.0, 5]
            >>> mixed = pdcast.detect_type(vector)
            >>> mixed   # doctest: +SKIP
            CompositeType({int[python], bool[python], float64[python]})
            >>> mixed.index
            array([PythonIntegerType(), PythonBooleanType(), PythonIntegerType(),
                   PythonIntegerType(), PythonFloatType(), PythonIntegerType()],
                  dtype=object)

        .. note::

            This index can be used for :meth:`groupby() <pandas.Series.groupby>`
            operations on the original sequence.

            .. doctest::

                >>> import pandas as pd

                >>> groups = pd.Series(vector, dtype=object).groupby(mixed.index)
                >>> for _, group in groups:
                ...     print(group)
                1    True
                dtype: object
                4    4.0
                dtype: object
                0    0
                2    2
                3    3
                5    5
                dtype: object
        """
        if self._index is None:
            return None

        return np.repeat(self._index["value"], self._index["count"])

    cdef void forget_index(self):
        self._index = None

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
        Type.from_string : For more information on how this method is called.

        Examples
        --------
        :class:`CompositeTypes <pdcast.CompositeType>` support the addition of
        dynamic aliases at runtime.  These can be used to 'pin' specific
        composites by name, allowing users to refer to them directly.

        .. doctest::

            >>> numeric = resolve_type(["bool", "int", "float", "complex"])
            >>> numeric.aliases.add("numeric")
            >>> resolve_type("numeric")   # doctest: +SKIP
            CompositeType({bool, int, float, complex})
            >>> resolve_type("numeric") is numeric
            True

        If the alias is removed, then the composite will be inaccessible.

        .. doctest::

            >>> numeric.aliases.remove("numeric")
            >>> resolve_type("numeric")
            Traceback (most recent call last):
                ...
            ValueError: invalid specifier: 'numeric'
        """
        if args:
            raise TypeError("CompositeTypes cannot be parametrized")

        return self

    ##########################
    ####    MEMBERSHIP    ####
    ##########################

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
        Type.contains : For more information on how this method is called.

        Examples
        --------
        This method extends membership tests to the whole composite, rather
        than its individual components.  It returns ``True`` if the given type
        forms a subset of the composite.

        .. doctest::

            >>> resolve_type("int, float, complex").contains("complex64")
            True
            >>> resolve_type("int, float, complex").contains("int64, complex64")
            True
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(other_typ) for other_typ in other)

        return any(typ.contains(other) for typ in self)

    ###########################
    ####    HIERARCHIES    ####
    ###########################

    def expand(self) -> CompositeType:
        """Expand the contained types to include their full hierarchies.

        Returns
        -------
        CompositeType
            A copy of this composite with the full contents of every type's
            hierarchy.

        See Also
        --------
        CompositeType.collapse : Inverse of this method.

        Notes
        -----
        By default, :class:`CompositeTypes <pdcast.CompositeType>` implicitly
        contain the full hierarchies of each of their members.  This method
        makes these explicit by expanding the composite to include all the
        :meth:`subtypes <pdcast.AbstractType.subtype>` and
        :meth:`implementations <pdcast.AbstractType.implementation>` for each
        member.

        Examples
        --------
        .. doctest::

            >>> resolve_type(["bool"]).expand()   # doctest: +SKIP
            CompositeType({bool, bool[numpy], bool[pandas], bool[python]})
        """
        cdef CompositeType result
        cdef VectorType original
        cdef VectorType typ

        result = self.copy()
        for original in self:
            for typ in original.implementations.values():
                result.add(typ)

        for typ in result.copy():
            result |= traverse_subtypes(typ)

        return result

    def collapse(self) -> CompositeType:
        """Strip redundant types from this composite.

        Returns
        -------
        CompositeType
            A copy of this composite without any redundant types.

        See Also
        --------
        CompositeType.expand : Inverse of this method.

        Notes
        -----
        By default, :class:`CompositeTypes <pdcast.CompositeType>` implicitly
        contain the full hierarchies of each of their members.  This method
        utilizes this to remove any types that are fully contained within
        another type's hierarchy from the composite.  The result is a
        composite that is functionally equivalent to the original, but without
        any redundant members.

        Examples
        --------
        .. doctest::

            >>> resolve_type("bool, bool[numpy], bool[pandas], bool[python]").collapse()
            CompositeType({bool})
        """
        cdef VectorType atomic_type
        cdef VectorType t

        # for every type a in self, check if there is another type b such that
        # a != b and b contains a.  If true, the type is redundant.
        return CompositeType(
            a for a in self
            if not any(a != b and a in b for b in self.types)
        )

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(self, other: type_specifier) -> None:
        """Add a type to the composite.

        This is equivalent to :meth:`set.add() <python:set.add>`.

        Parameters
        ----------
        other : type_specifier
            The type to add.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        See Also
        --------
        CompositeType.remove : Remove a type from the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float, complex")
            >>> composite.add("bool")
            >>> composite   # doctest: +SKIP
            CompositeType({int, float, complex, bool})
        """
        cdef CompositeType resolved = resolve_type([other])
        cdef VectorType typ

        self.types.update(typ for typ in resolved)
        self.forget_index()

    def remove(self, other: type_specifier) -> None:
        """Remove a type from the composite.

        This is equivalent to :meth:`set.remove() <python:set.remove>`.

        Parameters
        ----------
        other : type_specifier
            The type to remove.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`, and may reference
            an implicit type in this composite's shared hierarchy.

        Raises
        ------
        KeyError
            If the type is not contained in the composite.

        See Also
        --------
        CompositeType.add : Add a type to the composite.
        CompositeType.discard : Remove a type from the composite if it is
            present.
        CompositeType.pop : Remove and return an arbitrary type from the
            composite.
        CompositeType.clear : Remove all types from the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float, complex")
            >>> composite.remove("int")
            >>> composite   # doctest: +SKIP
            CompositeType({float, complex})
            >>> composite.remove("float16")   # implicit
            >>> composite   # doctest: +SKIP
            CompositeType({float32, float64, float80, complex})
            >>> composite.remove("bool")  # not present
            Traceback (most recent call last):
                ...
            KeyError: 'bool'
        """
        cdef CompositeType resolved = resolve_type([other])

        if not self.contains(resolved):
            raise KeyError(other)

        cdef CompositeType result = self - resolved

        self.types = result.types
        self.forget_index()

    def discard(self, other: type_specifier) -> None:
        """Remove a type specifier from the composite if it is present.

        This is equivalent to :meth:`set.discard() <python:set.discard>`.

        Parameters
        ----------
        other : type_specifier
            The type to remove.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`, and may reference
            an implicit type in this composite's shared hierarchy.

        See Also
        --------
        CompositeType.add : Add a type to the composite.
        CompositeType.remove : Remove a type from the composite.
        CompositeType.pop : Remove and return an arbitrary type from the
            composite.
        CompositeType.clear : Remove all types from the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float, complex")
            >>> composite.discard("int")
            >>> composite   # doctest: +SKIP
            CompositeType({float, complex})
            >>> composite.discard("float16")   # implicit
            >>> composite   # doctest: +SKIP
            CompositeType({float32, float64, float80, complex})
            >>> composite.discard("bool")   # not present
        """
        try:
            self.remove(other)
        except KeyError:
            pass

    def pop(self) -> VectorType:
        """Remove and return an arbitrary type from the composite.

        This is equivalent to :meth:`set.pop() <python:set.pop>`.

        Returns
        -------
        VectorType
            The type that was removed.

        Raises
        ------
        KeyError
            If the composite is empty.

        See Also
        --------
        CompositeType.add : Add a type to the composite.
        CompositeType.remove : Remove a type from the composite.
        CompositeType.discard : Remove a type from the composite if it is
            present.
        CompositeType.clear : Remove all types from the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.pop()
            int
            >>> composite   # doctest: +SKIP
            CompositeType({float, complex})
            >>> composite.pop()
            float
            >>> composite   # doctest: +SKIP
            CompositeType({})
            >>> composite.pop()   # empty
            Traceback (most recent call last):
                ...
            KeyError: 'pop from an empty set'
        """
        self.forget_index()
        return self.types.pop()

    def clear(self) -> None:
        """Remove all types from the composite.

        This is equivalent to :meth:`set.clear() <python:set.clear>`.

        See Also
        --------
        CompositeType.add : Add a type to the composite.
        CompositeType.remove : Remove a type from the composite.
        CompositeType.discard : Remove a type from the composite if it is
            present.
        CompositeType.pop : Remove and return an arbitrary type from the
            composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float, complex")
            >>> composite.clear()
            >>> composite   # doctest: +SKIP
            CompositeType({})
        """
        self.types.clear()
        self.forget_index()

    def copy(self) -> CompositeType:
        """Return a shallow copy of the composite.

        This is equivalent to :meth:`set.copy() <python:set.copy>`.

        Returns
        -------
        CompositeType
            A shallow copy of the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float, complex")
            >>> composite2 = composite.copy()
            >>> composite2.add("bool")
            >>> composite   # doctest: +SKIP
            CompositeType({int, float, complex})
            >>> composite2   # doctest: +SKIP
            CompositeType({int, float, complex, bool})
        """
        return CompositeType(self)

    ##########################
    ####    OPERATIONS    ####
    ##########################

    def union(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with the
        contents of this and all others.

        This is equivalent to :meth:`set.union() <python:set.union>`.

        Parameters
        ----------
        *others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with the contents of this and all others.

        See Also
        --------
        CompositeType.intersection : Return a new composite with types in
            common to this and all others.
        CompositeType.difference : Return a new composite with types that are
            not in any of the others.
        CompositeType.symmetric_difference : Return a new composite with types
            that are in exactly one of this and the others.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.union("float, complex")   # doctest: +SKIP
            CompositeType({int, float, complex})
        """
        cdef CompositeType result

        result = self.copy()
        for other in others:
            result |= other
        return result

    def intersection(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with
        types in common to this and all others.

        This is equivalent to
        :meth:`set.intersection() <python:set.intersection>`.

        Parameters
        ----------
        *others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types in common to this and all others.

        See Also
        --------
        CompositeType.union : Return a new composite with the contents of this
            and all others.
        CompositeType.difference : Return a new composite with types that are
            not in any of the others.
        CompositeType.symmetric_difference : Return a new composite with types
            that are in exactly one of this and the others.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.intersection("float, complex")   # doctest: +SKIP
            CompositeType({float})
        """
        cdef CompositeType result

        result = self.copy()
        for other in others:
            result &= other
        return result

    def difference(self, *others: type_specifier) -> CompositeType:
        """Return a new :class:`CompositeType <pdcast.CompositeType>` with
        types that are not in any of the others.

        This is equivalent to :meth:`set.difference() <python:set.difference>`.

        Parameters
        ----------
        *others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types that are not in any of the others.

        See Also
        --------
        CompositeType.union : Return a new composite with the contents of this
            and all others.
        CompositeType.intersection : Return a new composite with types in
            common to this and all others.
        CompositeType.symmetric_difference : Return a new composite with types
            that are in exactly one of this and the others.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.difference("float, complex")   # doctest: +SKIP
            CompositeType({int})
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
        types that are in either this composite or ``other``, but not both.

        This is equivalent to
        :meth:`set.symmetric_difference() <python:set.symmetric_difference>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types that are in exactly one of this and
            ``other``.

        See Also
        --------
        CompositeType.union : Return a new composite with the contents of this
            and all others.
        CompositeType.intersection : Return a new composite with types in
            common to this and all others.
        CompositeType.difference : Return a new composite with types that are
            not in any of the others.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.symmetric_difference("float, complex")   # doctest: +SKIP
            CompositeType({int, complex})
        """
        return self ^ other

    def __or__(self, other: type_specifier) -> CompositeType:
        """Return the union of two
        :class:`CompositeTypes <pdcast.CompositeType>`.

        This is equivalent to :meth:`set.__or__() <python:set.__or__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with the contents of this and ``other``.

        See Also
        --------
        CompositeType.__and__ : Return a new composite with types in common to
            this and ``other``.
        CompositeType.__sub__ : Return a new composite with types that are not
            in ``other``.
        CompositeType.__xor__ : Return a new composite with types that are in
            exactly one of this and ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite | {"float", "complex"}   # doctest: +SKIP
            CompositeType({int, float, complex})
        """
        return CompositeType(self.types | resolve_type([other]).types)

    def __and__(self, other: type_specifier) -> CompositeType:
        """Return the intersection of two
        :class:`CompositeTypes <pdcast.CompositeType>`.

        This is equivalent to :meth:`set.__and__() <python:set.__and__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types in common to this and ``other``.

        See Also
        --------
        CompositeType.__or__ : Return a new composite with the contents of this
            and ``other``.
        CompositeType.__sub__ : Return a new composite with types that are not
            in ``other``.
        CompositeType.__xor__ : Return a new composite with types that are in
            exactly one of this and ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite & {"float", "complex"}   # doctest: +SKIP
            CompositeType({float})
        """
        cdef CompositeType resolved = resolve_type([other])
        cdef VectorType typ
        cdef set forward
        cdef set backward

        forward = {typ for typ in self if typ in resolved}
        backward = {typ for typ in resolved if typ in self}
        return CompositeType(forward | backward)

    def __sub__(self, other: type_specifier) -> CompositeType:
        """Return the difference of two
        :class:`CompositeTypes <pdcast.CompositeType>`.

        This is equivalent to :meth:`set.__sub__() <python:set.__sub__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types that are not in ``other``.

        See Also
        --------
        CompositeType.__or__ : Return a new composite with the contents of this
            and ``other``.
        CompositeType.__and__ : Return a new composite with types in common to
            this and ``other``.
        CompositeType.__xor__ : Return a new composite with types that are in
            exactly one of this and ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite - {"float", "complex"}   # doctest: +SKIP
            CompositeType({int})
        """
        cdef CompositeType result = self.expand()
        cdef CompositeType resolved = resolve_type([other]).expand()
        cdef VectorType typ
        cdef VectorType typ2

        has_child = lambda typ: any(typ2 in typ for typ2 in resolved)
        result = CompositeType(typ for typ in result if not has_child(typ))

        return result.collapse()

    def __xor__(self, other: type_specifier) -> CompositeType:
        """Return the symmetric difference of two
        :class:`CompositeTypes <pdcast.CompositeType>`.

        This is equivalent to :meth:`set.__xor__() <python:set.__xor__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            A new composite with types that are in exactly one of this and
            ``other``.

        See Also
        --------
        CompositeType.__or__ : Return a new composite with the contents of this
            and ``other``.
        CompositeType.__and__ : Return a new composite with types in common to
            this and ``other``.
        CompositeType.__sub__ : Return a new composite with types that are not
            in ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite ^ {"float", "complex"}   # doctest: +SKIP
            CompositeType({int, complex})
        """
        cdef CompositeType resolved = resolve_type([other])

        return (self - resolved) | (resolved - self)

    ###################################
    ####    IN-PLACE OPERATIONS    ####
    ###################################

    def update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        adding the contents of all others.

        This is equivalent to :meth:`set.update() <python:set.update>`.

        Parameters
        ----------
        others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        See Also
        --------
        CompositeType.intersection_update : Update this composite, keeping only
            types found in all others.
        CompositeType.difference_update : Update this composite, removing
            types found in any of the others.
        CompositeType.symmetric_difference_update : Update this composite,
            keeping types that are found in it or any of the others, but not
            both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.update({"float", "complex"})
            >>> composite   # doctest: +SKIP
            CompositeType({int, float, complex})
        """
        for other in others:
            self |= other

    def intersection_update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        keeping types found in it and all others.

        This is equivalent to
        :meth:`set.intersection_update() <python:set.intersection_update>`.

        Parameters
        ----------
        others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        See Also
        --------
        CompositeType.update : Update this composite, adding the contents of
            all others.
        CompositeType.difference_update : Update this composite, removing
            types found in any of the others.
        CompositeType.symmetric_difference_update : Update this composite,
            keeping types that are found in it or any of the others, but not
            both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.intersection_update({"float", "complex"})
            >>> composite   # doctest: +SKIP
            CompositeType({float})
        """
        for other in others:
            self &= other

    def difference_update(self, *others: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        removing types from any of the others.

        This is equivalent to
        :meth:`set.difference_update() <python:set.difference_update>`.

        Parameters
        ----------
        others : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        See Also
        --------
        CompositeType.update : Update this composite, adding the contents of
            all others.
        CompositeType.intersection_update : Update this composite, keeping only
            types found in all others.
        CompositeType.symmetric_difference_update : Update this composite,
            keeping types that are found in it or any of the others, but not
            both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.difference_update({"float", "complex"})
            >>> composite   # doctest: +SKIP
            CompositeType({int})
        """
        for other in others:
            self -= other

    def symmetric_difference_update(self, other: type_specifier) -> None:
        """Update this :class:`CompositeType <pdcast.CompositeType>` in-place,
        keeping types that are found in it or ``other``, but not both.

        This is equivalent to
        :meth:`set.symmetric_difference_update() <python:set.symmetric_difference_update>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        See Also
        --------
        CompositeType.update : Update this composite, adding the contents of
            all others.
        CompositeType.intersection_update : Update this composite, keeping only
            types found in all others.
        CompositeType.difference_update : Update this composite, removing
            types found in any of the others.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.symmetric_difference_update({"float", "complex"})
            >>> composite   # doctest: +SKIP
            CompositeType({int, complex})
        """
        self ^= other

    def __ior__(self, other: type_specifier) -> CompositeType:
        """Merge the contents of another
        :class:`CompositeType <pdcast.CompositeType>` into this one.

        This is equivalent to :meth:`set.__ior__() <python:set.__ior__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            This composite, after merging the contents of ``other``.

        See Also
        --------
        CompositeType.__iand__ : Keep only types found in both this and another
            composite.
        CompositeType.__isub__ : Remove types found in another composite from
            this one.
        CompositeType.__ixor__ : Keep only types found in either this or
            another composite, but not both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite |= {"float", "complex"}
            >>> composite   # doctest: +SKIP
            CompositeType({int, float, complex})
        """
        result = self | other
        self.types = result.types
        return self

    def __iand__(self, other: type_specifier) -> CompositeType:
        """Keep only types found in both this and another
        :class:`CompositeType <pdcast.CompositeType>`.

        This is equivalent to :meth:`set.__iand__() <python:set.__iand__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            This composite, after keeping only types found in both it and
            ``other``.

        See Also
        --------
        CompositeType.__ior__ : Merge the contents of another composite into
            this one.
        CompositeType.__isub__ : Remove types found in another composite from
            this one.
        CompositeType.__ixor__ : Keep only types found in either this or
            another composite, but not both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite &= {"float", "complex"}
            >>> composite   # doctest: +SKIP
            CompositeType({float})
        """
        result = self & other
        self.types = result.types
        return self

    def __isub__(self, other: type_specifier) -> CompositeType:
        """Remove types found in another
        :class:`CompositeType <pdcast.CompositeType>` from this one.

        This is equivalent to :meth:`set.__isub__() <python:set.__isub__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            This composite, after removing types found in ``other``.

        See Also
        --------
        CompositeType.__ior__ : Merge the contents of another composite into
            this one.
        CompositeType.__iand__ : Keep only types found in both this and another
            composite.
        CompositeType.__ixor__ : Keep only types found in either this or
            another composite, but not both.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite -= {"float", "complex"}
            >>> composite   # doctest: +SKIP
            CompositeType({int})
        """
        result = self - other
        self.types = result.types
        return self

    def __ixor__(self, other: type_specifier) -> CompositeType:
        """Keep only types found in either this or another
        :class:`CompositeType <pdcast.CompositeType>`, but not both.

        This is equivalent to :meth:`set.__ixor__() <python:set.__ixor__>`.

        Parameters
        ----------
        other : type_specifier
            The other types to include in the composite.  These can be in any
            format recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        CompositeType
            This composite, after keeping only types found in either it or
            ``other``, but not both.

        See Also
        --------
        CompositeType.__ior__ : Merge the contents of another composite into
            this one.
        CompositeType.__iand__ : Keep only types found in both this and another
            composite.
        CompositeType.__isub__ : Remove types found in another composite from
            this one.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite ^= {"float", "complex"}
            >>> composite   # doctest: +SKIP
            CompositeType({int, complex})
        """
        result = self ^ other
        self.types = result.types
        return self

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def issubset(self, other: type_specifier) -> bool:
        """Check whether every element in ``self`` is contained in ``other``.

        This is equivalent to :meth:`set.issubset() <python:set.issubset>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if every element in ``self`` is contained in ``other``.
            ``False`` otherwise.

        See Also
        --------
        CompositeType.issuperset : Check whether every element in ``other`` is
            contained in ``self``.
        CompositeType.isdisjoint : Check whether ``self`` and ``other`` have no
            elements in common.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.issubset({"int", "float", "complex"})
            True
            >>> composite.issubset({"int", "complex"})
            False
        """
        return resolve_type([other]).contains(self)

    def issuperset(self, other: type_specifier) -> bool:
        """Check whether every element in ``other`` is contained in ``self``.

        This is equivalent to :meth:`set.issuperset() <python:set.issuperset>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if every element in ``other`` is contained in ``self``.
            ``False`` otherwise.

        See Also
        --------
        CompositeType.issubset : Check whether every element in ``self`` is
            contained in ``other``.
        CompositeType.isdisjoint : Check whether ``self`` and ``other`` have no
            elements in common.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.issuperset({"int"})
            True
            >>> composite.issuperset({"int", "complex"})
            False
        """
        return self.contains(other)

    def isdisjoint(self, other: type_specifier) -> bool:
        """Check whether ``self`` and ``other`` have no elements in common.

        This is equivalent to :meth:`set.isdisjoint() <python:set.isdisjoint>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``self`` and ``other`` have no elements in common.
            ``False`` otherwise.

        See Also
        --------
        CompositeType.issubset : Check whether every element in ``self`` is
            contained in ``other``.
        CompositeType.issuperset : Check whether every element in ``other`` is
            contained in ``self``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite.isdisjoint("complex")
            True
            >>> composite.isdisjoint("int, complex")
            False
        """
        return not self & other

    def __gt__(self, other: type_specifier) -> bool:
        """Test whether ``self`` is a proper superset of ``other``
        (``self >= other and self != other``).

        This is equivalent to :meth:`set.__gt__() <python:set.__gt__>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``self`` is a proper superset of ``other``.
            ``False`` otherwise.

        See Also
        --------
        CompositeType.__ge__ : Test whether every element in ``other`` is
            contained within ``self``.
        CompositeType.__lt__ : Test whether ``self`` is a proper subset of
            ``other``.
        CompositeType.__le__ : Test whether every element in ``self`` is
            contained within ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite > {"int"}
            True
            >>> composite > {"int", "float"}
            False
        """
        cdef CompositeType resolved = resolve_type([other])

        return self != resolved and self >= resolved

    def __ge__(self, other: type_specifier) -> bool:
        """Test whether every element in ``other`` is contained within ``self``.

        This is equivalent to :meth:`set.__ge__() <python:set.__ge__>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if every element in ``other`` is contained within
            ``self``.  ``False`` otherwise.

        See Also
        --------
        CompositeType.__gt__ : Test whether ``self`` is a proper superset of
            ``other``.
        CompositeType.__lt__ : Test whether ``self`` is a proper subset of
            ``other``.
        CompositeType.__le__ : Test whether every element in ``self`` is
            contained within ``other``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite >= {"int"}
            True
            >>> composite >= {"int", "float"}
            True
        """
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        """Test whether ``self`` and ``other`` contain identical types.

        This is equivalent to :meth:`set.__eq__() <python:set.__eq__>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``self`` and ``other`` contain identical types.
            ``False`` otherwise.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite == {"int", "float"}
            True
            >>> composite == {"int", "complex"}
            False
        """
        return self.types == resolve_type([other]).types

    def __lt__(self, other: type_specifier) -> bool:
        """Test whether ``self`` is a proper subset of ``other``
        (``self <= other and self != other``).

        This is equivalent to :meth:`set.__lt__() <python:set.__lt__>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if ``self`` is a proper subset of ``other``.
            ``False`` otherwise.

        See Also
        --------
        CompositeType.__le__ : Test whether every element in ``self`` is
            contained within ``other``.
        CompositeType.__gt__ : Test whether ``self`` is a proper superset of
            ``other``.
        CompositeType.__ge__ : Test whether every element in ``other`` is
            contained within ``self``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite < {"int", "float", "complex"}
            True
            >>> composite < {"int", "float"}
            False
        """
        cdef CompositeType resolved = resolve_type([other])

        return self != resolved and self <= resolved

    def __le__(self, other: type_specifier) -> bool:
        """Test whether every element in ``self`` is contained within ``other``.

        This is equivalent to :meth:`set.__le__() <python:set.__le__>`.

        Parameters
        ----------
        other : type_specifier
            The types to compare against.  These can be in any format
            recognized by :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if every element in ``self`` is contained within
            ``other``.  ``False`` otherwise.

        See Also
        --------
        CompositeType.__lt__ : Test whether ``self`` is a proper subset of
            ``other``.
        CompositeType.__gt__ : Test whether ``self`` is a proper superset of
            ``other``.
        CompositeType.__ge__ : Test whether every element in ``other`` is
            contained within ``self``.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> composite <= {"int", "float", "complex"}
            True
            >>> composite <= {"int", "float"}
            True
        """
        return resolve_type(other).contains(self)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __bool__(self) -> bool:
        """Check if this :class:`CompositeType <pdcast.CompositeType>` is empty.

        Returns
        -------
        bool
            ``True`` if this composite is empty.  ``False`` otherwise.

        Examples
        --------
        .. doctest::

            >>> bool(resolve_type("int, float"))
            True
            >>> bool(resolve_type([]))
            False
        """
        return bool(self.types)

    def __contains__(self, other: type_specifier) -> bool:
        """Check if the composite contains a given type.

        This is equivalent to ``self.contains(other)``.

        Parameters
        ----------
        other : type_specifier
            The type to check for.  This can be in any format recognized by
            :func:`resolve_type() <pdcast.resolve_type>`.

        Returns
        -------
        bool
            ``True`` if the composite contains the type.  ``False`` otherwise.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> "int" in composite
            True
            >>> "complex" in composite
            False
        """
        return self.contains(other)

    def __iter__(self) -> Iterator[VectorType]:
        """Iterate through a :class:`CompositeType <pdcast.CompositeType>`.

        Returns
        -------
        Iterator[VectorType]
            An iterator over the types in the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> [typ for typ in composite]
            [FloatType(), IntegerType()]
        """
        return iter(self.types)

    def __len__(self) -> int:
        """Return the number of types in the composite.

        Returns
        -------
        int
            The number of types in the composite.

        Examples
        --------
        .. doctest::

            >>> composite = resolve_type("int, float")
            >>> len(composite)
            2
        """
        return len(self.types)

    def __str__(self) -> str: 
        return f"{{{', '.join(str(x) for x in self.types)}}}"

    def __repr__(self) -> str:
        cdef str slugs = ", ".join(str(x) for x in self.types)
        return f"{type(self).__name__}({{{slugs}}})"

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
