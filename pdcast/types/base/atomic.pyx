"""This module describes an ``AtomicType`` object, which serves as the base
of the ``pdcast`` type system.
"""
import inspect
from types import MappingProxyType
from typing import Any

cimport numpy as np
import numpy as np
import pandas as pd
from pandas.api.extensions import ExtensionDtype

from pdcast import resolve
from pdcast.util.structs cimport LRUDict
from pdcast.util.type_hints import array_like, dtype_like, type_specifier

from .registry cimport AliasManager, CacheValue
from . cimport instance
from . cimport scalar
from . cimport adapter
from . cimport composite
from pdcast.types.array import abstract


# TODO: add examples/raises for each method

# TODO: remove is_na() in favor of pd.isna() and convert make_nullable into
# a .nullable property.


# TODO: abstract dtype creation is broken due to broken code in
# traverse_subtypes().  This function may not even be necessary anymore with
# composite GenericType and ParentType classes.  These can just implement
# their own contains() methods.


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


######################
####    ATOMIC    ####
######################


cdef class AtomicTypeConstructor(scalar.ScalarType):
    """A stub class that separates internal :class:`AtomicType` constructor
    methods from the public interface.
    """

    cdef void _init_identifier(self, type subclass):
        """Create a SlugFactory to uniquely identify this type.

        Notes
        -----
        There are two possible algorithms for generating identification strings
        based on decorator configuration:

            (1) A unique name followed by a bracketed list of parameter
                strings.
            (2) A non-unique name followed by a bracketed list containing a
                unique backend specifier and zero or more parameter strings.

        The second option is chosen whenever a concrete implementation is
        registered to a :class:`GenericType` that shares the same name.  This
        is what allows us to maintain generic namespaces with unique
        identifiers.
        """
        name = subclass.name
        parameters = tuple(self._kwargs)

        # NOTE: appropriate slug generation depends on hierarchical
        # configuration.  If name is inherited from GenericType, we have to
        # append the backend specifier as the first parameter to maintain
        # uniqueness.

        if hasattr(subclass, "_backend"):
            backend = subclass._backend
            slugify = instance.BackendSlugFactory(name, parameters, backend)
        else:
            slugify = instance.SlugFactory(name, parameters)

        self._slug = slugify((), self._kwargs)
        self.slugify = slugify

    cdef void _init_instances(self, type subclass):
        """Create an InstanceFactory to control instance generation for this
        type.

        The chosen factory depends on the value of
        :attr:`AtomicType.cache_size`.
        """
        cache_size = self.cache_size

        # cache_size = 0 negates flyweight pattern
        if not cache_size:
            instances = instance.InstanceFactory(subclass)
        else:
            slugify = self.slugify
            instances = instance.FlyweightFactory(subclass, slugify, cache_size)
            instances[self._slug] = self

        self.instances = instances

    cdef void init_base(self):
        """Initialize a base (non-parametrized) instance of this type.

        See :meth:`ScalarType.init_base` for more information on how this is
        called and why it is necessary.
        """
        subclass = type(self)

        self._aliases = AliasManager(subclass.aliases | {subclass})
        self._init_identifier(subclass)
        self._init_instances(subclass)

        # clean up subclass fields
        del subclass.aliases

        subclass._base_instance = self

    cdef void init_parametrized(self):
        """Initialize a parametrized instance of this type.

        See :meth:`ScalarType.init_parametrized` for more information on how
        this is called and why it is necessary.
        """
        base = type(self)._base_instance

        self._aliases = base._aliases
        self.slugify = base.slugify
        self._slug = self.slugify((), self._kwargs)
        self.instances = base.instances


