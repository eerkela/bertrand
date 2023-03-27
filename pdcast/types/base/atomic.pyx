from types import MappingProxyType
from typing import Any, Callable, Iterator

cimport numpy as np
import numpy as np
import pandas as pd
from pandas.api.extensions import ExtensionDtype, register_extension_dtype

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

cimport pdcast.types.base.registry as registry
import pdcast.types.base.registry as registry
cimport pdcast.types.base.adapter as adapter
cimport pdcast.types.base.composite as composite

import pdcast.util.array as array
from pdcast.util.round cimport Tolerance
from pdcast.util.structs cimport LRUDict
from pdcast.util.type_hints import type_specifier


# TODO: add examples/raises for each method

# TODO: IntegerType.downcast should accept a tolerance argument


# conversions
# +------------------------------------------------
# |           | b | i | f | c | d | d | t | s | o |
# +-----------+------------------------------------
# | bool      | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | int       | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | float     | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | complex   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | decimal   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | datetime  | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | timedelta | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | string    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | object    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+


##########################
####    PRIMITIVES    ####
##########################


cdef class BaseType:
    """Base type for all type objects.

    This has no interface of its own.  It simply serves to anchor inheritance
    and distribute the shared type registry to all ``pdcast`` types.
    """

    registry: registry.TypeRegistry = registry.TypeRegistry()


cdef class ScalarType(BaseType):
    """Base type for :class:`AtomicType` and :class:`AdapterType` objects.

    This allows inherited types to manage aliases and update them at runtime.
    """

    #######################
    ####    ALIASES    ####
    #######################

    @classmethod
    def register_alias(cls, alias: Any, overwrite: bool = False) -> None:
        """Register a new alias for this type.

        See the docs on the :ref:`type specification mini language
        <mini_language>` for more information on how aliases work.

        Parameters
        ----------
        alias : Any
            A string, ``dtype``, ``ExtensionDtype``, or scalar type to register
            as an alias of this type.
        overwrite : bool, default False
            Indicates whether to overwrite existing aliases (``True``) or
            raise an error (``False``) in the event of a conflict.
        """
        if alias in cls.registry.aliases:
            other = cls.registry.aliases[alias]
            if other is cls:
                return None
            if overwrite:
                del other.aliases[alias]
            else:
                raise ValueError(
                    f"alias {repr(alias)} is already registered to {other}"
                )
        cls.aliases.add(alias)
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def remove_alias(cls, alias: Any) -> None:
        """Remove an alias from this type.

        See the docs on the :ref:`type specification mini language
        <mini_language>` for more information on how aliases work.

        Parameters
        ----------
        alias : Any
            The alias to remove.  This can be a string, ``dtype``,
            ``ExtensionDtype``, or scalar type that is present in this type's
            ``.aliases`` attribute.
        """
        del cls.aliases[alias]
        cls.registry.flush()  # rebuild regex patterns

    @classmethod
    def clear_aliases(cls) -> None:
        """Remove every alias that is registered to this type.

        See the docs on the :ref:`type specification mini language
        <mini_language>` for more information on how aliases work.
        """
        cls.aliases.clear()
        cls.registry.flush()  # rebuild regex patterns


######################
####    ATOMIC    ####
######################


