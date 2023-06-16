.. currentmodule:: pdcast

.. testsetup::

    import pdcast

.. _ScalarType:

pdcast.ScalarType
=================

.. autoclass:: ScalarType

.. raw:: html
    :file: ../../images/types/Types_UML.html

.. _ScalarType.constructors:

Constructors
------------
These are automatically called by :func:`detect_type` and :func:`resolve_type`
to create instances of the associated type.

.. autosummary::
    :toctree: ../../generated

    ScalarType.from_string
    ScalarType.from_dtype
    ScalarType.from_scalar
    ScalarType.kwargs
    ScalarType.replace
    ScalarType.__call__

The resulting instances are immutable `flyweights
<https://en.wikipedia.org/wiki/Flyweight_pattern>`_ that are allocated
once and then cached for the duration of the program.

.. doctest::

    >>> pdcast.resolve_type("int") is pdcast.resolve_type("int")
    True
    >>> pdcast.resolve_type("int").new_attribute = 2
    Traceback (most recent call last):
        ...
    AttributeError: ScalarType objects are read-only

.. note::

    Some types might be parameterized with continuous or unpredictable inputs,
    which could cause `memory leaks <https://en.wikipedia.org/wiki/Memory_leak>`_
    if not addressed.  In these cases, users can specify a `Least Recently Used
    (LRU) <https://en.wikipedia.org/wiki/Cache_replacement_policies#Least_recently_used_(LRU)>`_
    caching strategy by defining an appropriate :attr:`_cache_size` in a type's
    definition.

    .. code:: python

        class CustomType(pdcast.ScalarType):

            _cache_size = 128
            ...

    Setting ``_cache_size = 0`` effectively disables the flyweight protocol,
    though this is not recommended.

.. _ScalarType.membership:

Membership
----------
These are called by :func:`typecheck` and :func:`@dispatch <dispatch>` to
perform membership tests.

.. autosummary::
    :toctree: ../../generated

    ScalarType.contains
    ScalarType.__contains__

.. _ScalarType.config:

Configuration
-------------
:class:`ScalarTypes <ScalarType>` can implement the following attributes to
customize their behavior.

.. autosummary::
    :toctree: ../../generated

    ScalarType.name
    ScalarType.aliases
    ScalarType.type_def
    ScalarType.dtype
    ScalarType.itemsize
    ScalarType.is_numeric
    ScalarType.max
    ScalarType.min
    ScalarType.is_nullable
    ScalarType.na_value
    ScalarType.make_nullable
    ScalarType.__getattr__
    ScalarType.__setattr__

..
    HACK - commenting out an autosummary directive like this will still
    generate stubs, which can then be linked inside ScalarType.dtype.

    .. autosummary::
        :toctree: ../../generated

        ObjectDtype
        ObjectArray  

.. _ScalarType.traversal:

Traversal
---------
:class:`ScalarTypes <pdcast.ScalarType>` can be embedded into
:ref:`abstract hierarchies <AbstractType.hierarchy>` that can be traversed with
the following properties.

.. autosummary::
    :toctree: ../../generated

    ScalarType.is_root
    ScalarType.root
    ScalarType.supertype
    ScalarType.generic
    ScalarType.implementations
    ScalarType.subtypes
    ScalarType.is_leaf
    ScalarType.leaves

.. _ScalarType.downcast:

Upcast/Downcast
---------------
They can also be dynamically resized based on example data, preventing
overflow and minimizing memory usage.

.. autosummary::
    :toctree: ../../generated

    ScalarType.larger
    ScalarType.smaller
    ScalarType.__lt__
    ScalarType.__gt__

.. _ScalarType.decorators:

Decorators
----------
Lastly, :class:`ScalarTypes <pdcast.ScalarType>` can be wrapped with
:class:`DecoratorTypes <pdcast.DecoratorType>` to adjust their behavior on a
case-by-case basis.

.. autosummary::
    :toctree: ../../generated

    ScalarType.decorators
    ScalarType.unwrap
    ScalarType.make_sparse
    ScalarType.make_categorical

.. _ScalarType.special:

Special Methods
---------------
:class:`ScalarTypes <pdcast.ScalarType>` are `hashable
<https://en.wikipedia.org/wiki/Hash_function>`_, and can be contained as
elements of :class:`sets <python:set>` or keys in a :class:`dict <python:dict>`.

.. autosummary::
    :toctree: ../../generated

    ScalarType.__instancecheck__
    ScalarType.__subclasscheck__
    ScalarType.__hash__
    ScalarType.__eq__
    ScalarType.__str__