cdef class AtomicType(AtomicTypeConstructor):
    """Abstract base class for all user-defined scalar types.

    :class:`AtomicTypes <AtomicType>` are the most fundamental unit of the
    ``pdcast`` type system.  They are used to describe scalar values of a
    particular type (i.e. :class:`int <python:int>`, :class:`numpy.float32`,
    etc.), and are responsible for defining all the necessary implementation
    logic for :doc:`dispatched </content/api/attach>` methods,
    :doc:`conversions </content/api/cast>`, and
    :ref:`type-related <AtomicType.required>` functionality at the scalar
    level.

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
    All in all, a typical :class:`AtomicType` definition could look something like
    this:

    .. code:: python

        @pdcast.register
        @ParentType.subtype
        @GenericType.implementation("backend")  # inherits .name
        class ImplementationType(pdcast.AtomicType, cache_size=128):

            aliases = {"foo", "bar", "baz", np.dtype(np.int64), int, ...}
            type_def = int
            dtype = np.dtype(np.int64)
            itemsize = 8
            na_value = pd.NA

            def __init__(self, x, y):
                # custom arg parsing goes here, along with any new attributes
                super().__init__(x=x, y=y)  # no new attributes after this point

            @classmethod
            def slugify(cls, x, y) -> str:
                return f"cls.name[{str(x)}, {str(y)}]"

            # additional customizations/dispatch methods as needed

    Where ``ParentType`` and ``GenericType`` reference other :class:`AtomicType`
    definitions that ``ImplementationType`` is linked to.
    """

    # NOTE: this is a sample __init__ method for a parametrized type.
    # Non-parametrized types can omit __init__ entirely.

    # def __init__(self, foo=None, bar=None):
    #     if foo is not None:
    #         ...
    #     if bar is not None:
    #         ...
    #     super.__init__(foo=foo, bar=bar)

    # This automatically assigns foo and bar as parametrized attributes of
    # the inheriting type and keeps track of any instances that are generated
    # with them.  They must be optional, and the type must be constructable
    # without arguments to be considered valid.

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

    @property
    def cache_size(self) -> int:
        """The number of flyweights to store in this type's cache.

        Notes
        -----
        There are 3 possible algorithms for caching flyweights according to the
        value of this attribute.

            (1) ``cache_size < 0`` (the default): simple cache using a hash map
                with string identifiers as keys and
                :class:`AtomicType <pdcast.AtomicType>` instances as values.
            (2) ``cache_size == 0``: no instance caching.  Effectively disables
                the flyweight pattern.  Not recommended for general use.
            (3) ``cache_size > 0`` same as (1) but with a fixed-size map and a
                Least Recently Used (LRU) caching strategy.  Instances will be
                evicted if they cause the map to grow past the given size.

        """
        return -1

    @property
    def type_def(self) -> type | None:
        """The scalar class for objects of this type.

        Returns
        -------
        type | None
            A class object used to instantiate scalar examples of this type.
        """
        # TODO: raise NotImplementedError?
        return None

    @property
    def dtype(self) -> np.dtype | ExtensionDtype:
        """The numpy :class:`dtype <numpy.dtype>` or pandas
        :class:`ExtensionDtype <pandas.api.extensions.ExtensionDtype>` to use
        for arrays of this type.

        Returns
        -------
        numpy.dtype | pandas.api.extensions.ExtensionDtype
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
        if not self._dtype:
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
        return None

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
        return pd.NA

    @property
    def is_numeric(self) -> bool:
        """Used to auto-generate :class:`AbstractDtypes <pdcast.AbstractDtype>`
        from this type.
        """
        return False

    @property
    def is_nullable(self) -> bool:
        """Indicates whether a type supports missing values.

        Set this ``False`` where necessary to invoke :meth:`make_nullable
        <AtomicType.make_nullable>`.  This allows automatic conversion to a
        nullable alternative when missing values are detected/coerced.
        """
        return True

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

    #########################
    ####    TRAVERSAL    ####
    #########################

    # TODO: is_root = True, root = self, supertype = None, subtypes = {self}
    # these are overloaded in ParentType

    @property
    def is_root(self) -> bool:
        """Indicates whether this type is the root of its
        :func:`@subtype <subtype>` hierarchy.

        Returns
        -------
        bool
            ``True`` if this type has no :attr:`supertype
            <AtomicType.supertype>`, ``False`` otherwise.
        """
        return self._parent is None

    @property
    def root(self) -> AtomicType:
        """The root node of this type's subtype hierarchy."""
        if self.is_root:
            return self
        return self.supertype.root

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
    def supertype(self) -> AtomicType:
        """An :class:`AtomicType` representing the supertype that this type is
        registered to, if one exists.

        Notes
        -----
        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        cached = self._supertype_cache
        if not cached:
            cached = CacheValue(self._generate_supertype(self._parent))
            self._supertype_cache = cached

        return cached.value

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

    @property
    def subtypes(self) -> composite.CompositeType:
        """A :class:`CompositeType` containing every subtype that is
        currently registered to this type.

        Notes
        -----
        The result of this accessor is cached between :class:`TypeRegistry`
        updates.
        """
        cached = self._subtype_cache
        if not cached:
            result = composite.CompositeType(
                self._generate_subtypes(traverse_subtypes(type(self)))
            )
            cached = CacheValue(result)
            self._subtype_cache = cached

        return cached.value

    @property
    def is_generic(self) -> bool:
        """Indicates whether this type is decorated with
        :func:`@generic <generic>`.
        """
        return False

    @property
    def generic(self) -> AtomicType:
        """The generic equivalent of this type, if one exists."""
        return getattr(self, "_generic", None)

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
        # NOTE: this is overridden in ParentType/GenericType
        yield from ()

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
        # NOTE: this is overridden in ParentType/GenericType
        yield from ()

    ##########################
    ####    MEMBERSHIP    ####
    ##########################

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
                subtypes = set(backend.subtypes) - {self}
                if any(s.contains(other) for s in subtypes):
                    return True

        return False

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


def generic(cls: type) -> GenericType:
    """Class decorator to mark generic type definitions.

    Generic types are backend-agnostic and act as wildcard containers for
    more specialized subtypes.  For instance, the generic "int" can contain
    the backend-specific "int[numpy]", "int[pandas]", and "int[python]"
    subtypes, which can be resolved as shown. 
    """
    if not issubclass(cls, AtomicType):
        raise TypeError("@generic types must inherit from AtomicType")

    # NOTE: cython __init__ is not introspectable.
    if (
        cls.__init__ != AtomicType.__init__ and
        inspect.signature(cls).parameters
    ):
        raise TypeError("@generic types cannot be parametrized")

    # print(dir(cls))

    return GenericType(cls)


# TODO: supertype() needs to be able to decorate GenericType objects.


def supertype(cls: type) -> ParentType:
    """Class decorator to mark parent type definitions.

    Supertypes are nodes within the ``pdcast`` type system.
    """
    pass


cdef class HierarchicalType(AtomicType):
    """A Composite Pattern type object that can contain other types.
    """

    def __init__(self, cls):
        self.__wrapped__ = cls()
        self._default = self.__wrapped__
        self._kwargs = {}
        self._slug = self.__wrapped__._slug
        self._hash = self.__wrapped__._hash

        self._aliases = self.__wrapped__._aliases
        self.slugify = self.__wrapped__.slugify
        self.instances = self.__wrapped__.instances

        self._read_only = True

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def resolve(self, *args: str) -> AtomicType:
        """Forward constructor arguments to the appropriate implementation."""
        return self._default.resolve(*args)

    def detect(self, example: Any) -> AtomicType:
        """Forward scalar inference to the default implementation."""
        return self._default.detect(example)

    def from_dtype(self, dtype: dtype_like) -> AtomicType:
        """Forward dtype translation to the default implementation."""
        return self._default.from_dtype(dtype)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def name(self) -> str:
        """Return the name of the decorated type."""
        return self.__wrapped__.name

    @property
    def aliases(self) -> AliasManager:
        """Return the aliases of the decorated type."""
        return self.__wrapped__.aliases

    @property
    def type_def(self) -> type | None:
        """Delegate `type_def` lookups to the default implementation."""
        return self._default.type_def

    @property
    def dtype(self) -> np.dtype | ExtensionDtype:
        """Delegate `dtype` lookups to the default implementation."""
        return self._default.dtype

    @property
    def itemsize(self) -> int | None:
        """Delegate `itemsize` lookups to the default implementation."""
        return self._default.itemsize

    @property
    def na_value(self) -> Any:
        return self._default.na_value

    @property
    def is_numeric(self) -> bool:
        """Delegate `is_numeric` lookups to the default implementation."""
        return self._default.is_numeric

    #########################
    ####    TRAVERSAL    ####
    #########################

    # TODO: delegate all AtomicType/ScalarType attributes to default.

    ##########################
    ####    MEMBERSHIP    ####
    ##########################

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getattr__(self, name: str) -> Any:
        return getattr(self._default, name)

    def __call__(self, *args, **kwargs):
        if not (args or kwargs):
            return self
        return self.instances(*args, **kwargs)

    def __repr__(self) -> str:
        return repr(self.__wrapped__)


cdef class GenericType(HierarchicalType):
    """A hierarchical type that can contain other types as implementations.
    """

    def __init__(self, cls):
        super().__init__(cls)
        self._backends = {None: self._default}

    ##########################
    ####    OVERLOADED    ####
    ##########################

    @property
    def is_generic(self) -> bool:
        """True by definition."""
        return True

    @property
    def generic(self) -> AtomicType:
        """self by definition."""
        return self

    def resolve(self, backend: str | None = None, *args) -> AtomicType:
        """Forward constructor arguments to the appropriate implementation."""
        if backend is None:
            return self
        return self._backends[backend].resolve(*args)    

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def backends(self) -> MappingProxyType:
        """A mapping of all backend specifiers to their corresponding
        concretions.
        """
        return MappingProxyType(self._backends)

    @property
    def default_implementation(self) -> AtomicType:
        """The concrete type that this generic type defaults to.

        This will be used whenever the generic type is specified without an
        explicit backend.
        """
        return self._default

    @default_implementation.setter
    def default_implementation(self, backend: str) -> None:
        self._default = self._backends[backend]

    @default_implementation.deleter
    def default_implementation(self) -> None:
        self._default = self.__wrapped__

    ##########################
    ####    DECORATORS    ####
    ##########################

    def implementation(
        self,
        backend: str,
        default: bool = False
    ):
        """A class decorator that registers a type definition as an
        implementation of this type.

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

        def decorator(cls: type) -> type:
            """Link the decorated type to this GenericType."""
            if not issubclass(cls, AtomicType):
                raise TypeError("@generic types can only contain AtomicTypes")

            if cls.name is AtomicType.name:
                cls.name = self.name


            cls._generic = self  # for AtomicType.generic
            if cls.name == self.name:
                cls._backend = backend  # for AtomicType.slugify

            def promise(instance):
                """A promise that registers the base implementation with this
                GenericType.
                """
                if backend in self._backends:
                    raise TypeError(
                        f"backend specifier must be unique: {repr(backend)} "
                        f"is already registered to "
                        f"{repr(self._backends[backend])}"
                    )
                self._backends[backend] = instance
                if default:
                    self.default_implementation = backend

            cls.registry.promises.setdefault(cls, []).append(promise)
            return cls

        return decorator