cdef class AtomicType(ScalarType):
    """Base class for all user-defined implementation types.

    :class:`AtomicTypes <AtomicType>` are the most fundamental unit of the
    ``pdcast`` type system.  They are used to describe scalar values of a
    particular type (i.e. ``int``, ``numpy.float32``, etc.).  They can
    dynamically wrapped with :class:`AdapterTypes <AdapterType>` to modify
    their behavior or contained in :class:`CompositeTypes <CompositeType>` to
    refer to multiple types at once, and they are responsible for defining all
    the necessary implementation logic for dispatched methods, conversions, and
    type-related functionality.

    Parameters
    ----------
    **kwargs : dict
        Arbitrary keyword arguments describing metadata for this type.  If a
        subclass accepts arguments in its ``__init__`` method, they should
        always be passed here.  This is conceptually equivalent to the
        ``_metadata`` field of pandas `ExtensionDtype <https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.api.extensions.ExtensionDtype.html#>`_
        objects.

    Notes
    -----
    See the :doc:`implementation docs </content/implementation/atomic>` and
    :doc:`tutorial </content/tutorial>` for more information on how
    :class:`AtomicTypes <AtomicType>` work and how to build your own.
    """

    # INTERNAL FIELDS.  These should never be overridden.
    conversion_func = convert.to_object   # default standalone conversion
    is_boolean = None   # used to autogenerete ExtensionDtypes
    is_numeric = None   # used to autogenerate ExtensionDtypes
    is_sparse = False   # marker for SparseType
    is_categorical = False   # marker for CategoricalType
    is_generic = None   # marker for @generic, @register_backend
    is_root = True   # marker for @subtype
    backend = None   # marker for @register_backend

    # DEFAULT FIELDS.  These can be overridden to customize a type's behavior.
    type_def = None   # output from type() on an example of this type
    dtype = NotImplemented   # signals pdcast to autogenerate an ExtensionDtype
    itemsize = None   # size in bytes for members of this type
    na_value = pd.NA   # missing value for vectors of this type
    is_nullable = True  # must be explicitly set False where applicable

    def __init__(self, **kwargs):
        self.kwargs = MappingProxyType(kwargs)
        self.slug = self.slugify(**kwargs)
        self.hash = hash(self.slug)

        # Auto-generate a new ExtensionDtype if directed
        if self.dtype == NotImplemented:
            self.dtype = array.construct_extension_dtype(
                self,
                is_boolean=self.is_boolean,
                is_numeric=self.is_numeric,
                add_comparison_ops=True,
                add_arithmetic_ops=True
            )

        self._is_frozen = True  # no new attributes beyond this point

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def slugify(cls) -> str:
        """Generate a string representation of a type.

        The signature of this class method must match a type's ``__init__``
        method.  This symmetry is enforced by the :func:`@register <register>`
        decorator.

        Returns
        -------
        str
            A string that fully specifies the type.  The string must be unique
            for every set of inputs, as it is used to look up flyweights.

        Notes
        -----
        This method is always called **before** initializing a new
        :class:`AtomicType`.  Its uniqueness determines whether a new flyweight
        will be generated for this type.
        """
        # NOTE: we explicitly check for is_generic=False, which signals that
        # @register_backend has been explicitly called on this type.
        if cls.is_generic == False:
            return f"{cls.name}[{cls.backend}]"
        return cls.name

    @classmethod
    def instance(cls, *args, **kwargs) -> AtomicType:
        """The preferred constructor for :class:`AtomicType` objects.

        This method is responsible for implementing the
        `flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_
        pattern for :class:`AtomicType` objects.

        Parameters
        ----------
        *args, **kwargs
            Arguments passed to this type's
            :meth:`slugify() <AtomicType.slugify>` and
            :class:`__init__ <AtomicType>` methods.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same inputs again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method should never be overriden.  Together with a corresponding
        :meth:`slugify() <AtomicType.slugify>` method, it ensures that no
        leakage occurs with the flyweight cache, which could result in
        uncontrollable memory consumption.

        Users should use this method in place of ``__init__`` to ensure the
        `flyweight <https://python-patterns.guide/gang-of-four/flyweight/>`_
        pattern is obeyed.  One can manually check the cache that is used for
        this method under a type's ``.flyweights`` attribute.
        """
        # generate slug
        cdef str slug = cls.slugify(*args, **kwargs)

        # get previous flyweight if one exists
        cdef AtomicType result = cls.flyweights.get(slug, None)
        if result is None:  # create new flyweight
            result = cls(*args, **kwargs)
            cls.flyweights[slug] = result

        # return flyweight
        return result

    @classmethod
    def resolve(cls, *args: str) -> AtomicType:
        """Construct a new :class:`AtomicType` in the :ref:`type specification
        mini-language <mini_language>`.

        Override this if a type implements custom parsing rules for any
        arguments that are supplied to it.

        Parameters
        ----------
        *args : str
            Positional arguments supplied to this type.  These will always be
            passed as strings, exactly as they appear in the :ref:`type
            specification mini-language <mini_language>`.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same inputs again in the future, then this will be a simple
            reference to the previous instance.
        """
        # NOTE: Most types don't accept any arguments at all
        return cls.instance(*args)

    @classmethod
    def detect(cls, example: Any) -> AtomicType:
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

        In order for this method to be called, the output of ``type()`` on the
        example must be registered as one of this type's aliases.

        If the input to :func:`detect_type` is vectorized, then this method
        will be called at each index.
        """
        # NOTE: most types disregard example data
        return cls.instance()

    @classmethod
    def from_dtype(cls, dtype: np.dtype | ExtensionDtype) -> AtomicType:
        """Construct an :class:`AtomicType` from a corresponding numpy/pandas
        ``dtype`` object.

        Override this if a type must parse the attributes of an associated
        ``dtype`` or ``ExtensionDtype``.

        Parameters
        ----------
        dtype : np.dtype | ExtensionDtype
            The numpy ``dtype`` or pandas ``ExtensionDtype`` to parse.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        For numpy ``dtype``\s, the input to this function must be a member of
        the type's ``.aliases`` attribute.

        For pandas ``ExtensionDtype``\s, the input's **type** must be a member
        of ``.aliases``, not the dtype itself.  This asymmetry allows pandas
        dtypes to be arbitrarily parameterized when passed to this method.
        """
        return cls.instance()  # NOTE: most types disregard dtype metadata

    def replace(self, **kwargs) -> AtomicType:
        """Return a modified copy of a type with the values specified in
        `**kwargs`.

        Parameters
        ----------
        **kwargs : dict
            keyword arguments corresponding to attributes of this type.  Any
            arguments that are not specified will be replaced with the current
            values for this type.

        Returns
        -------
        AtomicType
            A flyweight for the specified type.  If this method is given the
            same input again in the future, then this will be a simple
            reference to the previous instance.

        Notes
        -----
        This method respects the immutability of :class:`AtomicType` objects.
        It always returns a flyweight with the new values.
        """
        cdef dict merged = {**self.kwargs, **kwargs}
        return self.instance(**merged)

    ###################################
    ####    SUBTYPES/SUPERTYPES    ####
    ###################################

    def _generate_subtypes(self, types: set) -> set:
        """Transform a set of subtype definitions into their corresponding
        instances.

        Override this if your AtomicType implements custom logic to generate
        subtype instances (such as wildcard behavior or similar functionality).

        Parameters
        ----------
        types : set
            A set containing uninstantiated subtype definitions.  This
            represents all the types that have been decorated with the
            :func:`@subtype <subtype>` decorator with this type as one of their
            parents.

        Returns
        -------
        set
            A set containing the transformed equivalents of the input types,
            converted into their corresponding instances.  This method is free
            to determine how this is done.

        Notes
        -----
        This method will only be called once, with the result being cached
        until the shared :class:`TypeRegistry` is next updated.
        """
        result = set()
        for t in types:  # skip invalid kwargs
            try:
                result.add(t.instance(**self.kwargs))
            except TypeError:
                continue

        return result

    def _generate_supertype(self, supertype: type) -> AtomicType:
        """Transform a (possibly null) supertype definition into its
        corresponding instance.

        Override this if your AtomicType implements custom logic to generate
        supertype instances (due to an interface mismatch or similar obstacle).
        
        Parameters
        ----------
        supertype : type
            A parent :class:`AtomicType` definition that has not yet been
            instantiated.  This can be ``None``, indicating that the type has
            not been decorated with :func:`@subtype <subtype>`.

        Returns
        -------
        AtomicType | None
            The transformed equivalent of the input type, converted to its
            corresponding instance.  This method is free to determine how this
            is done.

        Notes
        -----
        This method will only be called once, with the result being cached
        until the shared :class:`TypeRegistry` is next updated.
        """
        if supertype is None or supertype not in self.registry:
            return None
        return supertype.instance()

    @property
    def subtypes(self) -> composite.CompositeType:
        """A :class:`CompositeType` containing every subtype that is
        currently registered to this type.

        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        if self.registry.needs_updating(self._subtype_cache):
            result = composite.CompositeType(
                self._generate_subtypes(traverse_subtypes(type(self)))
            )
            self._subtype_cache = self.registry.remember(result)

        return self._subtype_cache.value

    @property
    def supertype(self) -> AtomicType:
        """An :class:`AtomicType` representing the supertype that this type is
        registered to, if one exists.

        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        if self.registry.needs_updating(self._supertype_cache):
            result = self._generate_supertype(self._parent)
            self._supertype_cache = self.registry.remember(result)

        return self._supertype_cache.value

    @property
    def root(self) -> AtomicType:
        """The root node of this type's subtype hierarchy."""
        if self.is_root:
            return self
        return self.supertype.root

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
        if isinstance(other, composite.CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # self.backends includes self
        for backend in self.backends.values():
            if other == backend:
                return True
            if include_subtypes:
                subtypes = backend.subtypes.atomic_types - {self}
                if any(s.contains(other) for s in subtypes):
                    return True

        return False

    def is_subtype(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        """Reverse of :meth:`AtomicType.contains`.

        Parameters
        ----------
        other : type specifier
            The type to check for membership.  This can be in any
            representation recognized by :func:`resolve_type`.
        include_subtypes : bool, default True
            Controls whether to include subtypes for this comparison.  If this
            is set to ``False``, then subtypes will be excluded.  Backends will
            still be considered, but only at the top level.

        Returns
        -------
        bool
            ``True`` if ``self`` is a member of ``other``\'s hierarchy.
            ``False`` otherwise.

        Notes
        -----
        This method performs the same check as :meth:`AtomicType.contains`,
        except in reverse.  It is functionally equivalent to
        ``other.contains(self)``.
        """
        other = resolve.resolve_type(other)
        return other.contains(self, include_subtypes=include_subtypes)

    #######################
    ####    GENERIC    ####
    #######################

    @property
    def backends(self) -> MappingProxyType:
        """A dictionary mapping backend specifiers to their corresponding
        implementation types.

        This dictionary always contains the key/value pair ``{None: self}``.
        """
        if self.registry.needs_updating(self._backend_cache):
            result = {None: self}
            result |= {
                k: v.instance(**self.kwargs) for k, v in self._backends.items()
            }
            result = MappingProxyType(result)
            self._backend_cache = self.registry.remember(result)

        return self._backend_cache.value

    @property
    def generic(self) -> AtomicType:
        """The generic equivalent of this type, if one exists."""
        if self.registry.needs_updating(self._generic_cache):
            if self.is_generic:
                result = self
            elif self._generic is None:
                result = None
            else:
                result = self._generic.instance()
            self._generic_cache = self.registry.remember(result)
        return self._generic_cache.value

    @classmethod
    def register_backend(cls, backend: str):
        """A decorator that allows individual backends to be added to generic
        types.

        Parameters
        ----------
        backend : str
            The string to use as this type's backend specifier.

        Notes
        -----
        The implementation for this method can be found within the
        :func:`@generic <generic>` decorator itself.  Users do not need to
        implement it themselves.
        """
        raise NotImplementedError(f"'{cls.__name__}' is not generic")

    ########################
    ####    ADAPTERS    ####
    ########################

    @property
    def adapters(self) -> Iterator[adapter.AdapterType]:
        """An iterator that yields each :class:`AdapterType` that is attached
        to this type.

        For :class:`AtomicTypes <AtomicType>`, this is always an empty
        iterator.
        """
        yield from ()

    def unwrap(self) -> AtomicType:
        """Get the base :clasS:`AtomicType` for this type, disregarding
        adapters.

        For :class:`AtomicTypes <AtomicType>`, this always returns ``self``.
        """
        return self

    def make_categorical(
        self,
        series: convert.SeriesWrapper,
        levels: list = None
    ) -> convert.SeriesWrapper:
        """Transform a series of the associated type into a categorical format
        with the given levels.

        This method is invoked whenever a categorical conversion is performed
        that targets this type.

        Parameters
        ----------
        series : SeriesWrapper
            The series to be transformed.  This is always provided as a
            :class:`SeriesWrapper` object.
        levels : list
            The categories to use for the transformation.  If this is ``None``
            (the default), then levels will be automatically discovered when
            this method is called.

        Returns
        -------
        SeriesWrapper
            The transformed series, returned as a :class:`SeriesWrapper`.

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

        return convert.SeriesWrapper(
            series.series.astype(categorical_type),
            hasnans=series.hasnans
            # element_type is set in AdapterType.transform()
            )

    def make_sparse(
        self,
        series: convert.SeriesWrapper,
        fill_value: Any = None
    ) -> convert.SeriesWrapper:
        """Transform a series of the associated type into a sparse format with
        the given fill value.

        This method is invoked whenever a sparse conversion is performed that
        targets this type.

        Parameters
        ----------
        series : SeriesWrapper
            The series to be transformed.  This is always provided as a
            :class:`SeriesWrapper` object.
        fill_value : Any
            The fill value to use for the transformation.  If this is ``None``
            (the default), then this type's ``na_value`` will be used instead.

        Returns
        -------
        SeriesWrapper
            The transformed series, returned as a :class:`SeriesWrapper`.

        Notes
        -----
        If a type implements custom logic when performing a sparse conversion,
        it should be implemented here.
        """
        if fill_value is None:
            fill_value = self.na_value
        sparse_type = pd.SparseDtype(series.dtype, fill_value)

        return convert.SeriesWrapper(
            series.series.astype(sparse_type),
            hasnans=series.hasnans
            # element_type is set in AdapterType.transform()
        )

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert generic data to a boolean data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_boolean()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_boolean()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.make_nullable()
        return series.astype(dtype, errors=errors)

    def to_integer(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert generic data to an integer data type.

        Note: this method does not do any cleaning/pre-processing of the
        incoming data.  If a type definition requires this, then it should be
        implemented in its own `to_integer()` equivalent before delegating
        to this method in the return statement.  Any changes made to this
        method will be propagated to the top-level `to_integer()` and `cast()`
        functions when they are called on objects of the given type.
        """
        if series.hasnans:
            dtype = dtype.make_nullable()

        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast)
        return series

    def to_float(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a floating point data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_complex(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: composite.CompositeType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a complex data type."""
        series = series.astype(dtype, errors=errors)
        if downcast is not None:
            return dtype.downcast(series, smallest=downcast, tol=tol)
        return series

    def to_decimal(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert boolean data to a decimal data type."""
        return series.astype(dtype, errors=errors)

    def to_string(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        format: str,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert arbitrary data to a string data type.

        Override this to change the behavior of the generic `to_string()` and
        `cast()` functions on objects of the given type.
        """
        if format:
            series = series.apply_with_errors(
                lambda x: f"{x:{format}}",
                errors=errors,
                element_type=resolve.resolve_type(str)
            )
        return series.astype(dtype, errors=errors)

    def to_object(
        self,
        series: convert.SeriesWrapper,
        dtype: AtomicType,
        call: Callable,
        errors: str,
        **unused
    ) -> convert.SeriesWrapper:
        """Convert arbitrary data to an object data type."""
        direct = call is None
        if direct:
            call = dtype.type_def

        # object root type
        if call is object:
            return series.astype("O")

        def wrapped_call(object val):
            cdef object result = call(val)
            if direct:
                return result

            cdef type output_type = type(result)
            if output_type != dtype.type_def:
                raise ValueError(
                    f"`call` must return an object of type {dtype.type_def}"
                )
            return result

        return series.apply_with_errors(
            call=dtype.type_def,
            errors=errors,
            element_type=dtype
        )

    ###############################
    ####    UPCAST/DOWNCAST    ####
    ###############################

    @property
    def larger(self) -> list:
        """A list of types that this type can be
        :meth:`upcasted <AtomicType.upcast>` to in the event of overflow.

        Override this to change the behavior of a bounded type (with
        appropriate `.min`/`.max` fields) when an ``OverflowError`` is
        detected.

        Notes
        -----
        Candidate types will always be tested in order.
        """
        return []  # NOTE: most types cannot be upcasted

    @property
    def smaller(self) -> list:
        """A list of types that this type can be
        :meth:`downcasted <AtomicType.downcast>` to if directed.

        Override this to change the behavior of a type when the ``downcast``
        argument is supplied to a conversion function.

        Notes
        -----
        Candidate types will always be tested in order.
        """

    def upcast(self, series: convert.SeriesWrapper) -> AtomicType:
        """Upcast an :class:`AtomicType` to fit the observed range of a series.

        Parameters
        ----------
        series : SeriesWrapper
            The series to be fitted.  This is always provided as a
            :class:`SeriesWrapper` object.

        Returns
        -------
        SeriesWrapper
            The transformed series, returned as a :class:`SeriesWrapper`.

        Notes
        -----
        Upcasting occurs whenever :meth:`SeriesWrapper.boundscheck` is called
        and an ``OverflowError`` is detected.  When this occurs, we search
        :attr:`AtomicType.larger` for another type that has a wider range than
        ``self``, which we can use to represent the series without overflowing.

        This is generally relevant only for generic types and supertypes that
        have implementations/subtypes with a wider range than the default.
        This method allows these to be dynamically resized to match observed
        data.
        """
        # NOTE: we convert to python int to prevent inconsistent comparisons
        if pd.isna(series.min):
            min_val = self.max  # NOTE: we swap these to maintain upcast()
            max_val = self.min  # behavior for upcast-only types
        else:
            min_val = int(series.min - bool(series.min % 1))  # round floor
            max_val = int(series.max + bool(series.max % 1))  # round ceiling

        if min_val < self.min or max_val > self.max:
            # recursively search for a larger alternative
            for t in self.larger:
                try:
                    return t.upcast(series)
                except OverflowError:
                    pass

            # no matching type could be found
            raise OverflowError(
                f"could not upcast {self} to fit observed range "
                f"({series.min}, {series.max})"
            )

        # series fits type
        return self

    def downcast(
        self,
        series: convert.SeriesWrapper,
        tol: Tolerance,
        smallest: composite.CompositeType = None
    ) -> AtomicType:
        """Downcast an :class:`AtomicType` to compress an observed series.

        Parameters
        ----------
        series : SeriesWrapper
            The series to be fitted.  This is always provided as a
            :class:`SeriesWrapper` object.
        tol : Tolerance
            The tolerance to use for downcasting.  This sets an upper bound on
            the amount of precision loss that can occur during this operation.
        smallest : CompositeType, default None
            Forbids downcasting past the specified types.  If this is omitted,
            then downcasting will proceed to the smallest possible type rather
            than stopping early.

        Returns
        -------
        SeriesWrapper
            The transformed series, returned as a :class:`SeriesWrapper`.

        Notes
        -----
        Downcasting allows users to losslessly compress numeric data by
        reducing the precision or range of a data type.
        """
        raise NotImplementedError(
            f"'{type(self).__name__}' objects cannot be downcasted."
        )

    ##############################
    ####    MISSING VALUES    ####
    ##############################

    def is_na(self, val: Any) -> bool:
        """Check if a scalar value is equal to this type's NA representation.

        Parameters
        ----------
        val : Any
            A scalar value to check for NA equality.

        Returns
        -------
        bool
            ``True`` if ``val`` is equal to this type's ``na_value``, ``False``
            otherwise.

        Notes
        -----
        Comparison with missing values is often tricky.  Most NA values are not
        equal to themselves, so some other algorithm must be used to test for
        them.  This method allows users to define this logic on a per-type
        basis.
        """
        return val is pd.NA or val is None or val != val

    def make_nullable(self) -> AtomicType:
        """Convert a non-nullable :class:`AtomicType` into one that can accept
        missing values.

        Returns
        -------
        AtomicType
            A nullable version of this data type to be used when missing or
            coerced values are detected during a conversion.
        """
        if self.is_nullable:
            return self
        return self.generic.instance(backend="pandas", **self.kwargs)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, other: type_specifier) -> bool:
        return self.contains(other)

    def __eq__(self, other: type_specifier) -> bool:
        other = resolve.resolve_type(other)
        return isinstance(other, AtomicType) and self.hash == other.hash

    def __getattr__(self, name: str) -> Any:
        """Pass attribute lookups to ``self.kwargs``."""
        try:
            return self.kwargs[name]
        except KeyError as err:
            err_msg = (
                f"{repr(type(self).__name__)} object has no attribute: "
                f"{repr(name)}"
            )
            raise AttributeError(err_msg) from err

    @classmethod
    def __init_subclass__(cls, cache_size: int = None, **kwargs):
        """Metaclass initializer.

        This method is responsible for
        `initializing subclasses <https://peps.python.org/pep-0487/>`_ of
        :class:`AtomicType`.
        """
        # allow cooperative inheritance
        super(AtomicType, cls).__init_subclass__(**kwargs)

        # limit to 1st order
        valid = AtomicType.__subclasses__()
        if cls not in valid:
            raise TypeError(
                f"{cls.__name__} cannot inherit from another AtomicType "
                f"definition"
            )

        # init required fields
        cls.aliases.add(cls)  # cls always aliases itself
        if cache_size is None:
            cls.flyweights = {}
        else:
            cls.flyweights = LRUDict(maxsize=cache_size)

        # init fields for @subtype
        cls._children = set()
        cls._parent = None

        # init fields for @generic
        cls._generic = None
        cls._backends = {}

    def __setattr__(self, name: str, value: Any) -> None:
        if self._is_frozen:
            raise AttributeError("AtomicType objects are read-only")
        else:
            self.__dict__[name] = value

    def __hash__(self) -> int:
        return self.hash

    def __repr__(self) -> str:
        sig = ", ".join(f"{k}={repr(v)}" for k, v in self.kwargs.items())
        return f"{type(self).__name__}({sig})"

    def __str__(self) -> str:
        return self.slug


#######################
####    PRIVATE    ####
#######################


cdef void _traverse_subtypes(type atomic_type, set result):
    """Recursive helper for traverse_subtypes()"""
    result.add(atomic_type)
    for subtype in atomic_type._children:
        if subtype in atomic_type.registry:
            _traverse_subtypes(subtype, result=result)


cdef set traverse_subtypes(type atomic_type):
    """Traverse through an AtomicType's subtype tree, recursively gathering
    every subtype definition that is contained within it or any of its
    children.
    """
    cdef set result = set()
    _traverse_subtypes(atomic_type, result)  # in-place
    return result
