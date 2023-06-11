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
from pandas.api.extensions import ExtensionDtype

from pdcast.resolve import resolve_type
from pdcast.util.type_hints import (
    array_like, dtype_like, numeric, type_specifier
)

from .registry cimport AliasManager, CacheValue, TypeRegistry
from .vector cimport (
    READ_ONLY_ERROR, VectorType, ArgumentEncoder, BackendEncoder,
    InstanceFactory, FlyweightFactory
)
from .composite cimport CompositeType
from ..array import construct_object_dtype


# TODO: add examples/raises for each method

# TODO: .max/.min are currently stored as arbitrary objects.


# TODO: .larger sorts are unstable if __lt__ is only overridden on one class.
# This leads to ties.
# -> Break ties by looking for overloaded < operator.  If this is found, we
# always put this type first.
# -> There doesn't appear to be a reliable way of doing this in cython.


######################
####    SCALAR    ####
######################


cdef class ScalarType(VectorType):
    """Base class for all user-defined scalar types.

    :class:`ScalarTypes <pdcast.ScalarType>` are the most fundamental unit of
    the ``pdcast`` type system.  They form the leaves of a
    :ref:`type hierarchy <AbstractType.hierarchy>`, describing concrete values
    of a particular type (i.e. :class:`int <python:int>`,
    :class:`numpy.float32`, etc.).

    Parameters
    ----------
    **kwargs : dict
        Parametrized keyword arguments describing metadata for this type.  This
        is conceptually equivalent to the ``_metadata`` field of pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.
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
        Type.from_string :
            For more information on how this method is called.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
            True

        If a type defines a :attr:`_cache_size` attribute, then only a fixed
        number of instances will be cached, implementing a Least Recently Used
        (LRU) caching strategy.
        """
        # NOTE: string parsing goes here

        return self(*args)  # calling self handles flyweight creation

    def from_dtype(self, dtype: np.dtype | ExtensionDtype) -> ScalarType:
        """Construct a :class:`ScalarType <pdcast.ScalarType>` from a
        numpy/pandas :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            A numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.

        Returns
        -------
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_dtype :
            For more information on how this method is called.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> import numpy as np
            >>> pdcast.resolve_type(np.dtype(int)) is pdcast.resolve_type(np.dtype(int))
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
        Type.from_scalar :
            For more information on how this method is called.

        Notes
        -----
        When an ambiguous sequence (without a parsable ``.dtype``) is given as
        input to :func:`detect_type() <pdcast.detect_type>`, this method
        will be called to resolve the ambiguity.  It should be fast, as it will
        be called at every index of the input.

        Examples
        --------
        This method returns
        `flyweights <https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that
        are only allocated once and then cached for the duration of the
        program.

        .. doctest::

            >>> pdcast.detect_type(1) is pdcast.detect_type(2)
            True

        If a type defines a :attr:`_cache_size` attribute, then only a fixed
        number of instances will be cached, implementing a Least Recently Used
        (LRU) caching strategy.
        """
        # NOTE: scalar parsing goes here

        return self  # if the type is parametrized, call self() directly

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
    def type_def(self) -> type | None:
        """The scalar class for objects of this type.

        Returns
        -------
        type | None
            A class object used to instantiate scalar examples of this type.
        """
        # TODO: raise NotImplementedError?
        if self._type_def is None:
            return None

        return self._type_def

    @property
    def dtype(self) -> dtype_like:
        """The numpy :class:`dtype <numpy.dtype>` or pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to use
        for arrays of this type.

        Returns
        -------
        dtype_like
            The dtype to use for arrays of this type.
            :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>` are
            free to define their own storage backends and behavior.

        Notes
        -----
        By default, this will automatically create a new
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
        encapsulate data of this type, storing them internally as a
        ``dtype: object`` array, which may not be the most efficient.  If there
        is a more compact representation for a particular data type, users can
        :ref:`provide <pandas:extending>` their own
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`
        instead.
        """
        from ..boolean import BooleanType

        if self._dtype is None:
            return construct_object_dtype(
                self,
                is_boolean=BooleanType.contains(self),
                is_numeric=self.is_numeric,
                add_comparison_ops=True,
                add_arithmetic_ops=True
            )
        return self._dtype

    @property
    def itemsize(self) -> int | None:
        """The size (in bytes) for scalars of this type.

        Returns
        -------
        int | None
            If not :data:`None`, a positive integer describing the size of each
            element in bytes.  If this would be hard to compute, use
            :func:`sys.getsizeof() <python:sys.getsizeof>` or give an
            approximate lower bound here.

        Notes
        -----
        :data:`None` is interpreted as being resizable/unlimited.
        """
        if self._itemsize is None:
            return None

        return self._itemsize

    @property
    def is_numeric(self) -> bool:
        """Used to auto-generate :class:`ObjectDtypes <pdcast.ObjectDtype>`
        from this type.
        """
        if self._is_numeric is None:
            return False

        return self._is_numeric

    @property
    def max(self) -> decimal.Decimal:
        """TODO"""
        if self._max is None:
            return decimal.Decimal("inf")

        return self._max

    @property
    def min(self) -> decimal.Decimal:
        """TODO"""
        if self._min is None:
            return decimal.Decimal("-inf")

        return self._min

    @property
    def is_nullable(self) -> bool:
        """Indicates whether a type supports missing values.

        Set this ``False`` where necessary to invoke :meth:`make_nullable
        <ScalarType.make_nullable>`.  This allows automatic conversion to a
        nullable alternative when missing values are detected/coerced.
        """
        if self._is_nullable is None:
            return True

        return self._is_nullable

    @property
    def na_value(self) -> Any:
        """The representation to use for missing values of this type.

        Returns
        -------
        Any
            An NA-like value for this data type.
        """
        if self._na_value is None:
            return pd.NA

        return self._na_value

    def make_nullable(self) -> ScalarType:
        """Convert a non-nullable :class:`ScalarType` into one that can accept
        missing values.

        Override this to control how this type is coerced when missing values
        are detected during a :func:`cast` operation. 

        Returns
        -------
        ScalarType
            A nullable version of this data type to be used when missing or
            coerced values are detected during a conversion.
        """
        if self.is_nullable:
            return self

        raise NotImplementedError(
            f"'{type(self).__name__}' objects have no nullable alternative."
        )

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
        Type.contains :
            For more information on how this method is called.

        Examples
        --------
        By default, this method treats unparametrized instances as wildcards,
        which match all of their parametrized equivalents.

        .. doctest::

            >>> pdcast.resolve_type("M8").contains("M8[5ns]")
            True
            >>> pdcast.resolve_type("M8[5ns]").contains("M8")
            False
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        # treat base instances as wildcards
        if self == self.base_instance:
            return isinstance(other, type(self))

        return self == other

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
    def is_root(self) -> bool:
        """Indicates whether this type is the root of its subtype hierarchy.

        Returns
        -------
        bool
            ``True`` if this type has no :attr:`supertype
            <ScalarType.supertype>`, ``False`` otherwise.
        """
        return self.supertype is None

    @property
    def root(self) -> ScalarType:
        """The root node of this type's subtype hierarchy."""
        result = self
        while result.supertype:
            result = result.supertype
        return result

    @property
    def supertype(self) -> AbstractType:
        """An :class:`ScalarType` representing the supertype that this type is
        registered to, if one exists.

        Notes
        -----
        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        return self.registry.get_supertype(self)

    @property
    def is_generic(self) -> bool:
        """Indicates whether this type is managing any backends."""
        return False  # overridden in AbstractType

    @property
    def generic(self) -> AbstractType:
        """The generic equivalent of this type, if one exists."""
        return self.registry.get_generic(self)

    @property
    def backends(self) -> MappingProxyType:
        """A mapping of all the implementations that are registered to this
        type, if it is marked as :func:`@generic <pdcast.generic>`.

        Returns
        -------
        MappingProxyType
            A read-only dictionary listing the concrete implementations that
            have been registered to this type, with their backend specifiers
            as keys.

        Raises
        ------
        TypeError
            If this type is not decorated with
            :func:`@generic <pdcast.generic>`.
        """
        return MappingProxyType({None: self})

    @property
    def subtypes(self) -> CompositeType:
        """A :class:`CompositeType` containing every subtype that is
        currently registered to this type.

        Notes
        -----
        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        return CompositeType()  # overridden in AbstractType

    @property
    def is_leaf(self) -> bool:
        """Indicates whether this type has subtypes."""
        return True  # overridden in AbstractType

    @property
    def leaves(self) -> CompositeType:
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        leaf nodes associated with this type's subtypes.
        """
        candidates = set(CompositeType(self).expand()) - {self}
        return CompositeType(typ for typ in candidates if typ.is_leaf)

    @property
    def larger(self) -> Iterator[ScalarType]:
        """A list of types that this type can be
        :meth:`upcasted <ScalarType.upcast>` to in the event of overflow.

        Override this to change the behavior of a bounded type (with
        appropriate `.min`/`.max` fields) when an ``OverflowError`` is
        detected.

        Notes
        -----
        Candidate types will always be tested in order.
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
    def smaller(self) -> Iterator[ScalarType]:
        """A list of types that this type can be
        :meth:`downcasted <ScalarType.downcast>` to if directed.

        Override this to change the behavior of a type when the ``downcast``
        argument is supplied to a conversion function.

        Notes
        -----
        Candidate types will always be tested in order.
        """
        candidates = self.leaves
        candidates |= {
            typ for candidate in candidates for typ in candidate.smaller
        }

        # filter off any leaves with itemsize greater than self
        itemsize = lambda typ: typ.itemsize or np.inf
        narrower = lambda typ: itemsize(typ) < itemsize(self)

        # sort according to comparison operators
        yield from sorted([typ for typ in candidates if narrower(typ)])

    ########################
    ####    ADAPTERS    ####
    ########################

    # TODO: These should be overloadable convert/ functions

    def make_categorical(
        self,
        series: pd.Series,
        levels: list = None
    ) -> pd.Series:
        """Transform a series of the associated type into a categorical format
        with the given levels.

        This method is invoked whenever a categorical conversion is performed
        that targets this type.

        Parameters
        ----------
        series : pd.Series
            The series to be transformed.
        levels : list
            The categories to use for the transformation.  If this is ``None``
            (the default), then levels will be automatically discovered when
            this method is called.

        Returns
        -------
        pd.Series
            The transformed series.

        Notes
        -----
        If a type implements custom logic when performing a categorical
        conversion, it should be implemented here.
        """
        if levels is None:
            categorical_type = pd.CategoricalDtype()
        else:
            categorical_type = pd.CategoricalDtype(
                pd.Index(levels, dtype=self.dtype)
            )

        return series.astype(categorical_type)

    def make_sparse(
        self,
        series: pd.Series,
        fill_value: Any = None
    ) -> pd.Series:
        """Transform a series of the associated type into a sparse format with
        the given fill value.

        This method is invoked whenever a sparse conversion is performed that
        targets this type.

        Parameters
        ----------
        series : pd.Series
            The series to be transformed.
        fill_value : Any
            The fill value to use for the transformation.  If this is ``None``
            (the default), then this type's ``na_value`` will be used instead.

        Returns
        -------
        pd.Series
            The transformed series.

        Notes
        -----
        If a type implements custom logic when performing a sparse conversion,
        it should be implemented here.
        """
        if fill_value is None:
            fill_value = self.na_value
        sparse_type = pd.SparseDtype(series.dtype, fill_value)
        return series.astype(sparse_type)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __lt__(self, other: ScalarType) -> bool:
        """Sort types by their size in memory and representable range.

        This method is automatically called by the built-in ``sorted()``
        function and thus dictates the ordering of types in the
        :attr:`larger <pdcast.ScalarType.larger>`\ /
        :attr:`smaller <pdcast.ScalarType.smaller>` generators.
        """
        itemsize = lambda typ: typ.itemsize or np.inf
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        family = lambda typ: typ.backend or ""

        # lexical sort, same as `key` argument of sorted()
        features = lambda typ: (
            coverage(typ), itemsize(typ), bias(typ), family(typ)
        )

        return features(self) < features(other)

    def __hash__(self) -> int:
        """Reimplement hash() for ScalarTypes.

        Adding the ``<`` and ``>`` operators mysteriously breaks an inherited
        ``__hash__()`` method for some reason.  This appears to be a problem
        with cython extension class inheritance, and isn't observed in normal
        python subclasses.
        """
        return self._hash

    def __gt__(self, other: ScalarType) -> bool:
        """Sort types by their size in memory and representable range.

        This method is provided for completeness with respect to ``__lt__()``.
        """
        itemsize = lambda typ: typ.itemsize or np.inf
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        family = lambda typ: typ.backend or ""

        # lexical sort
        features = lambda typ: (
            coverage(typ), itemsize(typ), bias(typ), family(typ)
        )

        return features(self) > features(other)