cdef class ParentType(HierarchicalType):
    """A hierarchical type that can contain other types as subtypes.
    """

    def __init__(self, cls):
        super().__init__(cls)
        self._subtypes = set()

    #################################
    ####    COMPOSITE PATTERN    ####
    #################################

    @property
    def default_subtype(self) -> AtomicType:
        """The subtype that this parent type defaults to.

        This will be used in place of the parent type during attribute access.
        """
        return self._default

    @default_subtype.setter
    def default_subtype(self, subtype: AtomicType) -> None:
        if subtype not in self._subtypes:
            raise KeyError(f"{subtype}")
        self._default = subtype

    @default_subtype.deleter
    def default_subtype(self) -> None:
        self._default = self.__wrapped__

    ##########################
    ####    DECORATORS    ####
    ##########################

    def subtype(self, cls: type | None = None):
        """A class decorator that adds a type as a subtype of this GenericType.
        """
        def decorator(cls: type) -> type:
            """Link the decorated type to this GenericType."""
            if not issubclass(cls, AtomicType):
                raise TypeError("@generic types can only contain AtomicTypes")

            if cls._parent:
                raise TypeError(
                    f"subtypes can only be registered to one parent at a "
                    f"time: '{cls.__qualname__}' is currently registered to "
                    f"'{cls._parent.__qualname__}'"
                )

            typ = cls
            while typ is not None:
                if typ is self:
                    raise TypeError("@generic type cannot contain itself")
                typ = typ._parent

            # TODO: use promises

            cls._parent = self
            self._subtypes |= cls()
            self.registry.flush()
            return cls

        return decorator


# TODO: move traversal into CompositeType.expand()


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
