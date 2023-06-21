.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    from pdcast.decorators.extension import *

.. _extension_func:

pdcast.extension_func
=====================

.. autodecorator:: extension_func

.. raw:: html
    :file: ../../images/decorators/Decorators_UML.html

.. _extension_func.basic:

Extension functions
-------------------
The :func:`@extension_func <extension_func>` decorator transforms a function
into an :class:`ExtensionFunc` object, which supports dynamic arguments and
default values.  These have the following interface,
:ref:`in addition <attachable.nested>` to that of the function itself.

.. autosummary::
    :toctree: ../../generated

    ExtensionFunc
    ExtensionFunc.arguments
    ExtensionFunc.settings
    ExtensionFunc.argument
    ExtensionFunc.remove_arg
    ExtensionFunc.reset_defaults
    ExtensionFunc.__call__

The behavior of the decorated function is otherwise unchanged.

.. doctest::

    >>> @extension_func
    ... def foo(bar, baz=2, **kwargs):
    ...     return {"bar": bar, "baz": baz} | kwargs

    >>> foo
    foo(bar, baz=2, **kwargs)
    >>> foo(1)
    {'bar': 1, 'baz': 2}
    >>> foo(1, 3)
    {'bar': 1, 'baz': 3}
    >>> foo()
    Traceback (most recent call last):
        ...
    TypeError: foo() missing 1 required positional argument: 'bar'

.. _extension_func.managed:

Managed arguments
-----------------
We can manage the values that are supplied to our :class:`ExtensionFunc` by
defining validators for one or more of its arguments.  These can be assigned
using the :func:`@ExtensionFunc.argument <ExtensionFunc.argument>` decorator,
like so:

.. doctest::

    >>> @foo.argument
    ... def bar(val, context: dict) -> int:
    ...     return int(val)

.. warning::

    The name of the validator must match the name of the argument that it
    validates.

This validator will be implicitly executed whenever the ``bar`` argument is
supplied to ``foo()``.  Its output is passed into the body of ``foo()``
in-place, effectively replacing the original value.

.. doctest::

    >>> foo(1)
    {'bar': 1, 'baz': 2}
    >>> foo("1")  # string -> int
    {'bar': 1, 'baz': 2}
    >>> foo("a")
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'

In this way, we can separate argument validation from the body of ``foo()``
itself, making it easier to maintain and extend as we add new features.

.. _extension_func.settings:

Default Values
--------------
:class:`ExtensionFuncs <ExtensionFunc>` also allow us to programmatically
assign and modify default values for each of our managed arguments.

.. doctest::

    >>> foo.bar = 1
    >>> foo
    foo(bar=1, baz=2, **kwargs)
    >>> foo()  # no arguments
    {'bar': 1, 'baz': 2}

.. note::

    We can also do this by supplying the ``default`` argument to the
    :func:`@argument <ExtensionFunc.argument>` decorator.

    .. testsetup::

        foo.remove_arg("bar")

    .. doctest::

        >>> @foo.argument(default=4)
        ... def bar(val, context: dict) -> int:
        ...     return int(val)

        >>> foo.bar
        4

    .. testcleanup::

        # replace original bar() validator
        foo.remove_arg("bar")

        @foo.argument
        def bar(val, context: dict) -> int:
            return int(val)

        foo.bar = 1

    Or by assigning a default value in the signature of ``foo()`` itself, as
    with ``baz``:

    .. doctest::

        >>> @foo.argument
        ... def baz(val, context: dict) -> int:
        ...     return int(val)

        >>> foo.baz
        2

These values can be updated at run time to dynamically change the behavior of
``foo()`` on a global basis.

.. doctest::

    >>> foo.bar = 24
    >>> foo.baz = -17
    >>> foo()
    {'bar': 24, 'baz': -17}

We can reset them to their hardcoded defaults using
:meth:`ExtensionFunc.reset_defaults` or by simply deleting the attribute:

.. doctest::

    >>> foo
    foo(bar=24, baz=-17, **kwargs)
    >>> del foo.bar, foo.baz
    >>> foo
    foo(bar, baz=2, **kwargs)
    >>> foo(1)
    {'bar': 1, 'baz': 2}

.. warning::

    Unless a ``default`` value is assigned in
    :meth:`@ExtensionFunc.argument <ExtensionFunc.argument>` or the signature
    of ``foo()`` itself, resetting the argument will make it required
    whenever ``foo()`` is invoked.

    .. doctest::

        >>> foo()
        Traceback (most recent call last):
            ...
        TypeError: foo() missing 1 required positional argument: 'bar'

    Note that this does not remove the underlying validator.

    .. doctest::

        >>> foo("a")
        Traceback (most recent call last):
            ...
        ValueError: invalid literal for int() with base 10: 'a'

    To purge a managed argument entirely, use :meth:`ExtensionFunc.remove_arg`.

