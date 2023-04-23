.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    from pdcast.decorators.extension import *

pdcast.extension_func
=====================

.. autodecorator:: extension_func

.. _extension_func.basic:

Extension functions
-------------------
:func:`@extension_func <extension_func>` transforms the decorated callable into
an :class:`ExtensionFunc` object, which supports dynamic arguments and default
values.  This adds the following attributes to the decorated function's
interface, in addition to any that are present on the callable itself (see
:ref:`nested decorators <attachable.nested>`).

.. autosummary::
    :toctree: ../../generated

    ExtensionFunc
    ExtensionFunc.validators
    ExtensionFunc.default_values
    ExtensionFunc.register_arg
    ExtensionFunc.remove_arg
    ExtensionFunc.reset_defaults

The behavior of the original function is otherwise unchanged.

.. doctest::

    >>> @extension_func
    ... def foo(bar, baz=2, **kwargs):
    ...     return bar, baz

    >>> foo
    foo(bar, baz=2, **kwargs)
    >>> foo(1)
    (1, 2)
    >>> foo(1, 3)
    (1, 3)
    >>> foo(1, 3, qux=4)
    (1, 3)
    >>> foo()
    Traceback (most recent call last):
        ...
    TypeError: foo() missing 1 required positional argument: 'bar'

.. _extension_func.validator:

Managed arguments
-----------------
We can manage the values that are supplied to our :class:`ExtensionFunc` by
defining :meth:`validators <ExtensionFunc.register_arg>` for one or more of its
arguments.

.. doctest::

    >>> @foo.register_arg
    ... def bar(val: int, state: dict) -> int:
    ...     return int(val)

.. note::

    The name of the validator must match the name of the argument it validates.

This validator will be implicitly executed whenever ``bar`` is supplied to
``foo()``, before it is passed into the body of the function itself.

.. doctest::

    >>> foo("a", 2)
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'
    >>> foo("1", 2)
    (1, 2)

.. _extension_func.default:

Default Values
--------------
:class:`ExtensionFunc` also allows us to programmatically assign and modify
default values for our managed arguments.

.. doctest::

    >>> foo.bar = 1
    >>> foo
    foo(bar = 1, baz = 2, **kwargs)
    >>> foo.bar
    1
    >>> foo()
    (1, 2)

.. note::

    We can also do this by supplying a ``default`` argument to the
    :func:`@register_arg <ExtensionFunc.register_arg>` decorator.

    .. testsetup::

        foo.remove_arg("bar")

    .. doctest::

        >>> @foo.register_arg(default=1)
        ... def bar(val: int, state: dict) -> int:
        ...     return int(val)

        >>> foo.bar
        1

    Or by assigning a default value in the signature of ``foo()`` itself, as
    with ``baz``:

    .. doctest::

        >>> @foo.register_arg
        ... def baz(val: int, state: dict) -> int:
        ...     return int(val)

        >>> foo.baz
        2

These default values can be updated at run time to change the behavior of
``foo()`` on a global basis.

.. doctest::

    >>> foo.bar = 24
    >>> foo.baz = -17
    >>> foo()
    (24, -17)

They can be reset to their default using :meth:`ExtensionFunc.reset_defaults`
or by simply deleting the attribute:

.. doctest::

    >>> foo
    foo(bar = 24, baz = -17, **kwargs)
    >>> del foo.bar, foo.baz
    >>> foo
    foo(bar, baz = 2, **kwargs)
    >>> foo(1)
    (1, 2)

.. note::

    Unless a default is assigned in
    :meth:`@register_arg <ExtensionFunc.register_arg>` or the signature
    of ``foo()`` itself, deleting the argument will make it required
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

The values themselves are **thread-local**.  This means that if the
:class:`ExtensionFunc` is referenced from a child thread, a new instance will
be dynamically created with arguments and defaults from the main thread.  That
instance can then be modified without affecting the behavior of other threads.

.. doctest::

    >>> import random
    >>> import threading
    >>> random.seed(5)
    >>> values = list(np.arange(-50, 50))

    >>> def worker():
    ...     foo.bar, foo.baz = random.sample(values, 2)
    ...     print(foo())

    >>> threads = [threading.Thread(target=worker) for _ in range(3)]
    >>> for t in threads:   # doctest: +SKIP
    ...     t.start()
    (29, -18)
    (44, -5)
    (38, 44)
    >>> foo()
    (1, 2)

.. _extension_func.extension:

Extension arguments
-------------------
If the decorated callable allows variable-length keyword arguments
(``**kwargs``), then :class:`@register_arg <pdcast.ExtensionFunc.register_arg>`
allows us to add new arguments dynamically at run time. These use the same
validation and default value logic as above. 

.. doctest::

    >>> @foo.register_arg(default=3)
    ... def qux(val: int, state: dict) -> int:
    ...     return int(val)

    >>> foo
    foo(bar = 1, baz = 2, qux = 3, **kwargs)
    >>> foo(1, qux="a")
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'

They are passed dynamically into the base function's ``**kwargs`` attribute.

.. note::

    If a dynamic argument does not specify a default value, then it will
    **not** be passed through to ``**kwargs`` unless it is explicitly
    specified.

.. _extension_func.application:

Applications
------------
Imagine you have a whole :doc:`type system's <../types/types>` worth of
:doc:`robust <detect_type>` and :doc:`extendable <AtomicType>` type
:doc:`checks <typecheck>` and :doc:`conversions <cast>` at your disposal.

.. doctest::

    >>> @extension_func
    ... def my_func(foo: Any, **kwargs) -> int:
    ...     return cast(foo, int, **kwargs)

    >>> @my_func.register_arg
    ... def foo(val: Any, state: dict) -> datetime_like:
    ...     return cast(val, "datetime")

    >>> my_func("today", unit="D", since="April 5th, 2022")   # doctest: +SKIP
    0   373
    dtype: int[python]

.. _extension_func.source:

Source
------
:func:`@extension_func <extension_func>` is implemented in pure Python and does
not require any ``pdcast``\-specific functionality to work.  It is available as
a generic :doc:`recipe <source/extension_func>`.

.. toctree::
    :hidden:

    source <source/extension_func>
