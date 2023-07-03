"""This module describes an ``ScalarType`` object, which serves as the base
of the ``pdcast`` type system.
"""
import decimal
import inspect
from types import MappingProxyType
from typing import Any
import warnings

cimport numpy as np
import numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util.type_hints import (
    array_like, dtype_like, numeric, type_specifier
)

from .registry cimport CacheValue, TypeRegistry
from .vector cimport READ_ONLY_ERROR, VectorType, BackendEncoder
from .decorator cimport DecoratorType
from .composite cimport CompositeType
from ..array import construct_object_dtype


# TODO: when checking for interface matches in AbstractType, call
# object.__getattribute__ on every name in dir() to find out which ones are
# overridden on the abstract type.  Then, only check for interface matches on
# this subset.


######################
####    SCALAR    ####
######################


cdef class ScalarType(VectorType):
    """Base class for all user-defined scalar types.

    :class:`ScalarTypes <pdcast.ScalarType>` are the most fundamental unit of
    the ``pdcast`` type system.  They form the leaves of a
    :ref:`type hierarchy <AbstractType.hierarchy>` and describe concrete values
    of a particular type (i.e. :class:`int <python:int>`,
    :class:`numpy.float32`, etc.).

    Parameters
    ----------
    **kwargs : dict
        Parametrized keyword arguments describing metadata for this type.  This
        is conceptually equivalent to the ``_metadata`` field of pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.

    See Also
    --------
    AbstractType : Base class for parent nodes in an abstract hierarchy.
    """

    # NOTE: this is a sample __init__ method for a parametrized type.
    # Non-parametrized types can omit this method entirely.

    # def __init__(self, foo=None, bar="yes"):
    #     if foo is not None:
    #         foo = int(foo)
    #     if bar not in {"yes", "no", "maybe"}:
    #         raise TypeError()
    #     super().__init__(foo=foo, bar=bar)

    # This automatically assigns foo and bar as parametrized attributes of
    # the inheriting type and caches any instances that are generated using
    # them.  They must be optional, and the type must be constructable from
    # their default values to be considered valid.
    
    # NOTE: we don't have to explicitly assign self.foo/bar ourselves;
    # super().__init__() does this for us.  It also makes the type read-only,
    # so any attribute assignment after it is called will fail.

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, *args: str) -> ScalarType:
        """Construct a :class:`ScalarType <pdcast.ScalarType>` from a string in
        the :ref:`type specification mini-language <resolve_type.mini_language>`.

        Parameters
        ----------
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the :ref:`type
            specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_string : For more information on how this method is called.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> resolve_type("int") is resolve_type("int")
            True

        If a type defines a :attr:`_cache_size` attribute, then only a fixed
        number of instances will be cached, implementing a Least Recently Used
        (LRU) caching strategy.
        """
        # NOTE: string parsing goes here

        return self(*args)  # calling self handles flyweight creation

    def from_dtype(
        self,
        dtype: dtype_like,
        array: array_like | None = None
    ) -> ScalarType:
        """Construct a :class:`ScalarType <pdcast.ScalarType>` from a
        numpy/pandas :class:`dtype <numpy.dtype>`\ /\
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
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_dtype : For more information on how this method is called.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> import numpy as np
            >>> resolve_type(np.dtype(int)) is resolve_type(np.dtype(int))
            True

        If a type defines a :attr:`_cache_size` attribute, then only a fixed
        number of instances will be cached, implementing a Least Recently Used
        (LRU) caching strategy.
        """
        # NOTE: dtype parsing goes here

        return self   # if the type is parametrized, call self() directly

    def from_scalar(self, example: Any) -> ScalarType:
        """Construct a :class:`ScalarType <pdcast.ScalarType>` from scalar
        example data.

        Parameters
        ----------
        example : Any
            A scalar example of this type (e.g. ``1``, ``42.0``, ``"foo"``,
            etc.).

        Returns
        -------
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_scalar : For more information on how this method is called.

        Notes
        -----
        When an ambiguous sequence (without a recognizable ``.dtype``) is given
        as input to :func:`detect_type() <pdcast.detect_type>`, this method
        will be called to resolve the ambiguity.  It should be fast, as it will
        be called at every index of the input.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> detect_type(1) is detect_type(2)
            True

        If a type defines a :attr:`_cache_size` attribute, then only a fixed
        number of instances will be cached, implementing a Least Recently Used
        (LRU) caching strategy.
        """
        # NOTE: scalar parsing goes here

        return self  # if the type is parametrized, call self() directly

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
        Type.contains : For more information on how this method is called.

        Examples
        --------
        By default, this method treats unparametrized instances as wildcards,
        which match all of their parametrized equivalents.

        .. doctest::

            >>> resolve_type("M8").contains("M8[5ns]")
            True
            >>> resolve_type("M8[5ns]").contains("M8")
            False
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        # treat base instances as wildcards
        if self == self.base_instance:
            return isinstance(other, type(self))

        return self == other

    #############################
    ####    CONFIGURATION    ####
    #############################

    # NOTE: `_cache_size` dictates the number of flyweights to store in a
    # type's instance factory.  Values < 0 indicate an unlimited cache size
    # while values > 0 specify a Least Recently Used (LRU) caching strategy.  0
    # disables the flyweight pattern entirely, though this is not recommended.

    _cache_size: int = -1
    backend: str | None = None  # marker for @AbstractType.implementation

    @property
    def type_def(self):
        """A scalar class for elements of this type.

        Returns
        -------
        type | None
            A class object used to instantiate scalar examples of this type.

        See Also
        --------
        ScalarType.dtype : A pandas array dtype associated with objects of this
            type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").type_def
            <class 'numpy.int64'>
            >>> resolve_type("datetime[python]").type_def
            <class 'datetime.datetime'>

        Whenever ``pdcast`` generates a type that lacks an explicit
        :attr:`dtype <pdcast.ScalarType.dtype>`, it will fall back to this
        attribute as during conversions.  Calling:

        .. doctest::

            >>> cast(["1", "2", "3"], "int[python]")
            0    1
            1    2
            2    3
            dtype: int[python]

        Is effectively equivalent to:

        .. doctest::

            >>> import pandas as pd

            >>> target = resolve_type("int[python]")
            >>> values = [target.type_def(x) for x in ["1", "2", "3"]]
            >>> pd.Series(values, dtype=target.dtype)
            0    1
            1    2
            2    3
            dtype: int[python]
        """
        if self._type_def is None:
            raise NotImplementedError(
                f"{repr(self)} does not define a `type_def` attribute."
            )

        return self._type_def

    @property
    def dtype(self):
        """A numpy :class:`dtype <numpy.dtype>` or pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to use
        for arrays of this type.

        Returns
        -------
        dtype_like
            A pandas-compatible dtype to use for arrays of this type.
            :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>` are
            free to define their own storage backends and behavior.

        Notes
        -----
        If no explicit :attr:`dtype <pdcast.ScalarType.dtype>` is defined for
        a given type, then a new :class:`ObjectDtype <pdcast.ObjectDtype>`
        will be created to encapsulate the associated data.

        .. autosummary::
            :toctree: ../generated/
        
            ObjectDtype
            ObjectArray

        This may not be the most efficient way to store data of a particular
        type.  If a more compact representation is available, users can
        :ref:`write <pandas:extending>` their own
        :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>`
        instead.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int[numpy]").dtype
            dtype('int64')
            >>> resolve_type("int[pandas]").dtype
            Int64Dtype()
            >>> resolve_type("int[python]").dtype
            _ObjectDtype(int[python])

        Each of these are valid :class:`Series <pandas.Series>` dtypes.

        .. doctest::

            >>> import pandas as pd

            >>> pd.Series([1, 2, 3], dtype=resolve_type("int[numpy]").dtype)
            0    1
            1    2
            2    3
            dtype: int64
            >>> pd.Series([1, 2, 3], dtype=resolve_type("int[pandas]").dtype)
            0    1
            1    2
            2    3
            dtype: Int64
            >>> pd.Series([1, 2, 3], dtype=resolve_type("int[python]").dtype)
            0    1
            1    2
            2    3
            dtype: int[python]
        """
        from ..boolean import BooleanType

        if self._dtype is None:
            return construct_object_dtype(
                self,
                is_boolean=BooleanType.contains(self),
                is_numeric=self.is_numeric
            )
        return self._dtype

    @property
    def itemsize(self):
        """The size (in bytes) for scalars of this type.

        Returns
        -------
        int | None
            A positive integer describing the size of each element in bytes,
            or :data:`None` if no fixed size can be given.

        Notes
        -----
        If this would be hard to compute, use
        :func:`sys.getsizeof() <python:sys.getsizeof>` or give an approximate
        lower bound here.

        .. note::

            Measuring the size of a Python object is not always
            straightforward.  The :func:`sys.getsizeof() <python:sys.getsizeof>`
            function is a good place to start, but it may not always be
            accurate.  For instance, it does not account for the size of
            referenced objects, and it may not be consistent across different
            Python implementations.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").itemsize
            8
            >>> resolve_type("bool[python]").itemsize
            28
            >>> resolve_type("decimal").itemsize
            104
        """
        if self._itemsize is None:
            return np.inf

        return self._itemsize

    @property
    def is_numeric(self):
        """A flag indicating whether this type is numeric.

        Returns
        -------
        bool
            ``True`` if this type is numeric, ``False`` otherwise.

        Notes
        -----
        This is used when generating automatic
        :attr:`dtype <pdcast.ScalarType.dtype>` definitions.  If this is
        set to ``True``, then the resulting
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` will be
        labeled numeric, and will support pandas arithmetic and comparison
        operations.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[pandas]").is_numeric
            True
            >>> resolve_type("float32[numpy]").is_numeric
            True
            >>> resolve_type("string").is_numeric
            False
        """
        if self._is_numeric is None:
            return False

        return self._is_numeric

    @property
    def max(self):
        """The maximum representable value for this type.

        Returns
        -------
        int | infinity
            An integer representing the maximum value for this type, or
            :data:`infinity <python:math.inf>` if no maximum is given.

        See Also
        --------
        ScalarType.min : The minimum representable value for this type.

        Notes
        -----
        These properties are used to check for overflow when converting values
        to this type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("bool[numpy]").max
            1
            >>> resolve_type("int8[numpy]").max
            127
            >>> resolve_type("int64[numpy]").max
            9223372036854775807
            >>> resolve_type("datetime64[ns]").max
            9223372036854775807
            >>> resolve_type("decimal").max
            Infinity

        Floating-point types have a maximum value, but rather than representing
        a hard limit, it instead describes the largest value that can be
        represented without losing integer precision.

        .. doctest::

            >>> resolve_type("float32[numpy]").max
            16777216
            >>> resolve_type("float64[numpy]").max
            9007199254740992
        """
        if self._max is None:
            return np.inf

        return self._max

    @property
    def min(self):
        """The minimum representable value for this type.

        Returns
        -------
        int | infinity
            An integer representing the minimum value for this type, or
            negative :data:`infinity <numpy.inf>` if no minimum is given.

        See Also
        --------
        ScalarType.max : The maximum representable value for this type.

        Notes
        -----
        These properties are used to check for overflow when converting values
        to this type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("bool[numpy]").min
            0
            >>> resolve_type("int8[numpy]").min
            -128
            >>> resolve_type("int64[numpy]").min
            -9223372036854775808
            >>> resolve_type("datetime64[ns]").min
            -9223372036854775807
            >>> resolve_type("decimal").min
            -inf

        Floating-point types have a minimum value, but rather than representing
        a hard limit, it instead describes the smallest value that can be
        represented without losing integer precision.

        .. doctest::

            >>> resolve_type("float32[numpy]").min
            -16777216
            >>> resolve_type("float64[numpy]").min
            -9007199254740992
        """
        if self._min is None:
            return -np.inf

        return self._min

    @property
    def is_nullable(self):
        """A flag indicating whether this type supports missing values.

        Returns
        -------
        bool
            ``True`` if this type supports missing values, ``False`` otherwise.

        Notes
        -----
        If this is set to ``False``, :func:`@dispatch <dispatch>` will
        automatically invoke :meth:`make_nullable() <ScalarType.make_nullable>`
        to convert this type into a nullable alternative when missing values
        are detected in a function's output.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").is_nullable
            False
            >>> resolve_type("int64[pandas]").is_nullable
            True
            >>> resolve_type("string").is_nullable
            True
        """
        if self._is_nullable is None:
            return True

        return self._is_nullable

    @property
    def na_value(self):
        """The representation to use for missing values of this type.

        Returns
        -------
        Any
            An NA-like value for this data type.

        Notes
        -----
        This will be used as the fill value when :func:`@dispatch <dispatch>`
        detects missing values in a function's output.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[pandas]").na_value
            <NA>
            >>> resolve_type("float32[numpy]").na_value
            nan
            >>> resolve_type("complex128[numpy]").na_value
            (nan+nanj)
            >>> resolve_type("decimal").na_value
            Decimal('NaN')
        """
        if self._na_value is None:
            return pd.NA

        return self._na_value

    def make_nullable(self):
        """Convert a non-nullable :class:`ScalarType` into one that can accept
        missing values.

        Returns
        -------
        ScalarType
            A nullable version of this data type to be used when missing values
            are detected in the output of a :func:`@dispatch <dispatch>`
            function.  If the type is already nullable, this will return a
            reference to ``self``.

        Raises
        ------
        NotImplementedError
            If a type is not nullable and does not implement this method.

        See Also
        --------
        ScalarType.is_nullable : An indicator specifying whether a type
            supports missing values.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").make_nullable()
            PandasInt64Type()
            >>> resolve_type("int64[pandas]").make_nullable()
            PandasInt64Type()
        """
        if self.is_nullable:
            return self

        raise NotImplementedError(
            f"'{type(self).__name__}' objects have no nullable alternative."
        )

    # NOTE: additional attributes can be added as needed.  These are just the
    # ones that are necessary for minimal functionality.

    #######################
    ####    SETTERS    ####
    #######################

    # NOTE: these are only exposed to allow reassignment during __init__.  If
    # you'd like to make these attributes truly writable, you should
    # reimplement them in the inheriting class.

    @type_def.setter
    def type_def(self, val: type) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._type_def = val

    @dtype.setter
    def dtype(self, val: dtype_like) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._dtype = val

    @itemsize.setter
    def itemsize(self, val: int) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._itemsize = val

    @is_numeric.setter
    def is_numeric(self, val: bool) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._is_numeric = val

    @max.setter
    def max(self, val: numeric) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._max = val

    @min.setter
    def min(self, val: numeric) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._min = val

    @is_nullable.setter
    def is_nullable(self, val: bool) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._is_nullable = val

    @na_value.setter
    def na_value(self, val: Any) -> None:
        if self._read_only:
            raise READ_ONLY_ERROR
        self._na_value = val

    #########################
    ####    TRAVERSAL    ####
    #########################

    @property
    def is_root(self):
        """Indicates whether this type is the root of its subtype hierarchy.

        Returns
        -------
        bool
            ``True`` if this type does not have an associated
            :attr:`supertype <ScalarType.supertype>`, ``False`` otherwise.

        See Also
        --------
        ScalarType.supertype : The supertype that this type is registered to,
            if one exists.
        ScalarType.root : The root node of this type's subtype hierarchy.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int").is_root
            True
            >>> resolve_type("int[numpy]").is_root
            True
            >>> resolve_type("int64[numpy]").is_root
            False
        """
        return self.supertype is None

    @property
    def root(self):
        """The root node of this type's subtype hierarchy.

        Returns
        -------
        ScalarType
            The top-level :attr:`supertype <ScalarType.supertype>` for this
            type.

        See Also
        --------
        ScalarType.supertype : The supertype that this type is registered to,
            if one exists.
        ScalarType.is_root : An indicator specifying whether this type is the
            root of its subtype hierarchy.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").root
            NumpyIntegerType()
            >>> resolve_type("float16")
            Float16Type()
            >>> resolve_type("bool").root
            BooleanType()
        """
        result = self
        while result.supertype:
            result = result.supertype
        return result

    @property
    def supertype(self):
        """The :class:`AbstractType <pdcast.AbstractType>` this type is a
        :meth:`subtype <pdcast.AbstractType.subtype>` of, if one exists.

        Returns
        -------
        AbstractType | None
            The supertype that this type is registered to, if one exists.

        See Also
        --------
        ScalarType.root : The root node of this type's subtype hierarchy.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").supertype
            NumpySignedIntegerType()
            >>> resolve_type("float16").supertype
            FloatType()
            >>> resolve_type("bool").supertype is None
            True
        """
        return self.registry.get_supertype(self)

    @property
    def generic(self):
        """The type that this type is an
        :meth:`implementation <pdcast.AbstractType.implementation>` of, if one
        exists.

        Returns
        -------
        ScalarType
            The generic equivalent of this type.  If this type is not an
            :meth:`implementation <pdcast.AbstractType.implementation>` of
            another type, then this will be a reference to ``self``.

        See Also
        --------
        ScalarType.implementations : A mapping of all the
            :meth:`implementations <pdcast.AbstractType.implementation>` that
            are registered to this type.
        AbstractType.implementation : A class decorator used to mark types as
            concrete implementations of an abstract type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").generic
            Int64Type()
            >>> resolve_type("int64").generic
            Int64Type()
        """
        return self.registry.get_generic(self)

    @property
    def implementations(self):
        """A mapping of all the
        :meth:`implementations <pdcast.AbstractType.implementation>` that are
        registered to this type.

        Returns
        -------
        MappingProxyType
            A read-only dictionary listing the concrete implementations that
            have been registered to this type, with their backend specifiers
            as keys.  This always includes the type's default implementation
            under the ``None`` key.

        See Also
        --------
        ScalarType.generic : The generic equivalent of this type, if one
            exists.
        AbstractType.implementation : A class decorator used to mark types as
            concrete implementations of an abstract type.

        Notes
        -----
        For :class:`ScalarType <pdcast.ScalarType>` objects, this will always
        have a single entry containing a reference to this type's base instance
        under the ``None`` key.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").implementations
            mappingproxy({None: NumpyInt64Type()})
            >>> resolve_type("float16").implementations
            mappingproxy({None: NumpyFloat16Type(), 'numpy': NumpyFloat16Type()})
            >>> resolve_type("bool").implementations
            mappingproxy({None: NumpyBooleanType(), 'numpy': NumpyBooleanType(), 'pandas': PandasBooleanType(), 'python': PythonBooleanType()})
        """
        return MappingProxyType({None: self})

    @property
    def subtypes(self):
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        :meth:`subtypes <pdcast.AbstractType.subtype>` that are currently
        registered to this type.

        Returns
        -------
        CompositeType
            A collection of :class:`ScalarTypes <pdcast.ScalarType>` and/or
            :class:`AbstractTypes <pdcast.AbstractType>` representing the
            immediate children of this type.

        See Also
        --------
        ScalarType.leaves : A :class:`CompositeType <pdcast.CompositeType>`
            containing all the leaf nodes associated with this type.

        Notes
        -----
        For :class:`ScalarType <pdcast.ScalarType>` objects, this will always
        be an empty :class:`CompositeType <pdcast.CompositeType>`.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").subtypes
            CompositeType({})
            >>> resolve_type("float").subtypes   # doctest: +SKIP
            CompositeType({float16, float32, float64, float80})
            >>> resolve_type("int").subtypes   # doctest: +SKIP
            CompositeType({signed, unsigned})
        """
        return CompositeType()  # overridden in AbstractType

    @property
    def is_leaf(self):
        """A flag indicating whether this type is a leaf node within its
        hierarchy.

        Returns
        -------
        bool
            ``True`` if this type has no registered
            :meth:`subtypes <pdcast.AbstractType.subtype>` or
            :meth:`implementations <pdcast.AbstractType.implementations>`.

        See Also
        --------
        ScalarType.leaves : A :class:`CompositeType <pdcast.CompositeType>`
            containing all the leaf nodes associated with this type.

        Notes
        -----
        For :class:`ScalarType <pdcast.ScalarType>` objects, this will always
        be ``True``.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").is_leaf
            True
            >>> resolve_type("float16").is_leaf
            False
            >>> resolve_type("bool").is_leaf
            False
        """
        return True  # overridden in AbstractType

    @property
    def leaves(self):
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        leaf nodes associated with this type's hierarchy.

        Returns
        -------
        CompositeType
            A collection of :class:`ScalarTypes <pdcast.ScalarType>`
            representing the leaf nodes for this type's hierarchy.

        See Also
        --------
        ScalarType.subtypes : A :class:`CompositeType <pdcast.CompositeType>`
            containing all the :meth:`subtypes <pdcast.AbstractType.subtype>`
            of this type.
        ScalarType.implementations : A mapping of all the
            :meth:`implementations <pdcast.AbstractType.implementation>` of
            this type.

        Notes
        -----
        For :class:`ScalarType <pdcast.ScalarType>` objects, this will always
        be an empty :class:`CompositeType <pdcast.CompositeType>`.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").leaves
            CompositeType({})
            >>> resolve_type("float16").leaves   # doctest: +SKIP
            CompositeType({float16[numpy]})
            >>> resolve_type("bool").leaves   # doctest: +SKIP
            CompositeType({bool[numpy], bool[pandas], bool[python]})
        """
        candidates = set(CompositeType(self).expand()) - {self}
        return CompositeType(typ for typ in candidates if typ.is_leaf)

    ###############################
    ####    UPCAST/DOWNCAST    ####
    ###############################

    @property
    def larger(self):
        """An ordered sequence of types that this type can be upcasted to in
        the event of overflow.

        Returns
        -------
        Iterator
            A generator object that yields
            :class:`ScalarTypes <pdcast.ScalarType>` in order.

        See Also
        --------
        ScalarType.smaller : An ordered sequence of types that this type can be
            downcasted to for memory efficiency.

        Examples
        --------
        :class:`ScalarTypes <pdcast.ScalarType>` can implement this attribute
        to allow dynamic resizing based on observed data.  For instance, numpy
        datetime64s support a variety of units, each with their own limits.
        If we cast to a unit that is too small, we receive an
        :data:`OverflowError <python:OverflowError>`.

        .. doctest::

            >>> cast(2**65, "datetime[numpy, ns]", unit="ns")
            Traceback (most recent call last):
                ...
            OverflowError: values exceed datetime[numpy, ns, 1] range at index [0]

        If we don't specify a unit, we instead choose the smallest one that
        fits the data.

        .. doctest::

            >>> cast(2**65, "datetime[numpy]", unit="ns")
            0    3139-02-09T23:09:07.419103
            dtype: object

        In this case, the result is measured in microseconds.
        """
        yield from ()

    @property
    def smaller(self):
        """An ordered sequence of types that this type can be downcasted to in
        order to save memory.

        Returns
        -------
        Iterator
            A generator that yields :class:`ScalarTypes <pdcast.ScalarType>` in
            order.

        See Also
        --------
        ScalarType.larger : An ordered sequence of types that this type can be
            upcasted to in the event of overflow.
        AbstractType.smaller : Abstract equivalent of this method.

        Notes
        -----
        :class:`ScalarTypes <pdcast.ScalarType>` can implement this method to
        allow :attr:`downcasting <pdcast.convert.arguments.downcast>` based on
        observed data.
        """
        yield from ()

    def __lt__(self, other: VectorType) -> bool:
        """Sort types according to their priority and representable range.

        Parameters
        ----------
        other : VectorType
            Another type to compare against.

        Returns
        -------
        bool
            ``True`` if this type is considered to be smaller than ``other``.
            See the notes below for details.

        See Also
        --------
        ScalarType.__gt__ : The inverse of this operator.
        TypeRegistry.priority : Override the default sorting order.

        Notes
        -----
        This operator is automatically called by the built-in
        :func:`sorted() <python:sorted>` function to sort type objects.  By
        default, types are sorted by the following criteria in ascending
        `lexicographic <https://en.wikipedia.org/wiki/Lexicographic_order>`_
        (smallest to largest) order:

            #.  Their total representable range (:attr:`max <ScalarType.max>` -
                :attr:`min <ScalarType.min>`).
            #.  Their size in memory (:attr:`itemsize <ScalarType.itemsize>`).
            #.  Their bias with respect to zero (:attr:`max <ScalarType.max>` +
                :attr:`min <ScalarType.min>`).
            #.  Alphabetically by their backend specifier ('numpy', 'pandas',
                'python', etc.).

        Manual overrides for this operator can be registered under the
        :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table,
        which represents a set of edges ``(A, B)`` where ``A < B``.  If an
        edge is present in this table, the above criteria will be bypassed
        entirely.

        .. note::

            In order to maintain the stability of sorts, this operator should
            never be overridden in subclasses.

        Examples
        --------
        This operator is used to sort types in
        :attr:`AbstractType.larger <pdcast.AbstractType.larger>`\ /
        :attr:`AbstractType.smaller <pdcast.AbstractType.smaller>`.

        .. doctest::

            >>> list(resolve_type("int").larger)
            [NumpyUInt64Type(), PandasUInt64Type(), PythonIntegerType()]
            >>> NumpyUInt64Type < PythonIntegerType
            True
        """
        # decorator special case - recur with wrapped type
        if isinstance(other, DecoratorType):
            return other.wrapped is not None and self < other.wrapped

        # check for manual override
        if (type(self), type(other)) in self.registry.priority:
            return True
        if (type(other), type(self)) in self.registry.priority:
            return False

        # default sort order
        itemsize = lambda typ: typ.itemsize
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        family = lambda typ: typ.backend or ""

        # lexical sort, same as `key` argument of sorted()
        features = lambda typ: (
            coverage(typ), itemsize(typ), bias(typ), family(typ)
        )

        return features(self) < features(other)

    def __gt__(self, other: VectorType) -> bool:
        """Sort types by their priority and representable range.

        Parameters
        ----------
        other : VectorType
            Another type to compare against.

        Returns
        -------
        bool
            ``True`` if this type is larger than ``other``.

        See Also
        --------
        ScalarType.__lt__ : The inverse of this operator.
        TypeRegistry.priority : Override the default sorting order.

        Notes
        -----
        This operator is provided for completeness with respect to
        :meth:`ScalarType.__lt__() <pdcast.ScalarType.__lt__>`.  It represents
        the inverse of that operation.

        If a manual override is registered under the
        :attr:`TypeRegistry.priority <pdcast.TypeRegistry.priority>` table,
        then it will be used for both operators.
        """
        # decorator special case - recur with wrapped type
        if isinstance(other, DecoratorType):
            return other.wrapped is None or self > other.wrapped

        # check for manual override
        if (type(self), type(other)) in self.registry.priority:
            return False
        if (type(other), type(self)) in self.registry.priority:
            return True

        # default sort order
        itemsize = lambda typ: typ.itemsize
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        family = lambda typ: typ.backend or ""

        # lexical sort
        features = lambda typ: (
            coverage(typ), itemsize(typ), bias(typ), family(typ)
        )

        return features(self) > features(other)

    def __hash__(self) -> int:
        """Reimplement hash() for ScalarTypes.

        Notes
        -----
        Adding the ``<`` and ``>`` operators mysteriously breaks an inherited
        cython ``__hash__()`` method for some reason.  This appears to be a
        problem with cdef class inheritance in the current build, and isn't
        required for normal python subclasses.
        """
        return self._hash

