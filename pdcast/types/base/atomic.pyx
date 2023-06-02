"""This module describes an ``AtomicType`` object, which serves as the base
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

from pdcast import resolve
from pdcast.util.type_hints import (
    array_like, dtype_like, numeric, type_specifier
)

from .registry cimport AliasManager, CacheValue
from .scalar cimport (
    READ_ONLY_ERROR, ScalarType, ArgumentEncoder, BackendEncoder,
    InstanceFactory, FlyweightFactory
)
from .composite cimport CompositeType
from ..array import abstract


# TODO: add examples/raises for each method

# TODO: remove is_na() in favor of pd.isna() and convert make_nullable into
# a .nullable property.

# TODO: store .max, .min as Decimal objects.  init_base can intercept these
# and coerce them to decimal if needed.

# TODO: does it make sense for @implementation to decorate a parent type?


######################
####    ATOMIC    ####
######################


cdef class AtomicTypeConstructor(ScalarType):
    """A stub class that separates internal :class:`AtomicType` constructor
    methods from the public interface.
    """   

    cdef void init_base(self):
        """Initialize a base (non-parametrized) instance of this type.

        See :meth:`ScalarType.init_base` for more information on how this is
        called and why it is necessary.
        """
        self.base_instance = self

        self._init_encoder()
        self._slug = self.encoder((), self._kwargs)
        self._init_instances()

    cdef void init_parametrized(self):
        """Initialize a parametrized instance of this type.

        See :meth:`ScalarType.init_parametrized` for more information on how
        this is called and why it is necessary.
        """
        base = self.base_instance

        self.encoder = base.encoder
        self._slug = self.encoder((), self._kwargs)
        self.instances = base.instances

    cdef void _init_encoder(self):
        """Create a ArgumentEncoder to uniquely identify this type.

        Notes
        -----
        There are two possible algorithms for generating identification strings
        based on decorator configuration:

            (1) A unique name followed by a bracketed list of parameter
                strings.
            (2) A non-unique name followed by a bracketed list containing a
                unique backend specifier and zero or more parameter strings.

        The second option is chosen whenever a concrete implementation is
        registered to a parent that shares the same name.  This is what allows
        us to maintain generic namespaces with unique identifiers.
        """
        name = self.name
        if not isinstance(name, str):
            raise TypeError(f"{repr(self)}.name must be a string")

        # NOTE: appropriate slug generation depends on hierarchical
        # configuration.  If name is inherited from parent, we have to append
        # the backend specifier as the first parameter to maintain uniqueness.

        backend = getattr(type(self), "_backend", None)
        if backend is None:
            encoder = ArgumentEncoder(name, self._kwargs)
        else:
            encoder = BackendEncoder(name, self._kwargs, backend)

        self.encoder = encoder

    cdef void _init_instances(self):
        """Create an InstanceFactory to control instance generation for this
        type.

        The chosen factory depends on the value of
        :attr:`AtomicType.cache_size`.
        """
        cache_size = self.cache_size

        # cache_size = 0 negates flyweight pattern
        if not cache_size:
            instances = InstanceFactory(type(self))
        else:
            instances = FlyweightFactory(type(self), self.encoder, cache_size)
            instances[self._slug] = self

        self.instances = instances


cdef class AtomicType(AtomicTypeConstructor):
    """Abstract base class for all user-defined scalar types.

    :class:`AtomicTypes <AtomicType>` are the most fundamental unit of the
    ``pdcast`` type system.  They are used to describe scalar values of a
    particular type (i.e. :class:`int <python:int>`, :class:`numpy.float32`,
    etc.), and can be linked together into hierarchical tree structures.

    Parameters
    ----------
    **kwargs : dict
        Arbitrary keyword arguments describing metadata for this type.  If a
        subclass accepts arguments in its ``__init__`` method, they should
        always be passed here via ``super().__init__(**kwargs)``.  This is
        conceptually equivalent to the ``_metadata`` field of pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` objects.

    Examples
    --------
    All in all, a typical :class:`AtomicType` definition could look something
    like this:

    .. code:: python

        @pdcast.register
        @ParentType.subtype
        @GenericType.implementation("backend")  # inherits .name
        class ImplementationType(pdcast.AtomicType):

            cache_size = 128
            aliases = {"foo", "bar", "baz", np.dtype(np.int64), int, ...}
            type_def = int
            dtype = np.dtype(np.int64)
            itemsize = 8
            na_value = pd.NA

            def __init__(self, x=None, y=None):
                # custom arg parsing goes here, along with any new attributes
                super().__init__(x=x, y=y)  # no new attributes after this point

            # further customizations

    Where ``ParentType`` and ``GenericType`` reference other :class:`AtomicType`
    definitions that ``ImplementationType`` is linked to.
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
    # so any attribute assignment will fail after it is called.

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def resolve(self, *args: str) -> AtomicType:
        """Construct a new :class:`AtomicType` in the :ref:`type specification
        mini-language <resolve_type.mini_language>`.

        Override this if a type implements custom parsing rules for any
        arguments that are supplied to it.

        Parameters
        ----------
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the :ref:`type
            specification mini-language <resolve_type.mini_language>`.

        See Also
        --------
        AdapterType.resolve : the adapter equivalent of this method.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same inputs again in the future, then this will be a simple
            reference to the previous instance.
        """
        # NOTE: any special string conversion logic goes here
        return self(*args)

    def detect(self, example: Any) -> AtomicType:
        """Construct a new :class:`AtomicType` from scalar example data.

        Override this if a type has attributes that depend on the value of a
        corresponding scalar (e.g. datetime64 units, timezones, etc.)

        Parameters
        ----------
        example : Any
            A scalar example of this type (e.g. ``1``, ``42.0``, ``"foo"``,
            etc.).

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method is called during :func:`detect_type` operations when there
        is no explicit ``.dtype`` field to interpret.  This might be the case
        for objects that are stored in a base Python list or ``dtype: object``
        array, for instance.

        In order for this method to be called, the output of
        :class:`type() <python:type>` on the example must be registered as one
        of this type's :attr:`aliases <AtomicType.aliases>`.

        If the input to :func:`detect_type` is vectorized, then this method
        will be called at each index.
        """
        # NOTE: any special scalar parsing logic goes here
        return self()

    def from_dtype(self, dtype: np.dtype | ExtensionDtype) -> AtomicType:
        """Construct an :class:`AtomicType` from a corresponding numpy/pandas
        :class:`dtype <numpy.dtype>`\ /\
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` object.

        Override this if a type must parse the attributes of an associated
        :class:`dtype <numpy.dtype>` or
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>`.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            The numpy :class:`dtype <numpy.dtype>` or pandas
            :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to
            parse.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        See Also
        --------
        AdapterType.from_dtype : the adapter equivalent of this method.

        Notes
        -----
        For numpy :class:`dtypes <numpy.dtype>`, the input to this function
        must be a member of the type's :attr:`aliases <AtomicType.aliases>`
        attribute.

        For pandas :class:`ExtensionDtypes <pandas.api.extensions.ExtensionDtype>`,
        the input's **type** must be a member of
        :attr:`aliases <AtomicType.aliases>`, not the dtype itself.  This
        asymmetry allows pandas dtypes to be arbitrarily parameterized when
        passed to this method.
        """
        # NOTE: any special dtype parsing logic goes here.
        return self()

    #############################
    ####    CONFIGURATION    ####
    #############################

    # NOTE: `cache_size` dictates the number of flyweights to store in a type's
    # instance factory.  Values < 0 indicate an unlimited cache size while
    # values > 0 specify a Least Recently Used (LRU) caching strategy.  0
    # disables the flyweight pattern entirely, though this is not recommended.

    cache_size = -1

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
        if self._dtype is None:
            return abstract.construct_extension_dtype(
                self,
                is_boolean=self.is_subtype("bool"),
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
        """Used to auto-generate :class:`AbstractDtypes <pdcast.AbstractDtype>`
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
        <AtomicType.make_nullable>`.  This allows automatic conversion to a
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

        See Also
        --------
        AtomicType.is_na : for comparisons against this value.
        """
        if self._na_value is None:
            return pd.NA

        return self._na_value

    def make_nullable(self) -> AtomicType:
        """Convert a non-nullable :class:`AtomicType` into one that can accept
        missing values.

        Override this to control how this type is coerced when missing values
        are detected during a :func:`cast` operation. 

        Returns
        -------
        AtomicType
            A nullable version of this data type to be used when missing or
            coerced values are detected during a conversion.
        """
        if self.is_nullable:
            return self

        raise NotImplementedError(
            f"'{type(self).__name__}' objects have no nullable alternative."
        )

    def is_na(self, val: Any) -> bool | array_like:
        """Check if one or more values are considered missing in this
        representation.

        Parameters
        ----------
        val : Any
            A scalar or 1D vector of values to check for NA equality.

        Returns
        -------
        bool | array-like
            ``True`` where ``val`` is equal to this type's ``na_value``,
            ``False`` otherwise.  If the input is vectorized, then the output
            will be as well.

        Notes
        -----
        Comparison with missing values is often tricky.  Most NA values are not
        equal to themselves, so some other algorithm must be used to test for
        them.  This method allows users to define this logic on a per-type
        basis.

        If you override this method, you should always call its base equivalent
        via ``super().is_na()`` before returning a custom result.
        """
        return pd.isna(val)

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Test whether ``other`` is a member of this type's hierarchy tree.

        Override this to change the behavior of the `in` keyword and implement
        custom logic for membership tests for the given type.

        Parameters
        ----------
        other : type_specifier
            The type to check for membership.  This can be in any
            representation recognized by :func:`resolve_type`.
        include_subtypes : bool, default True
            Controls whether to include subtypes for this comparison.  If this
            is set to ``False``, then subtypes will be excluded.  Backends will
            still be considered, but only at the top level.

        Returns
        -------
        bool
            ``True`` if ``other`` is a member of this type's hierarchy.
            ``False`` otherwise.

        Notes
        -----
        This method also controls the behavior of the ``in`` keyword for
        :class:`AtomicTypes <AtomicType>`.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

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
            <AtomicType.supertype>`, ``False`` otherwise.
        """
        return self.supertype is None

    @property
    def root(self) -> AtomicType:
        """The root node of this type's subtype hierarchy."""
        result = self
        while result.supertype:
            result = result.supertype
        return result

    @property
    def supertype(self) -> AtomicType:
        """An :class:`AtomicType` representing the supertype that this type is
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
        return False  # overridden in ParentType

    @property
    def generic(self) -> ParentType:
        """The generic equivalent of this type, if one exists."""
        return self.registry.get_generic(self)

    @property
    def backends(self) -> MappingProxyType:
        """A mapping of all the implementations that are registered to this
        type, if it is marked as :func:`@generic <pdcast.generic>`.

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
        return CompositeType()  # overridden in ParentType

    @property
    def is_leaf(self) -> bool:
        """Indicates whether this type has subtypes."""
        return True  # overridden in ParentType

    @property
    def leaves(self) -> CompositeType:
        """A :class:`CompositeType <pdcast.CompositeType>` containing all the
        leaf nodes associated with this type's subtypes.
        """
        candidates = CompositeType(self).expand()
        return CompositeType(typ for typ in candidates if typ.is_leaf)

    @property
    def larger(self) -> Iterator[AtomicType]:
        """A list of types that this type can be
        :meth:`upcasted <AtomicType.upcast>` to in the event of overflow.

        Override this to change the behavior of a bounded type (with
        appropriate `.min`/`.max` fields) when an ``OverflowError`` is
        detected.

        Notes
        -----
        Candidate types will always be tested in order.
        """
        leaves = CompositeType()
        for implementation in self.backends.values():
            leaves |= implementation.leaves

        # filter off any leaves with range less than self
        candidates = [
            typ for typ in leaves if typ.max > self.max or typ.min < self.min
        ]

        # sort according to range, preferring types centered near zero
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        itemsize = lambda typ: typ.itemsize or np.inf
        key = lambda typ: (coverage(typ), bias(typ), itemsize(typ))

        yield from sorted(candidates, key=key)

    @property
    def smaller(self) -> Iterator[AtomicType]:
        """A list of types that this type can be
        :meth:`downcasted <AtomicType.downcast>` to if directed.

        Override this to change the behavior of a type when the ``downcast``
        argument is supplied to a conversion function.

        Notes
        -----
        Candidate types will always be tested in order.
        """
        leaves = CompositeType()
        for implementation in self.backends.values():
            leaves |= implementation.leaves

        # filter off any leaves with itemsize greater than self
        itemsize = lambda typ: typ.itemsize or np.inf
        candidates = [typ for typ in leaves if itemsize(typ) < itemsize(self)]

        # sort by itemsize then range, preferring types centered near zero
        coverage = lambda typ: typ.max - typ.min
        bias = lambda typ: abs(typ.max + typ.min)
        key = lambda typ: (itemsize(typ), coverage(typ), bias(typ))

        yield from sorted(candidates, key=key)


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