########################
####    ABSTRACT    ####
########################


cdef class AbstractType(ScalarType):
    """Base class for all user-defined hierarchical types.

    :class:`AbstractTypes <pdcast.AbstractType>` represent nodes within the
    ``pdcast`` type system.  They can contain references to other nodes as
    particular :meth:`subtypes <pdcast.AbstractType.subtype>` and/or
    :meth:`implementations <pdcast.AbstractType.implementation>`.

    Parameters
    ----------
    **kwargs : dict
        Parametrized keyword arguments describing metadata for this type.
        These must be empty.
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
            The :attr:`backend <pdcast.AbstractType.backends>` specifier to
            delegate to.
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
        Type.from_string :
            For more information on how this method is called.

        Examples
        --------
        If no parameters are given, this method will return a reference to
        ``self``.

        .. doctest::

            >>> pdcast.resolve_type("datetime")
            DatetimeType()
            >>> isinstance(_, pdcast.AbstractType)
            True

        Otherwise, the first parameter must refer to one of this type's
        registered :meth:`implementations <pdcast.AbstractType.implementation>`.

        .. doctest::

            >>> pdcast.resolve_type("datetime[pandas]")
            PandasTimestampType(tz=None)
            >>> pdcast.resolve_type("datetime[foo]")
            Traceback (most recent call last):
                ...
            KeyError: 'foo'

        Any further arguments will be forwarded to the delegated type.

        .. doctest::

            >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
            PandasTimestampType(tz=zoneinfo.ZoneInfo(key='US/Pacific'))
        """
        if backend is None:
            return self

        return self.backends[backend].from_string(*args)  

    def from_dtype(self, dtype: dtype_like) -> ScalarType:
        """Construct an :class:`AbstractType <pdcast.AbstractType>` from a
        numpy/pandas :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            A numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.

        Returns
        -------
        ScalarType
            A `flyweight <https://en.wikipedia.org/wiki/Flyweight_pattern>`_
            for the specified type.  If this method is given the same inputs
            again, it will return a reference to the original instance.

        See Also
        --------
        Type.from_dtype :
            For more information on how this method is called.

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
        Type.from_scalar :
            For more information on how this method is called.

        Notes
        -----
        This method delegates to this type's
        :meth:`default <pdcast.AbstractType.default>` implementation.  It is
        functionally equivalent to
        :meth:`ScalarType.from_scalar() <pdcast.ScalarType.from_scalar>`.
        """
        return self.registry.get_default(self).from_scalar(example)

    ##########################
    ####    DECORATORS    ####
    ##########################

    @classmethod
    def default(cls, concretion: type = None, *, warn: bool = True):
        """A class decorator that assigns a concretion of this type as its
        default value.

        Attribute lookups for the parent type will be forwarded to its default.
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
        implementation of this :class:`AbstractType <pdcast.AbstractType>`.

        Parameters
        ----------
        backend : str
            A unique string to identify the decorated type.  This type will be
            automatically parametrized to accept this specifier as its first
            argument during :func:`resolve_type <pdcast.resolve_type>` lookups.
        default : bool
            If ``True``, set the decorated type as the default implementation
            for this type.

        Notes
        -----
        Any additional arguments will be dynamically passed to the
        implementation's :meth:`from_string()` constructor.
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
        """A class decorator that registers a child type to this parent.

        Parameters
        ----------
        subtype : type | ScalarType
            An :class:`ScalarType <pdcast.ScalarType>` or
            :class:`AbstractType <pdcast.AbstractType>` subclass to register to
            this type.
        default : bool, default False
            Used to reassign the default value of the parent type to the child
            type.

        Returns
        -------
        type | ScalarType
            The child type.

        Notes
        -----
        TODO
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

    ##########################
    ####    OVERLOADED    ####
    ##########################

    @property
    def is_generic(self) -> bool:
        """Indicates whether this type is managing any backends."""
        return bool(self.registry.get_implementations(self))

    @property
    def backends(self) -> MappingProxyType:
        """A mapping of all backend specifiers to their corresponding
        concretions.
        """
        if not self._backends:
            try:
                result = {None: self.registry.get_default(self)}
            except NotImplementedError:
                result = {}
            result |= self.registry.get_implementations(self)
            self._backends = CacheValue(result)

        return self._backends.value

    @property
    def subtypes(self) -> CompositeType:
        """A :class:`CompositeType` containing every subtype that is
        currently registered to this :class:`AbstractType`.
        """
        if not self._subtypes:
            result = self.registry.get_subtypes(self)
            self._subtypes = CacheValue(result)

        return self._subtypes.value

    @property
    def is_leaf(self) -> bool:
        """Indicates whether this type has subtypes."""
        return not self.subtypes and not self.is_generic

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
        Type.contains :
            For more information on how this method is called.

        Examples
        --------
        This method extends membership to all of this type's
        :meth:`subtypes <pdcast.AbstractType.subtype>` and
        :meth:`implementations <pdcast.AbstractType.implementation>`.

        .. doctest::

            >>> pdcast.resolve_type("signed").contains("int8, int16, int32, int64")
            True
            >>> pdcast.resolve_type("int64").contains("int64[numpy], int64[pandas]")
            True
        """
        other = resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        if other == self:
            return True

        children = {typ for typ in self.backends.values()}
        children |= {typ for typ in self.subtypes}
        return any(typ.contains(other) for typ in children)

    #########################
    ####    DELEGATED    ####
    #########################

    @property
    def type_def(self) -> type | None:
        """Delegate `type_def` to default."""
        return self.registry.get_default(self).type_def

    @property
    def dtype(self) -> np.dtype | ExtensionDtype:
        """Delegate `dtype` to default."""
        return self.registry.get_default(self).dtype

    @property
    def itemsize(self) -> int | None:
        """Delegate `itemsize` to default."""
        return self.registry.get_default(self).itemsize

    @property
    def is_numeric(self) -> bool:
        """Delegate `is_numeric` to default."""
        return self.registry.get_default(self).is_numeric

    @property
    def max(self) -> decimal.Decimal:
        """Delegate `max` to default."""
        return self.registry.get_default(self).max

    @property
    def min(self) -> decimal.Decimal:
        """Delegate `min` to default."""
        return self.registry.get_default(self).min

    @property
    def is_nullable(self) -> bool:
        """Delegate `is_nullable` to default."""
        return self.registry.get_default(self).is_nullable

    @property
    def na_value(self) -> Any:
        """Delegate `na_value` to default."""
        return self.registry.get_default(self).na_value

    def make_nullable(self) -> ScalarType:
        """Delegate `make_nullable()` to default."""
        return self.registry.get_default(self).make_nullable()

    def __getattr__(self, name: str) -> Any:
        return getattr(self.registry.get_default(self), name)


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
