.. _implementation.atomic_type:

AtomicType
==========
The base ``AtomicType`` definition is a `metaclass <https://peps.python.org/pep-0487/>`_
that is aware of its own subtypes.  Whenever a class inherits from it,
``AtomicType`` introspects various attributes of that class, bringing it into
a safe state, ready for integration with the rest of the package.  This allows
users to define new ``AtomicType``\s on the fly simply by inheriting from it.
There are, however, a few caveats to this process, which are described below.

Inheritance
-----------
The first is that ``AtomicType`` is limited to **first-order inheritance**.
This means that objects must inherit from ``AtomicType`` *directly*, and no
other objects can inherit from that object in turn.  For example,

.. code:: python

    class Type1(pdcast.AtomicType):
        ...

    class Type2(Type1):
        ...

would be invalid, since ``Type2`` inherits from ``Type1``, which inherits
from ``AtomicType``.  In contrast,

.. code:: python

    class Type1(pdcast.AtomicType):
        ...

    class Type2(pdcast.AtomicType):
        ...

is perfectly acceptable, since both types inherit from ``AtomicType``, and do
not inherit from each other.  The reasoning for this is to prevent
unintentional collisions between AtomicType attributes, which may differ
between parent and child classes.  Instead, users are encouraged to link their
dependent types using the ``@subtype()`` decorator like so:

.. code:: python

    @pdcast.subtype(Type1)
    class Type2(pdcast.AtomicType):
        ...

Which adds ``Type2`` to ``Type1.subtypes``.  If you'd like to share
functionality between ``Type1`` and ``Type2``, this can be done using
`Mixin classes <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_,
like so:

.. code:: python

    class Mixin:
        # shared attributes/methods go here
        ...

    class Type1(Mixin, pdcast.AtomicType):
        ...

    @pdcast.subtype(Type1)
    class Type2(Mixin, pdcast.AtomicType):
        ...

.. note::

    Note that the Mixin class comes **before** ``AtomicType`` in each
    inheritance signature.  This ensures correct
    `Method Resolution Order (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.

.. _flyweight:

Memory Allocation
-----------------
The second caveat is that ``AtomicType`` instances are
`flyweights <https://python-patterns.guide/gang-of-four/flyweight/>`_.  This
means that they are only constructed once, and are **automatically cached**
for the duration of the program.  If the same type is requested again, it will
simply return a **reference** to the original instance, ensuring that only one
version of each type will exist at any given time.  This allows them to be
extremely memory-efficient, especially when stored in arrays.  The downside of
this is that each type must be completely immutable, since any changes
introduced at runtime would corrupt the flyweight cache and lead to collisions.
As a result, all AtomicTypes are strictly **read-only** after they are
constructed.

.. testsetup:: allocation

    import pdcast

.. doctest:: allocation

    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").new_attribute = 2
    Traceback (most recent call last):
        ...
    AttributeError: AtomicType objects are read-only

Some types might have continuous or unpredictable inputs, which could overwhelm
the shared flyweight cache if requested in sufficient numbers.  In these cases,
types can specify a `Least Recently Used (LRU) <https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
caching strategy by passing an appropriate ``cache_size`` parameter in their
inheritance signature, like so:

.. code:: python

    class CustomType(pdcast.AtomicType, cache_size=128):
        ...

.. note::

    Setting ``cache_size`` to 0 effectively eliminates caching for that type,
    although this is not recommended.

Required Attributes
-------------------

.. TODO: include a subsection for each of these and remove Description column

Lastly, every ``AtomicType`` must define certain **required fields** before
they can be integrated with the rest of the package.  These are as follows:

.. list-table::
    :header-rows: 1
    :align: center

    * - Attribute
      - Type
      - Default
      - Description
    * - ``.name``
      - ``str``
      - 
      - This must be unique, and is used when generating string representations
        of the associated type.  It can also be inherited from generic types
        via ``generic.register_backend()`` if it is not specified manually.
    * - ``.aliases``
      - ``set``
      - 
      - This contains all the various strings, types, and numpy/pandas dtype
        objects that are recognized by ``resolve_type()`` and
        ``detect_type()``.  Each of these must be unique across the space of
        all aliases, and the set always includes the AtomicType itself.
    * - ``.type``
      - ``type``
      - ``None``
      - The scalar class definition for objects of this type.
    * - ``.dtype``
      - ``np.dtype``, ``ExtensionDtype``
      - ``np.dtype("O")``
      - The numpy/pandas ``dtype`` object that corresponds to this
        ``AtomicType``.
    * - ``.itemsize``
      - ``int``
      - ``None``
      - The size (in bytes) for objects of this type.  ``None`` is interpreted
        as being unlimited.
    * - ``.na_value``
      - Any
      - ``pd.NA``
      - The representation to use for missing values of this type.  This must
        pass a ``pd.isna()`` check.
    * - ``.slugify``
      - classmethod
      - :doc:`inherited <../../generated/pdcast.AtomicType.slugify>`
      - This is a classmethod that describes how to generate string labels for
        the associated type.  It must have the same argument signature as the
        type's ``__init__()`` method, and its output determines how flyweights
        are identified, so care must be taken to ensure that each output is
        unique.  If a type does not implement a custom ``__init__()`` method,
        this can be safely omitted.

Registration
------------
Once an appropriate ``AtomicType`` definition has been created, it can be
automatically integrated with the rest of the package by finishing its
decorator stack with ``@register``, which verifies its configuration and
appends it to the registry.  Once registered, the type should be recognized by
``resolve_type()`` using all of its given aliases, and will be detectable from
example data of the appropriate type(s).  Any dispatched methods it defines
will also be dynamically attached to ``pd.Series`` objects of that type.

All in all, a typical AtomicType definition could look something like this:

.. code:: python

    @register
    @subtype(ParentType)
    @CustomType.register_backend("backend name")  # inherits name
    class BackendType(AtomicType, cache_size=128):

        aliases = {"foo", "bar", "baz", np.dtype(np.int64), int, ...}
        type = int
        dtype = np.dtype(np.int64)
        itemsize = 8
        na_value = pd.NA

        def __init__(self, x, y):
            # custom arg processing goes here, along with any new attributes
            super().__init__(x=x, y=y)  # no new attributes after this point

        @classmethod
        def slugify(cls, x, y) -> str:
            return f"cls.name[{str(x)}, {str(y)}]"

        # additional customizations/dispatch methods as needed

Default Behavior
----------------
``AtomicType`` provides significant out of the box functionality for its
subclasses, giving users a robust framework to work from.  If you'd like to
write your own type definition, feel free to peruse the base type and
prepackaged AtomicTypes for examples.
