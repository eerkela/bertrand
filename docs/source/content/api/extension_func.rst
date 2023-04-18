.. currentmodule:: pdcast

.. testsetup::

    import numpy as np
    import pandas as pd
    from pdcast.func import *

pdcast.extension_func
=====================
An extension function is a normal Python function that supports dynamic writes
to its arguments and default values.

.. autodecorator:: extension_func

.. _extension_func.basic:

Creating extension functions
----------------------------
By default, the decorated function behaves exactly like the original.

.. doctest::

    >>> @extension_func
    ... def foo(bar, baz=2, **kwargs):
    ...     return bar, baz

    >>> foo
    foo(bar, baz = 2, **kwargs)
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
    ... def bar(val: int, defaults: dict) -> int:
    ...     return int(val)

.. note::

    The name of the validator must match the name of the argument it validates.

This validator will be implicitly executed whenever ``bar`` is supplied to
``foo()``, and its output will then be passed into the body of ``foo()``.

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
:class:`ExtensionFunc` also allows us to programmatically assign and/or modify
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
        ... def bar(val: int, defaults: dict) -> int:
        ...     return int(val)

        >>> foo.bar
        1

    Or by assigning a default value in the signature of ``foo()`` itself, as with
    ``baz``:

    .. doctest::

        >>> @foo.register_arg
        ... def baz(val: int, defaults: dict) -> int:
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

Additionally, the values themselves are **thread-local**.  If the
:class:`ExtensionFunc` is referenced from a child thread, a new instance will
be dynamically created with arguments and defaults from the main thread.  This
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

.. _extension_func.dynamic:

Dynamic arguments
-----------------
:func:`@register_arg <ExtensionFunc.register_arg>` also allows us to
dynamically add new arguments to ``foo()`` at run time, with the same
validation logic as before.

.. doctest::

    >>> @foo.register_arg(default=3)
    ... def qux(val: int, defaults: dict) -> int:
    ...     return int(val)

    >>> foo
    foo(bar = 1, baz = 2, qux = 3, **kwargs)
    >>> foo(1, qux="a")
    Traceback (most recent call last):
        ...
    ValueError: invalid literal for int() with base 10: 'a'

These are passed dynamically into the base function's ``**kwargs`` attribute.

.. note::

    If a dynamic argument does not specify a default value, then it will
    **not** be passed through to ``**kwargs`` unless it is explicitly
    specified.


.. 

    .. _extension_func.method:

    Extension methods
    -----------------
    :class:`ExtensionFuncs <ExtensionFunc>` can also be dynamically patched into
    other Python objects as :class:`ExtensionMethods <ExtensionMethod>`.  This
    can be done via :meth:`ExtensionFunc.attach_to`.

    .. doctest::

        >>> class MyClass:
        ...     def __int__(self) -> int:
        ...         print("Hello, World!")
        ...         return 4

        >>> foo.attach_to(MyClass)

    This creates a new attribute of ``MyClass`` under ``MyClass.foo``, which
    references our original extension function.  Whenever we invoke it this way, an
    instance of ``MyClass`` will be implicitly passed as its first argument.

    .. doctest::

        >>> MyClass.foo
        MyClass.foo(baz = 2, qux = 3, **kwargs)
        >>> MyClass().foo()
        Hello, World!
        (4, 2)

    If we invoke ``MyClass.foo`` as a class method (i.e. without instantiating
    ``MyClass`` first), then we get the same behavior as the naked ``foo``
    function.

    .. doctest::

        >>> MyClass.foo()
        (1, 2)

.. _extension_func.application:

Where to go from here?
----------------------
Imagine you have a whole package's worth of :doc:`robust <detect_type>` and
:doc:`extendable <AtomicType>` data :doc:`conversions <cast>` and a full 
:doc:`type system <../types/types>` at your disposal.

.. doctest::

    >>> @extension_func
    ... def my_func(foo: Any, **kwargs) -> int:
    ...     return cast(foo, int, **kwargs)

    >>> @my_func.register_arg
    ... def foo(val: Any, defaults: dict) -> datetime_like:
    ...     return cast(val, "datetime")

    >>> my_func("today", unit="D", since="April 5th, 2022")   # doctest: +SKIP
    0   373
    dtype: int[python]

.. _extension_func.internal:

Internals
---------

.. autosummary::
    :toctree: ../../generated

    ExtensionFunc
    ExtensionFunc.validators
    ExtensionFunc.default_values
    ExtensionFunc.register_arg
    ExtensionFunc.remove_arg
    ExtensionFunc.reset_defaults
    ExtensionMethod

.. _extension_func.source:

Source
------
:func:`@extension_func <extension_func>` is implemented as a standard recipe in
base Python.  It does not require any special ``pdcast``\-specific
functionality to work, and is available as a generic
:doc:`recipe <source/extension_func>`.

.. toctree::
    :hidden:

    source <source/extension_func>