These values are **thread-local**, meaning that if the :class:`ExtensionFunc`
is referenced from a child thread, a new instance will be created with the
arguments and default values of the main thread.  This copy can then be
modified independently, without affecting the behavior of other threads.

.. doctest::

    >>> import random
    >>> import threading

    >>> random.seed(5)
    >>> values = list(range(-50, 50))

    >>> def worker():
    ...     foo.bar, foo.baz = random.sample(values, 2)
    ...     print(foo())  # using random values for each thread

    >>> threads = [threading.Thread(target=worker) for _ in range(3)]
    >>> for t in threads:
    ...     t.start()
    {'bar': 29, 'baz': -18}
    {'bar': 44, 'baz': -5}
    {'bar': 38, 'baz': 44}
    >>> foo()  # using values from main thread
    Traceback (most recent call last):
        ...
    TypeError: foo() missing 1 required positional argument: 'bar'

.. _extension_func.extension:

Extension arguments
-------------------
Lastly, if the decorated callable includes **variable-length keyword arguments**
(``**kwargs``), then its :class:`ExtensionFunc` will allow us to define new
arguments dynamically at runtime.  These use the same pattern as above for
validation and default values, but they are not hardcoded into the signature of
the function itself.  We can create one of these by doing the following:

.. doctest::

    >>> @foo.argument(default=3)
    ... def qux(val, context: dict) -> int:
    ...     # NOTE: 'qux' is not mentioned in the signature of 'foo()'
    ...     return int(val)

    >>> foo
    foo(bar, baz=2, qux=3, **kwargs)
    >>> foo(1)
    {'bar': 1, 'baz': 2, 'qux': 3}

This gives ``foo()`` an entirely new argument ``qux``, which is passed into
the function via its ``**kwargs`` dict.  It has a default value of ``3`` and,
just like any other managed argument, it will be validated according the
``qux()`` function we gave in its definition.

.. doctest::

    >>> foo(1, qux="4")  # string -> int
    {'bar': 1, 'baz': 2, 'qux': 4}
    >>> foo(1, qux="a")
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'

If a dynamic argument does not specify a default value, then it will **not** be
passed through to ``**kwargs`` unless it is explicitly specified.

.. doctest::

    >>> @foo.argument  # NOTE: no default value
    ... def corge(val, context: dict) -> int:
    ...     return int(val)

    >>> foo  # ``corge`` is not listed
    foo(bar, baz=2, qux=3, **kwargs)
    >> foo(1)  # no ``corge`` in output
    {'bar': 1, 'baz': 2, 'qux': 3}
    >>> foo(1, corge="4")  # string -> int
    {'bar': 1, 'baz': 2, 'qux': 3, 'corge': 4}
    >>> foo(1, corge="a")
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'

We could theoretically add any number of these dynamic arguments, each with
their own logic.  They will all be passed through ``**kwargs`` into the body of
the function, where they can be processed or passed on to other callables in
turn.

.. note::

    This is particularly useful for overloaded implementations of a
    :func:`@dispatch` function, which can intercept arguments simply by
    referencing them in their call signature:

    .. doctest::

        >>> @extension_func
        ... @dispatch("x")
        ... def func(x, **kwargs):
        ...     return x

        >>> @func.argument(default=2)
        ... def y(val, context: dict) -> int:
        ...     return int(val)

        >>> @func.overload("int")
        ... def integer_func(x: int, y: int, **kwargs):
        ...     return x + y

        >>> func(1)  # NOTE: y is automatically passed to integer_func
        3
        >>> func(1, y=4)
        5
        >>> func(1.0)  # the behavior of other types is unchanged
        1.0

    This allows us to add arguments specific to one or more of ``func``'s
    implementations without affecting any of the others.

.. _extension_func.application:

Applications
------------
Imagine you have a whole :doc:`type system's <../types/types>` worth of
:doc:`robust <detect_type>` and :doc:`extendable <AtomicType>` type
:doc:`checks <typecheck>` and :doc:`conversions <cast>` at your disposal!

.. doctest::

    >>> @extension_func
    ... def my_func(date, **kwargs) -> int:
    ...     return cast(date, int, **kwargs)

    >>> @my_func.argument
    ... def date(val, context: dict) -> datetime_like:
    ...     return cast(val, "datetime")

    >>> my_func("today", unit="D", since="April 5th, 2022")   # doctest: +SKIP
    0   373
    dtype: int[python]

.. _extension_func.source:

Source
------
:func:`@extension_func <extension_func>` is implemented in pure Python and does
not require any ``pdcast``\-specific functionality to work.  It is available as
a generic :doc:`recipe <source/extension_func>` for use in other projects.

.. toctree::
    :hidden:

    Recipe <source/extension_func>