############################
####    HIERARCHICAL    ####
############################


# TODO: if @parent comes after @implementation/@subtype, then we need to
# modify the global registry to point to the new type.


def parent(cls: type) -> type:
    """Class decorator to mark generic type definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """
    if not issubclass(cls, AtomicType):
        raise TypeError("@parent can only be applied to AtomicTypes")

    class _ParentType(ParentType):
        __wrapped__ = cls
        name = cls.name

    # copy aliases up the stack
    try:
        _ParentType.aliases = object.__getattribute__(cls, "aliases")
        del cls.aliases
    except AttributeError:
        pass

    cls.registry.implementations[_ParentType] = {}
    cls.registry.subtypes[_ParentType] = set()
    return _ParentType


cdef class ParentType(AtomicType):
    """A Composite Pattern type object that can contain other types.
    """

    def __init__(self):
        wrapped = type(self).__wrapped__  # passed from decorator
        del type(self).__wrapped__

        # instantiate wrapped type
        self.__wrapped__ = wrapped()

        # Type.__init__
        self._aliases = AliasManager(self)

        # ScalarType.__init__
        self.encoder = self.__wrapped__.encoder
        self.instances = self.__wrapped__.instances
        self._slug = self.__wrapped__._slug
        self._hash = self.__wrapped__._hash

        self.base_instance = self
        self._read_only = True

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def default(self) -> AtomicType:
        """The concrete type that this generic type defaults to.

        This will be used whenever the generic type is specified without an
        explicit backend.
        """
        if self._default is not None:
            return self._default

        instance = self.registry.get_default(self)
        if instance is None:
            return self.__wrapped__
        return instance

    @default.setter
    def default(self, val: type_specifier) -> None:
        val = resolve.resolve_type(val)
        if isinstance(val, CompositeType):
            raise TypeError(f"default cannot be composite: {val}")

        if val == self:
            raise TypeError(
                f"default cannot be circular (use `del type.default` "
                f"instead): {val}"
            )

        if not self.contains(val):
            raise TypeError(
                f"default must be contained within this type's hierarchy: "
                f"{val}"
            )

        self._default = val

    @default.deleter
    def default(self) -> None:
        self._default = None

    @classmethod
    def implementation(
        cls,
        backend: str,
        default: bool = False,
        warn: bool = True
    ):
        """A class decorator that registers a type definition as an
        implementation of this :class:`ParentType <pdcast.ParentType>`.

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
        implementation's :meth:`resolve()` constructor.
        """
        if not isinstance(backend, str):
            raise TypeError(
                f"backend specifier must be a string: {repr(backend)}"
            )

        def decorator(implementation: type) -> type:
            """Link the decorated type as an implementation of this parent."""
            if not issubclass(implementation, AtomicType):
                raise TypeError(
                    f"ParentTypes can only contain AtomicTypes, not "
                    f"{repr(implementation)}"
                )

            # marker for AtomicType.generic
            cls.registry.generics[implementation] = cls

            # allow namespace collisions w/ special encoding
            base_type = implementation
            while issubclass(base_type, ParentType):  # TODO: necessary?
                base_type = base_type.__wrapped__
            try:
                object.__getattribute__(base_type, "name")
            except AttributeError:
                base_type.name = cls.name
            if base_type.name == cls.name:
                base_type._backend = backend

            # register backend
            candidates = cls.registry.implementations[cls]
            if backend in candidates:
                raise TypeError(
                    f"backend specifier must be unique: {repr(backend)} is "
                    f"reserved for {repr(candidates[backend])}"
                )
            candidates[backend] = implementation

            # register default implementation
            if default:
                if warn and cls in cls.registry.defaults:
                    warn_msg = (
                        f"overwriting default for {repr(cls)} (use "
                        f"`warn=False` to silence this message)"
                    )
                    warnings.warn(warn_msg, UserWarning)
                cls.registry.defaults[cls] = implementation

            return implementation

        return decorator


    @classmethod
    def subtype(
        cls,
        subtype: type | AtomicType = None,
        *,
        default: bool = False,
        warn: bool = True
    ):
        """A class decorator that registers a child type to this parent.

        Parameters
        ----------
        subtype : type | AtomicType
            An :class:`AtomicType <pdcast.AtomicType>` or
            :class:`ParentType <pdcast.ParentType>` subclass to register to
            this type.
        default : bool, default False
            Used to reassign the default value of the parent type to the child
            type.

        Returns
        -------
        type | AtomicType
            The child type.

        Notes
        -----
        TODO
        """
        def decorator(_subtype: type) -> type:
            """Link the decorated type as a subtype of this parent."""
            if not issubclass(_subtype, AtomicType):
                raise TypeError(
                    f"ParentTypes can only contain AtomicTypes, not "
                    f"{repr(_subtype)}"
                )

            # if _subtype in cls.registry.supertypes:
            #     raise TypeError(
            #         f"subtypes can only be registered to one parent at a "
            #         f"time: '{_subtype.__qualname__}' is currently registered to "
            #         f"'{_subtype._parent.__qualname__}'"
            #     )

            # ensure hierarchy does not contain cycles
            typ = _subtype
            while typ is not None:
                if typ is cls:
                    raise TypeError(
                        f"type hierarchy cannot contain circular references: "
                        f"{repr(cls)}"
                    )
                typ = cls.registry.supertypes.get(typ, None)

            # marker for AtomicType.supertype
            cls.registry.supertypes[_subtype] = cls

            # register subtype
            cls.registry.subtypes[cls].add(_subtype)

            # register default implementation
            if default:
                if warn and cls in cls.registry.defaults:
                    warn_msg = (
                        f"overwriting default for {repr(cls)} (use "
                        f"`warn=False` to silence this message)"
                    )
                    warnings.warn(warn_msg, UserWarning)
                cls.registry.defaults[cls] = _subtype

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
        cached = self._backends
        if not cached:
            result = {None: self.default}
            result |= self.registry.get_implementations(self)
            cached = CacheValue(MappingProxyType(result))
            self._backends = cached

        return cached.value

    @property
    def subtypes(self) -> CompositeType:
        """A :class:`CompositeType` containing every subtype that is
        currently registered to this :class:`ParentType`.
        """
        cached = self._subtypes
        if not cached:
            result = self.registry.get_subtypes(self)
            cached = CacheValue(CompositeType(result))
            self._subtypes = cached

        return cached.value

    @property
    def is_leaf(self) -> bool:
        """Indicates whether this type has subtypes."""
        return not self.subtypes and not self.is_generic

    def resolve(self, backend: str | None = None, *args) -> AtomicType:
        """Forward constructor arguments to the appropriate implementation."""
        if backend is None:
            return self
        return self.backends[backend].resolve(*args)  

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Extend membership checks to this type's subtypes/implementations."""
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        children = {self.__wrapped__}
        children |= {typ for typ in self.backends.values()}
        children |= {typ for typ in self.subtypes}
        return any(typ.contains(other) for typ in children)

    #########################
    ####    DELEGATED    ####
    #########################

    def detect(self, example: Any) -> AtomicType:
        """Forward scalar inference to the default implementation."""
        return self.default.detect(example)

    def from_dtype(self, dtype: dtype_like) -> AtomicType:
        """Forward dtype translation to the default implementation."""
        return self.default.from_dtype(dtype)

    @property
    def kwargs(self) -> MappingProxyType:
        """Delegate `kwargs` to default."""
        return self.default.kwargs

    @property
    def type_def(self) -> type | None:
        """Delegate `type_def` to default."""
        return self.default.type_def

    @property
    def dtype(self) -> np.dtype | ExtensionDtype:
        """Delegate `dtype` to default."""
        return self.default.dtype

    @property
    def itemsize(self) -> int | None:
        """Delegate `itemsize` to default."""
        return self.default.itemsize

    @property
    def is_numeric(self) -> bool:
        """Delegate `is_numeric` to default."""
        return self.default.is_numeric

    @property
    def max(self) -> decimal.Decimal:
        """Delegate `max` to default."""
        return self.default.max

    @property
    def min(self) -> decimal.Decimal:
        """Delegate `min` to default."""
        return self.default.min

    @property
    def is_nullable(self) -> bool:
        """Delegate `is_nullable` to default."""
        return self.default.is_nullable

    @property
    def na_value(self) -> Any:
        """Delegate `na_value` to default."""
        return self.default.na_value

    def __getattr__(self, name: str) -> Any:
        return getattr(self.default, name)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __call__(self, *args, **kwargs):
        if not (args or kwargs):
            return self
        return self.instances(args, kwargs)

    def __repr__(self) -> str:
        return repr(self.__wrapped__)
