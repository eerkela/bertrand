.. currentmodule:: pdcast

.. _generic:

pdcast.generic
==============

.. autodecorator:: generic


For generic types, the process is somewhat different.  These types do not
implement their own ``.resolve()`` methods; they instead inherit them from the
``@generic`` decorator itself.  This comes with the restriction that the
generic type does not accept any arguments of its own, which is guaranteed by
the decorator at import time.  The inherited ``.resolve()`` method works as
follows:

#.  If provided, the first argument must be a registered backend of the generic
    type.
#.  Any additional arguments are passed on to the specified implementation's
    ``.resolve()`` method as if it were called directly.

This allows generic types to shift perspectives during the resolution process,
and enables constructs of the form:

.. doctest:: type_resolution

    >>> pdcast.resolve_type("int8[pandas]")
    PandasInt8Type()
    >>> pdcast.resolve_type("datetime[pandas, US/Pacific]")
    PandasTimestampType(tz=<DstTzInfo 'US/Pacific' LMT-1 day, 16:07:00 STD>)
    >>> pdcast.resolve_type("datetime[python, UTC]")
    PythonDatetimeType(tz=<UTC>)
    >>> pdcast.resolve_type("datetime[numpy, 5ns]")
    NumpyDatetime64Type(unit='ns', step_size=5)
    >>> pdcast.resolve_type("timedelta[numpy, s]")
    NumpyTimedelta64Type(unit='s', step_size=1)




Backends
^^^^^^^^
.. TODO: this goes in actual @generic stub

AtomicTypes can also be marked as being generic, allowing them to serve as
containers for individual backends.  This can be done by appending an
``@generic`` decorator to its class definition, like so:

.. code:: python

    @pdcast.generic
    class Type1(pdcast.AtomicType):
        ...

In order to qualify as a generic type, an ``AtomicType`` must not implement a
custom ``__init__()`` method.  Once marked, backends can be added to a generic
type by calling its ``@register_backend()`` decorator, as shown:

.. code:: python

    @Type1.register_backend("<backend name>")
    class Type2(pdcast.AtomicType):
        ...

This allows ``Type2`` to be resolved from ``Type1`` by passing the specified
backend string during ``resolve_type()`` calls.  It also adds ``Type2`` to
``Type1.subtypes``, and automatically includes it in membership checks.

.. note::

    The backend string provided to ``@register_backend()`` must be unique.




