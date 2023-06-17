.. currentmodule:: pdcast

.. _DecoratorType:

pdcast.DecoratorType
====================

.. autoclass:: DecoratorType

.. raw:: html
    :file: ../../images/types/Types_UML.html

.. _DecoratorType.constructors:

Constructors
------------
These are automatically called by :func:`detect_type() <pdcast.detect_type>`
and :func:`resolve_type() <pdcast.resolve_type>` to create instances of the
associated type.

.. autosummary::
    :toctree: ../../generated

    DecoratorType.from_string
    DecoratorType.from_dtype
    DecoratorType.kwargs
    DecoratorType.replace
    DecoratorType.__call__

.. _DecoratorType.membership:

Membership
----------
These are called by :func:`typecheck` and :func:`@dispatch <dispatch>` to
perform membership tests.

.. autosummary::
    :toctree: ../../generated

    DecoratorType.contains
    DecoratorType.__contains__

.. _DecoratorType.decorators:

Decorators
----------
:class:`DecoratorTypes <DecoratorType>` implement the `Decorator pattern
<https://en.wikipedia.org/wiki/Decorator_pattern>`_, dynamically wrapping
another type's behavior.  These attributes are used to access the wrapped type
and traverse any decorators that have been applied to it.

.. autosummary::
    :toctree: ../../generated

    DecoratorType.wrapped
    DecoratorType.decorators
    DecoratorType.unwrap
    DecoratorType.__getattr__
    DecoratorType.__setattr__

.. note::

    Any attributes not listed in this interface are automatically delegated to
    the :attr:`wrapped <DecoratorType.wrapped>` type via
    :meth:`__getattr__() <DecoratorType.__getattr__>`.

.. _DecoratorType.transform:

Data transformations
--------------------

.. autosummary::
    :toctree: ../../generated

    DecoratorType.transform
    DecoratorType.inverse_transform

.. _DecoratorType.special:

Special Methods
---------------

.. autosummary::
    :toctree: ../../generated

    DecoratorType.__instancecheck__
    DecoratorType.__subclasscheck__
    DecoratorType.__hash__
    DecoratorType.__eq__
    DecoratorType.__str__


.. TODO: discussion on parsing fill_value and levels should be moved into
    the type index.



``DecoratorType``\s are types that modify other types.  These include sparse and
categorical types, which provide a wrapper on top of a base ``ScalarType``
instance, adding information related to fill values and levels, respectively.
These must be provided at least one argument (the type being wrapped), which
can be another ``DecoratorType`` specifier, allowing them to be arbitrarily
nested.

Here are some examples of basic adapter types:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[int]")
    SparseType(wrapped=IntegerType(), fill_value=<NA>)
    >>> pdcast.resolve_type("sparse[str[pyarrow]]")
    SparseType(wrapped=PyArrowStringType(), fill_value=<NA>)
    >>> pdcast.resolve_type("categorical[bool]")
    CategoricalType(wrapped=BooleanType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[bool]]")
    SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=<NA>)

By default, sparse types use the base type's ``na_value`` field to determine
the ``fill_value``, but this can be manually specified by adding an additional
argument.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[bool, True]")
    SparseType(wrapped=BooleanType(), fill_value=True)
    >>> pdcast.resolve_type("sparse[int, -32]")
    SparseType(wrapped=IntegerType(), fill_value=-32)
    >>> pdcast.resolve_type("sparse[decimal, 4.68]")
    SparseType(wrapped=DecimalType(), fill_value=Decimal('4.68'))

Note that the second argument is provided as a string, but is resolved to an
object of the same type as the base.  This is thanks to ``pdcast``\s robust
suite of type conversions!  In fact, any string that can be converted to the
base type can be accepted here.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("sparse[bool, y]")
    SparseType(wrapped=BooleanType(), fill_value=True)
    >>> pdcast.resolve_type("sparse[datetime[pandas], Jan 12 2022 at 7:00 AM]")
    SparseType(wrapped=PandasTimestampType(tz=None), fill_value=Timestamp('2022-01-12 07:00:00'))

This is similar for categorical types, except that the second argument must be
a sequence, each element of which is resolved to form the levels of the
categorical type.

.. doctest:: type_resolution

    >>> pdcast.resolve_type("categorical[bool, [y, n]]")
    CategoricalType(wrapped=BooleanType(), levels=[True, False])
    >>> pdcast.resolve_type("categorical[int, [1, 2, 3]]")
    CategoricalType(wrapped=IntegerType(), levels=[1, 2, 3])
    >>> pdcast.resolve_type("categorical[decimal, [1.23, 2.34]]")
    CategoricalType(wrapped=DecimalType(), levels=[Decimal('1.23'), Decimal('2.34')])

.. note::

    These conversions use the **default** values for ``cast()`` operations.  If
    you'd like to change how these are interpreted, modify the defaults using
    ``cast.defaults``.