########################
####    ABSTRACT    ####
########################


cdef class AbstractType(ScalarType):
    """Base class for all user-defined hierarchical types.

    :class:`AbstractTypes <pdcast.AbstractType>` represent nodes within the
    ``pdcast`` type system.  They can contain references to other nodes as
    :meth:`subtypes <pdcast.AbstractType.subtype>` and/or
    :meth:`implementations <pdcast.AbstractType.implementation>`.

    Parameters
    ----------
    **kwargs : dict
        Parametrized keyword arguments describing metadata for this type.
        These must be empty.

    See Also
    --------
    ScalarType : Base class for leaf nodes in an abstract hierarchy.

    Notes
    -----
    These make use of the `Composite pattern
    <https://en.wikipedia.org/wiki/Composite_pattern>`_ to construct type
    hierarchies.  They inherit the same interface as
    :class:`ScalarType <pdcast.ScalarType>` objects, and forward some or all
    of their attributes to their :meth:`default <pdcast.AbstractType.default>`
    :meth:`subtype <pdcast.AbstractType.subtype>` or
    :meth:`implementation <pdcast.AbstractType.implementation>`.
    """

    def __init__(self, **kwargs):
        if kwargs:
            raise NotImplementedError(
                f"abstract types cannot be parametrized: {repr(self)}"
            )
        super().__init__()

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, backend: str | None = None, *args) -> ScalarType:
        """Construct an :class:`AbstractType <pdcast.AbstractType>` from a
        string in the
        :ref:`type specification mini-language <resolve_type.mini_language>`.

        Parameters
        ----------
        backend : str, optional
            The :attr:`backend <pdcast.AbstractType.implementations>` specifier
            to delegate to.
        *args : str
            Positional arguments supplied to the delegated type.  These will
            always be passed as strings, exactly as they appear in the
            :ref:`type specification mini-language <resolve_type.mini_language>`.

        Returns
        -------
        AbstractType | ScalarType
            Either a reference to ``self`` or a concrete
            :meth:`implementation <pdcast.AbstractType.implementation>` of this
            type.

        Raises
        ------
        KeyError
            If a backend specifier is given and is not recognized.

        See Also
        --------
        Type.from_string : For more information on how this method is called.

        Examples
        --------
        If no parameters are given, this method will return a reference to
        ``self``.

        .. doctest::

            >>> resolve_type("datetime")
            DatetimeType()
            >>> isinstance(_, AbstractType)
            True

        Otherwise, the first parameter must refer to one of this type's
        registered :meth:`implementations <pdcast.AbstractType.implementation>`.

        .. doctest::

            >>> resolve_type("datetime[pandas]")
            PandasTimestampType(tz=None)
            >>> resolve_type("datetime[foo]")
            Traceback (most recent call last):
                ...
            KeyError: 'foo'

        Any further arguments will be forwarded to the delegated type.

        .. doctest::

            >>> resolve_type("datetime[pandas, US/Pacific]")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))
        """
        if backend is None:
            return self

        return self.implementations[backend].from_string(*args)  

    def from_dtype(
        self,
        dtype: dtype_like,
        array: array_like | None = None
    ) -> ScalarType:
        """Construct an :class:`AbstractType <pdcast.AbstractType>` from a
        numpy/pandas :class:`dtype <numpy.dtype>`\ /\
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
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_dtype : For more information on how this method is called.

        Notes
        -----
        This method delegates to this type's
        :meth:`default <pdcast.AbstractType.default>` implementation.  It is
        functionally equivalent to
        :meth:`ScalarType.from_dtype() <pdcast.ScalarType.from_dtype>`.
        """
        return self.registry.get_default(self).from_dtype(dtype)

    def from_scalar(self, example: Any) -> ScalarType:
        """Construct an :class:`AbstractType <pdcast.AbstractType>` from scalar
        example data.

        Parameters
        ----------
        example : Any
            A scalar example of this type (e.g. ``1``, ``42.0``, ``"foo"``,
            etc.).

        Returns
        -------
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_scalar : For more information on how this method is called.

        Notes
        -----
        This method delegates to this type's
        :meth:`default <pdcast.AbstractType.default>` implementation.  It is
        functionally equivalent to
        :meth:`ScalarType.from_scalar() <pdcast.ScalarType.from_scalar>`.
        """
        return self.registry.get_default(self).from_scalar(example)

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
        Type.contains : For more information on how this method is called.

        Examples
        --------
        This method extends membership to all of this type's
        :meth:`subtypes <pdcast.AbstractType.subtype>` and
        :meth:`implementations <pdcast.AbstractType.implementation>`.

        .. doctest::

            >>> resolve_type("signed").contains("int8, int16, int32, int64")
            True
            >>> resolve_type("int64").contains("int64[numpy], int64[pandas]")
            True
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        if other == self:
            return True

        children = {typ for typ in self.implementations.values()}
        children |= {typ for typ in self.subtypes}
        return any(typ.contains(other) for typ in children)

    ##########################
    ####    DECORATORS    ####
    ##########################

    @classmethod
    def default(cls, concretion: type = None, *, warn: bool = True):
        """A class decorator that assigns a particular concretion of this type
        as its default value.

        Attribute lookups for the parent type will be forwarded to its default.

        Parameters
        ----------
        concretion : type
            A :class:`ScalarType <pdcast.ScalarType>` subclass to assign as
            this type's default.
        warn : bool, default True
            If ``True``, issue a warning if this type already has a default
            value.

        Returns
        -------
        type
            The decorated type.  This is not modified in any way.

        Raises
        ------
        TypeError
            If ``concretion`` is not a subclass of
            :class:`ScalarType <pdcast.ScalarType>`, or it is not a member of
            this type's hierarchy.

        See Also
        --------
        AbstractType.subtype : Link a type as a subtype of this
            :class:`AbstractType <pdcast.AbstractType>`.
        AbstractType.implementation : Link a type as an implementation of this
            :class:`AbstractType <pdcast.AbstractType>`.

        Notes
        -----
        The parent :class:`AbstractType <pdcast.AbstractType>` will delegate
        most of its :ref:`configuration <AbstractType.config>` attributes to
        the default concretion.  This can be updated dynamically as types are
        added to the hierarchy, allowing users to change a parent node's
        behavior simply by defining a new type.

        Examples
        --------
        .. doctest::

            >>> @register
            ... class ParentType(AbstractType):
            ...     name = "parent"

            >>> @register
            ... @ParentType.default
            ... @ParentType.implementation("concrete")
            ... class ChildType(ScalarType):
            ...     max = 10
            ...     min = 0

            >>> ParentType.max
            10

        .. testcleanup::

            pdcast.registry.remove(ParentType)
            pdcast.registry.remove(ChildType)
        """
        def decorator(_concrete: type):
            """Link the decorated type as the default value of this parent."""
            if not issubclass(_concrete, ScalarType):
                raise abstract_decorator_error(_concrete)

            update_default_registry(cls, _concrete, warn)
            return _concrete

        if concretion is not None:
            return decorator(concretion)
        return decorator

    @classmethod
    def implementation(cls, backend: str, validate: bool = True):
        """A class decorator that registers a type definition as an
        implementation of this type.

        Parameters
        ----------
        backend : str
            A unique string to identify the decorated type.  This type will be
            automatically parametrized to accept this specifier as its first
            argument during :func:`resolve_type <pdcast.resolve_type>` lookups.
        validate : bool, default True
            If ``True``, validate that the decorated type implements the
            parent's interface.

        Returns
        -------
        type
            The decorated type.  This is not modified in any way.

        Raises
        ------
        TypeError
            If ``backend`` is not a self-consistent string, or the decorated
            type is malformed in some way.  This can happen if it is not a
            subclass of :class:`ScalarType <pdcast.ScalarType>`, or if
            ``validate=True`` and it does not implement the parent's interface.

        See Also
        --------
        AbstractType.default : Assign a type as the default concretion of this
            :class:`AbstractType <pdcast.AbstractType>`.
        AbstractType.subtype : Link a type as a subtype of this
            :class:`AbstractType <pdcast.AbstractType>`.

        Notes
        -----
        If ``validate=True``, any attributes that are defined on the parent
        type beyond its standard interface must also be defined on the
        decorated type.  This will be checked at import time, preventing users
        from accidentally creating types that do not fully implement the
        parent's interface.
        
        This allows :class:`AbstractTypes <pdcast.AbstractType>` to act in a
        manner similar to the :class:`ABCMeta <python:abc.ABCMeta>` class in
        normal Python, though it does not directly inherit from it.

        Examples
        --------
        .. doctest::

            >>> @register
            ... class ParentType(AbstractType):
            ...     name = "parent"

            >>> @register
            ... @ParentType.implementation("concrete")
            ... class ChildType(ScalarType):
            ...     pass

            >>> ParentType.implementations
            mappingproxy({'concrete': ChildType()})
            >>> ParentType.contains(ChildType)
            True

        .. testcleanup::

            pdcast.registry.remove(ParentType)
            pdcast.registry.remove(ChildType)
        """
        if not isinstance(backend, str):
            raise TypeError(
                f"backend specifier must be a string: {repr(backend)}"
            )

        def decorator(implementation: type):
            """Link the decorated type as an implementation of this parent."""
            if not issubclass(implementation, ScalarType):
                raise abstract_decorator_error(implementation)

            if validate:
                validate_interface(cls, implementation)

            # mark family on type
            if implementation.backend and implementation.backend != backend:
                raise TypeError(
                    f"'{implementation.__qualname__}' backend must be "
                    f"self-consistent: '{backend}' != "
                    f"'{implementation.backend}'"
                )
            implementation.backend = backend

            # allow name collisions with special encoding
            update_implementation_registry(cls, implementation, backend)
            inherit_name(cls, implementation, backend)
            return implementation

        return decorator

    @classmethod
    def subtype(cls, subtype: type = None, *, validate: bool = True):
        """A class decorator that registers a type definition as a subtype of
        this type.

        Parameters
        ----------
        subtype : type
            A :class:`ScalarType <pdcast.ScalarType>` or
            :class:`AbstractType <pdcast.AbstractType>` subclass to register to
            this type.
        validate : bool, default True
            If ``True``, validate that the decorated type implements the
            parent's interface.

        Returns
        -------
        type
            The decorated type.  This is not modified in any way.

        Raises
        ------
        TypeError
            If the decorated type is malformed in some way.  This can happen
            if it is not a subclass of :class:`ScalarType <pdcast.ScalarType>`
            or :class:`AbstractType <pdcast.AbstractType>`, or if
            ``validate=True`` and it does not implement the parent's interface.

        See Also
        --------
        AbstractType.default : Assign a type as the default concretion of this
            :class:`AbstractType <pdcast.AbstractType>`.
        AbstractType.implementation : Link a type as an implementation of this
            :class:`AbstractType <pdcast.AbstractType>`.

        Notes
        -----
        If ``validate=True``, any attributes that are defined on the parent
        type beyond its standard interface must also be defined on the
        decorated type.  This will be checked at import time, preventing users
        from accidentally creating types that do not fully implement the
        parent's interface.
        
        This allows :class:`AbstractTypes <pdcast.AbstractType>` to act in a
        manner similar to the :class:`ABCMeta <python:abc.ABCMeta>` class in
        normal Python, though it does not directly inherit from it.

        Examples
        --------
        .. doctest::

            >>> @register
            ... class ParentType(AbstractType):
            ...     name = "parent"

            >>> @register
            ... @ParentType.subtype
            ... class ChildType(ScalarType):
            ...     name = "child"

            >>> ParentType.subtypes
            CompositeType({child})
            >>> ParentType.contains(ChildType)
            True

        .. testcleanup::

            pdcast.registry.remove(ParentType)
            pdcast.registry.remove(ChildType)
        """
        def decorator(_subtype: type):
            """Link the decorated type as a subtype of this parent."""
            if not issubclass(_subtype, ScalarType):
                raise abstract_decorator_error(_subtype)

            if validate:
                validate_interface(cls, _subtype)

            update_subtype_registry(cls, _subtype)
            return _subtype

        if subtype is not None:
            return decorator(subtype)
        return decorator

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def type_def(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.type_def
        """
        return self.registry.get_default(self).type_def

    @property
    def dtype(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.dtype
        """
        return self.registry.get_default(self).dtype

    @property
    def itemsize(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.itemsize
        """
        return self.registry.get_default(self).itemsize

    @property
    def is_numeric(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.is_numeric
        """
        return self.registry.get_default(self).is_numeric

    @property
    def max(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.max
        """
        return self.registry.get_default(self).max

    @property
    def min(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.min
        """
        return self.registry.get_default(self).min

    @property
    def is_nullable(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.is_nullable
        """
        return self.registry.get_default(self).is_nullable

    @property
    def na_value(self):
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.na_value
        """
        return self.registry.get_default(self).na_value

    def make_nullable(self) -> ScalarType:
        """Delegate to :meth:`default <pdcast.AbstractType.default>`.

        See Also
        --------
        ScalarType.make_nullable
        """
        return self.registry.get_default(self).make_nullable()

    def __getattr__(self, name: str) -> Any:
        """Delegate all other attributes to
        :meth:`default <pdcast.AbstractType.default>`.

        Parameters
        ----------
        name : str
            The name of the attribute to look up.

        Returns
        -------
        Any
            The value of the attribute on the
            :meth:`default <pdcast.AbstractType.default>` concretion.

        Raises
        ------
        AttributeError
            If the attribute is not defined on the
            :meth:`default <pdcast.AbstractType.default>` concretion.

        See Also
        --------
        AbstractType.default : Assign a type as the default concretion of this
            :class:`AbstractType <pdcast.AbstractType>`.

        Notes
        -----
        This allows :class:`AbstractTypes <pdcast.AbstractType>` to
        automatically inherit attributes from its default concretion if they
        are not defined on the parent type itself.

        Examples
        --------
        .. doctest::

            >>> @register
            ... class ParentType(AbstractType):
            ...     name = "parent"

            >>> @register
            ... @ParentType.default
            ... @ParentType.subtype
            ... class ChildType(ScalarType):
            ...     name = "child"
            ...     value = 1

            >>> ParentType.value
            1

        .. testcleanup::

            pdcast.registry.remove(ParentType)
            pdcast.registry.remove(ChildType)
        """
        return getattr(self.registry.get_default(self), name)

    #########################
    ####    TRAVERSAL    ####
    #########################

    @property
    def implementations(self):
        """A mapping of all the
        :meth:`implementations <pdcast.AbstractType.implementation>` that are
        registered to this type.

        Returns
        -------
        MappingProxyType
            A read-only dictionary listing the concrete implementations that
            have been registered to this type, with their backend specifiers
            as keys.  This always includes the type's default implementation
            under the ``None`` key.

        See Also
        --------
        ScalarType.implementations : The scalar equivalent of this attribute.
        AbstractType.implementation : A class decorator used to mark types as
            concrete implementations of this type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").implementations
            mappingproxy({None: NumpyInt64Type()})
            >>> resolve_type("float16").implementations
            {None: NumpyFloat16Type(), 'numpy': NumpyFloat16Type()}
            >>> resolve_type("bool").implementations
            {None: NumpyBooleanType(), 'numpy': NumpyBooleanType(), 'pandas': PandasBooleanType(), 'python': PythonBooleanType()}
        """
        if not self._implementations:
            try:
                result = {None: self.registry.get_default(self)}
            except NotImplementedError:
                result = {}
            result |= self.registry.get_implementations(self)
            self._implementations = CacheValue(MappingProxyType(result))

        return self._implementations.value

    @property
    def subtypes(self):
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        :meth:`subtypes <pdcast.AbstractType.subtype>` that are currently
        registered to this type.

        Returns
        -------
        CompositeType
            A collection of :class:`ScalarTypes <pdcast.ScalarType>` and/or
            :class:`AbstractTypes <pdcast.AbstractType>` representing the
            immediate children of this type.

        See Also
        --------
        ScalarType.leaves : A :class:`CompositeType <pdcast.CompositeType>`
            containing all the leaf nodes associated with this type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").subtypes
            CompositeType({})
            >>> resolve_type("float").subtypes   # doctest: +SKIP
            CompositeType({float16, float32, float64, float80})
            >>> resolve_type("int").subtypes   # doctest: +SKIP
            CompositeType({signed, unsigned})
        """
        if not self._subtypes:
            self._subtypes = CacheValue(self.registry.get_subtypes(self))

        return self._subtypes.value

    @property
    def is_leaf(self):
        """A flag indicating whether this type is a leaf node within its
        hierarchy.

        Returns
        -------
        bool
            ``True`` if this type has no registered
            :meth:`subtypes <pdcast.AbstractType.subtype>` or
            :meth:`implementations <pdcast.AbstractType.implementations>`.

        See Also
        --------
        ScalarType.leaves : A :class:`CompositeType <pdcast.CompositeType>`
            containing all the leaf nodes associated with this type.

        Examples
        --------
        .. doctest::

            >>> resolve_type("int64[numpy]").is_leaf
            True
            >>> resolve_type("float16").is_leaf
            False
            >>> resolve_type("bool").is_leaf
            False
        """
        try:
            implementations = self.registry.get_implementations(self)
        except TypeError:
            implementations = {}

        return not self.subtypes and not implementations

    ###############################
    ####    UPCAST/DOWNCAST    ####
    ###############################

    @property
    def larger(self):
        """An ordered sequence of types that this type can be upcasted to in
        the event of overflow.

        Returns
        -------
        Iterator
            A generator object that yields
            :class:`ScalarTypes <pdcast.ScalarType>` in order.

        See Also
        --------
        AbstractType.smaller : An ordered sequence of types that this type can
            be downcasted to for memory efficiency.
        ScalarType.larger : Scalar equivalent of this method.

        Notes
        -----
        This method uses the :meth:`< <pdcast.ScalarType.__lt__>` operator to
        determine the order of the resulting types.  Users can override that
        method to change how candidates are sorted.

        Examples
        --------
        This attribute automatically searches an
        :class:`AbstractType <pdcast.AbstractType>`'s leaves for candidates.

        .. doctest::

            >>> list(resolve_type("int").larger)
            [NumpyUInt64Type(), PandasUInt64Type(), PythonIntegerType()]
            >>> cast(2**64 - 1, "int")
            0    18446744073709551615
            dtype: uint64
            >>> cast(2**65 - 1)
            0    36893488147419103231
            dtype: int[python]

        If any of the candidates implement their own
        :attr:`larger <pdcast.ScalarType.larger>` attribute, then they will be
        recursively included.
        """
        candidates = self.leaves
        candidates |= {
            typ for candidate in candidates for typ in candidate.larger
        }

        # filter off any leaves with range less than self
        wider = lambda typ: typ.max > self.max or typ.min < self.min

        # sort according to comparison operators
        yield from sorted(typ for typ in candidates if wider(typ))

    @property
    def smaller(self):
        """An ordered sequence of types that this type can be downcasted to in
        order to save memory.

        Returns
        -------
        Iterator
            A generator object that yields
            :class:`ScalarTypes <pdcast.ScalarType>` in order.

        See Also
        --------
        AbstractType.larger : An ordered sequence of types that this type can
            be upcasted to in the event of overflow.
        ScalarType.smaller : Scalar equivalent of this method.

        Notes
        -----
        This method uses the :meth:`< <pdcast.ScalarType.__lt__>` operator to
        determine the order of the resulting types.  Users can override that
        method to change how candidates are sorted.

        Examples
        --------
        This attribute automatically searches an
        :class:`AbstractType <pdcast.AbstractType>`'s leaves for candidates.

        .. doctest::

            >>> list(resolve_type("signed").smaller)
            [NumpyInt8Type(), PandasInt8Type(), NumpyInt16Type(), PandasInt16Type(), NumpyInt32Type(), PandasInt32Type()]
            >>> cast([1, 2, 3], "int", downcast=True)
            0    1
            1    2
            2    3
            dtype: int8
            >>> cast([1, 2, 2**15 - 1], "int", downcast=True)
            0        1
            1        2
            2    32767
            dtype: int16

        If any of the candidates implement their own
        :attr:`smaller <pdcast.ScalarType.smaller>` attribute, then they will
        be recursively included.
        """
        candidates = self.leaves
        candidates |= {
            typ for candidate in candidates for typ in candidate.smaller
        }

        # filter off any leaves with itemsize greater than self
        narrower = lambda typ: typ.itemsize < self.itemsize

        # sort according to comparison operators
        yield from sorted([typ for typ in candidates if narrower(typ)])


#######################
####    PRIVATE    ####
#######################


# These attributes are not implemented by cython extension types, but are
# inserted whenever a python class inherits from it.
cdef set PYTHON_ATTRS = {"__dict__", "__module__", "__weakref__"}


cdef Exception abstract_decorator_error(type offender):
    """Return a standardized error message when a AbstractType's class decorators
    are applied to a non-type class.
    """
    return TypeError(
        f"AbstractTypes can only contain ScalarTypes, not {repr(offender)}"
    )


cdef void validate_interface(type parent, type child):
    """Ensure that the child type implements the abstract type's interface."""
    parent_attrs = [
        attr for attr in dir(parent)
        if attr not in dir(AbstractType) and attr not in PYTHON_ATTRS
    ]
    child_attrs = dir(child)

    # ensure child implements all the attributes of the parent
    missing = set(parent_attrs) - set(child_attrs)
    if missing:
        raise TypeError(f"'{child.__qualname__}' must implement {missing}")

    # ensure method signatures match
    for attr_name in parent_attrs:
        parent_attr = getattr(parent, attr_name)
        child_attr = getattr(child, attr_name)
        if callable(parent_attr):
            compare_methods(parent_attr, child_attr, child, attr_name)
        else:
            compare_properties(parent_attr, child_attr, child, attr_name)


cdef void compare_methods(
    object parent_method,
    object child_method,
    type child,
    str method_name
):
    """Compare two method attributes, ensuring that the child conforms to the
    parent.
    """
    parent_signature = inspect.signature(parent_method)

    # reconstruct signature as string
    reconstructed = []
    for arg_name, param in parent_signature.parameters.items():
        component = arg_name
        if param.annotation is not param.empty:
            component += f": {param.annotation}"
        if param.default is not param.empty:
            component += f" = {param.default}"
        reconstructed.append(component)
    reconstructed = ", ".join(reconstructed)

    # ensure child attr is callable
    if not callable(child_method):
        raise TypeError(
            f"'{child.__qualname__}.{method_name}' must be callable with "
            f"signature '{method_name}({reconstructed})'"
        )

    # ensure child inherits parent arguments (names, not annotations/defaults)
    child_signature = inspect.signature(child_method)
    parent_args = list(parent_signature.parameters)
    child_args = list(child_signature.parameters)
    if parent_args != child_args:
        raise TypeError(
            f"'{child.__qualname__}.{method_name}()' must take arguments "
            f"{parent_args} (observed: {child_args})"
        )


cdef void compare_properties(
    object parent_prop,
    object child_prop,
    type child,
    str prop_name
):
    """Compare two non-callable attributes, ensuring that the child conforms
    to the parent.
    """
    if callable(child_prop):
        raise TypeError(
            f"'{child.__qualname__}.{prop_name}' must not be callable"
        )


cdef void update_default_registry(type parent, type child, bint warn):
    """Update the shared registry's private attributes to reflect the addition
    of a new default concretion.

    This can only be done at the C level.
    """
    cdef TypeRegistry registry = parent.registry
    cdef set candidates
    cdef str warn_msg

    # ensure that the decorated concretion is a subtype or implementation
    candidates = registry.subtypes.get(parent, set()).copy()
    candidates |= set(registry.implementations.get(parent, {}).values())
    if child not in candidates:
        raise TypeError(
            f"@default must be a member of the '{parent.__qualname__}' "
            f"hierarchy: '{child.__qualname__}'"
        )

    if warn and parent in registry.defaults:
        warn_msg = (
            f"overwriting default for {repr(parent)} (use `warn=False` "
            f"to silence this message)"
        )
        warnings.warn(warn_msg, UserWarning)

    registry.defaults[parent] = child


cdef void update_subtype_registry(type parent, type child):
    """Update the shared registry's private attributes to reflect the addition
    of a new subtype.

    This can only be done at the C level.
    """
    cdef TypeRegistry registry = parent.registry
    cdef type typ

    # ensure hierarchy does not contain cycles
    typ = child
    while typ is not None:
        if typ is parent:
            raise TypeError(
                f"type hierarchy cannot contain circular references: "
                f"{repr(parent)}"
            )
        typ = registry.supertypes.get(typ, None)

    registry.subtypes.setdefault(parent, set()).add(child)  # parent -> child
    registry.supertypes[child] = parent  # child -> parent


cdef void update_implementation_registry(type parent, type child, str backend):
    """Update the shared registry's private attributes to reflect the addition
    of a new implementation.

    This can only be done at the C level.
    """
    cdef TypeRegistry registry = parent.registry
    cdef dict candidates

    # ensure backend is unique
    candidates = registry.implementations.setdefault(parent, {})
    if backend in candidates:
        raise TypeError(
            f"backend specifier must be unique: {repr(backend)} is "
            f"reserved for {repr(candidates[backend])}"
        )

    candidates[backend] = child  # parent -> child
    registry.generics[child] = parent  # child -> parent


cdef void inherit_name(type parent, type child, str backend):
    """Copy the name of the parent type onto the child type."""
    # replace name
    try:
        object.__getattribute__(child, "name")
    except AttributeError:
        child.name = parent.name

    # allow conflicts with special encoding
    if child.name == parent.name:
        child.set_encoder(BackendEncoder(backend))
