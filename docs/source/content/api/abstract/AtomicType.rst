
.. currentmodule:: pdcast

pdcast.AtomicType
=================

.. class:: AtomicType2(**kwargs)

    Abstract base class for all user-defined scalar types.

    :class:`AtomicTypes <AtomicType>` are the most fundamental unit of the
    ``pdcast`` type system.  They are used to describe scalar values of a
    particular type (i.e. ``int``, ``numpy.float32``, etc.), and are responsible
    for defining all the necessary implementation logic for dispatched methods,
    conversions, and type-related functionality at the scalar level.

    :param \*\*kwargs:
        Arbitrary keyword arguments describing metadata for this type.  If a
        subclass accepts arguments in its ``__init__`` method, they should
        always be passed here via ``super().__init__(**kwargs)``.  This is
        conceptually equivalent to the ``_metadata`` field of pandas
        `ExtensionDtype <https://pandas.pydata.org/pandas-docs/stable/reference/api/pandas.api.extensions.ExtensionDtype.html>`_
        objects.
    :type \*\*kwargs: dict

    .. attribute:: name

        A unique name for each type, which must be defined at the class level.
        This is used in conjunction with :meth:`slugify() <AtomicType.slugify>`
        to generate string representations of the associated type.  Names can
        also be inherited from :func:`generic <generic>` types via
        :meth:`@AtomicType.register_backend <AtomicType.register_backend>`.

        :type: str

    .. attribute:: aliases

        A set of unique aliases for this type, which must be defined at the
        class level.  These are used by :func:`detect_type` and
        :func:`resolve_type` to map aliases onto their corresponding types.

        .. note::

            Special significance is given to the type of each alias:

            *   Strings are used by the :ref:`type specification mini-language
                <mini_language>` to trigger :meth:`resolution
                <AtomicType.resolve>` of the associated type.
            *   Numpy/pandas ``dtype``\ /\ ``ExtensionDtype`` objects are used
                by :func:`detect_type` for *O(1)* type inference.  In both
                cases, parametrized dtypes can be handled by adding a root
                dtype to ``aliases``.  For numpy ``dtypes``, this will be the
                root of their ``np.issubdtype()`` hierarchy.  For pandas
                ``ExtensionDtypes``, it is its ``type()`` directly.  When
                either of these are encountered, they will invoke the
                :class:`AtomicType`'s :meth:`from_dtype()
                <AtomicType.from_dtype>` constructor.
            *   Raw Python types are used by :func:`detect_type` for scalar or
                unlabeled vector inference.  If the type of a scalar element
                appears in ``aliases``, then the associated type's
                :meth:`detect() <AtomicType.detect>` method will be called on
                it.

        All aliases are recognized by :func:`resolve_type` and the set always
        includes the :class:`AtomicType` itself.

        :type: set[str | type | np.dtype | ExtensionDtype]

    .. attribute:: type_def

        The scalar class for objects of this type.

        :type: type | None

    .. attribute:: dtype

        The numpy ``dtype`` or pandas ``ExtensionDtype`` to use for arrays of
        this type.  If this is not explicitly given, ``pdcast`` will
        automatically generate an ``ExtensionDtype`` according to the `pandas
        extension api <https://pandas.pydata.org/pandas-docs/stable/development/extending.html>`_.

        .. note::

            Auto-generated ``ExtensionDtypes`` store data internally as a
            ``dtype: object`` array, which may not be the most efficient.  If
            there is a more compact representation for a particular data type,
            users can provide their own ``ExtensionDtypes`` instead.

        :type: np.dtype | ExtensionDtype

    .. attribute:: itemsize

        The size (in bytes) for scalars of this type.  ``None`` is interpreted
        as being resizable/unlimited.

        :type: int | None

    .. attribute:: na_value

        The representation to use for missing values of this type.

        :type: Any

    .. rubric:: Notes

    .. _atomic_type.inheritance:

    :class:`AtomicTypes <AtomicType>` are `metaclasses <https://peps.python.org/pep-0487/>`_
    that are limited to **first-order inheritance**.  This means that they must
    inherit from :class:`AtomicType` *directly*, and cannot have any children
    of their own.  For example:

    .. code:: python

        class Type1(pdcast.AtomicType):   # valid
            ...

        class Type2(Type1):   # invalid
            ...

    If you'd like to share functionality between types, this can be done using
    `Mixin classes <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_,
    like so:

    .. code:: python

        class Mixin:
            # shared attributes/methods go here
            ...

        class Type1(Mixin, pdcast.AtomicType):
            ...

        class Type2(Mixin, pdcast.AtomicType):
            ...

    .. note::

        Note that ``Mixin`` comes **before** :class:`AtomicType` in each
        inheritance signature.  This ensures correct `Method Resolution Order
        (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.

    .. _atomic_type.allocation:

    Additionally, :class:`AtomicType` instances are `flyweights
    <https://python-patterns.guide/gang-of-four/flyweight/>`_ that are
    identified by their :meth:`slug <AtomicType.slugify>` attribute.  This
    allows them to be extremely memory-efficient (especially when stored in
    arrays) but also requires each one to be completely immutable.  As a
    result, all :class:`AtomicTypes <AtomicType>` are strictly **read-only**
    after they are constructed.

    .. testsetup:: allocation

        import pdcast

    .. doctest:: allocation

        >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
        True
        >>> pdcast.resolve_type("int").new_attribute = 2
        Traceback (most recent call last):
            ...
        AttributeError: AtomicType objects are read-only

    Some types might be parameterized with continuous or unpredictable inputs,
    which could cause `memory leaks <https://en.wikipedia.org/wiki/Memory_leak>`_.
    In these cases, users can specify a `Least Recently Used (LRU)
    <https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
    caching strategy by passing an appropriate ``cache_size`` parameter in the
    type's inheritance signature, like so:

    .. code:: python

        class CustomType(pdcast.AtomicType, cache_size=128):
            ...

    .. note::

        Setting ``cache_size`` to 0 effectively eliminates flyweight caching for
        the type in question, though this is not recommended.

    .. rubric:: Examples

    All in all, a typical :class:`AtomicType` definition could look something like
    this:

    .. code:: python

        @pdcast.register
        @pdcast.subtype(ParentType)
        @GenericType.register_backend("backend name")  # inherits .name
        class BackendType(pdcast.AtomicType, cache_size=128):

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
    definitions that ``BackendType`` is linked to.
