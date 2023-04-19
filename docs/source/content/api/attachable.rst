.. currentmodule:: pdcast

.. testsetup::

    from pdcast import *

pdcast.attachable
=================

.. autodecorator:: attachable

.. _attachable.decorated:

Decorated functions
-------------------
:func:`@attachable <attachable>` transforms the decorated callable into an
:class:`Attachable` object, which can be called exactly like the original.
These objects expose the following attributes for public use, in addition to
any that are present in the wrapped callable itself.

.. autosummary::
    :toctree: ../../generated

    Attachable
    Attachable.attached
    Attachable.attach_to

.. _attachable.namespace:

Namespaces
----------
:class:`Namespaces <Namespace>` are automatically created by the ``namespace``
argument of :meth:`Attachable.attach_to`.

.. autosummary::
    :toctree: ../../generated

    Namespace
    Namespace.original
    Namespace.detach
    Namespace.__get__

.. _attachable.method:

Instance methods
----------------
:class:`BoundMethods <BoundMethod>` are created by :meth:`Attachable.attach_to`
with ``pattern="method"``.

.. autosummary::
    :toctree: ../../generated

    BoundMethod
    BoundMethod.original
    BoundMethod.detach
    BoundMethod.__get__

.. _attachable.example:

Examples
--------
This decorator allows users to :meth:`attach <Attachable.attach_to>` any Python 
function as a `bound <https://realpython.com/python-descriptors/#python-descriptors-in-methods-and-functions>`_
attribute of an external class.  Here's an example:

.. doctest::

    >>> class MyClass:
    ...     def __init__(self, x):
    ...         self.x = x
    ... 
    ...     def __repr__(self):
    ...         return f"MyClass({self.x})"

    >>> @attachable
    ... def foo(data):
    ...     print("Hello, World!")
    ...     return data

    >>> foo.attach_to(MyClass)

This creates a new attribute ``MyClass.foo``, which is a self-binding wrapper
for the decorated function.  It behaves exactly like a normal instance method
of ``MyClass``.  For example:

.. doctest::

    >>> MyClass.foo   # doctest: +SKIP
    <function foo at 0x7fb5d383c4c0>
    >>> MyClass(1).foo()
    Hello, World!
    MyClass(1)

If we invoke ``MyClass.foo`` as a class method (i.e. without instantiating
``MyClass`` first), then we get the same behavior as the naked ``foo``
function.

.. doctest::

    >>> MyClass.foo()
    Traceback (most recent call last):
        ...
    TypeError: foo() missing 1 required positional argument: 'data'
    >>> MyClass.foo(MyClass(1))
    Hello, World!
    MyClass(1)

If ``MyClass`` defines its own ``foo`` method, then our attached method will
mask the original.

.. doctest::

    >>> class MyClass:
    ...     def __init__(self, x):
    ...         self.x = x
    ... 
    ...     def __repr__(self):
    ...         return f"MyClass({self.x})"
    ... 
    ...     def foo(self):
    ...         print("Goodbye, World!")
    ...         return self

    >>> foo.attach_to(MyClass)
    >>> MyClass(1).foo()
    Hello, World!
    MyClass(1)

We can recover the masked implementation by accessing the method's
:attr:`original <BoundMethod.original>` attribute.

.. doctest::

    >>> MyClass(1).foo.original()
    Goodbye, World!
    MyClass(1)

Note that this is not done by default, so any naive reference to
``MyClass.foo`` will automatically use our overloaded implementation, even if
that reference is internal to ``MyClass``.  This could lead to unexpected
behavior if our overloaded implementation returns a different result than the
method it masks.  In order to avoid this, we can either rename our overloaded
method:

.. testsetup::

    MyClass.foo.detach()

.. doctest::

    >>> foo.attach_to(MyClass, name="bar")
    >>> MyClass.bar   # doctest: +SKIP
    <function foo at 0x7f50672f04c0>
    >>> MyClass(1).bar()
    Hello, World!
    MyClass(1)

Or hide it behind a virtual :class:`Namespace`, which separates it from the
existing attributes of ``MyClass``.

.. testsetup::

    MyClass.bar.detach()

.. doctest::

    >>> foo.attach_to(MyClass, namespace="baz")
    >>> MyClass.baz   # doctest: +SKIP
    <pdcast.func.virtual.Namespace object at 0x7fe73dd10af0>
    >>> MyClass.baz.foo   # doctest: +SKIP
    <function foo at 0x7fe73dd1c4c0>
    >>> MyClass(1).baz.foo()
    Hello, World!
    MyClass(1)

Both of these approaches leave the original ``foo`` method untouched.

.. doctest::

    >>> MyClass(1).foo()
    Goodbye, World!
    MyClass(1)

In addition, we can


We can remove our attached attribute by calling its
:meth:`detach() <BoundMethod.detach>` method, which replaces the original
implementation if one exists.

.. doctest::

    >>> MyClass.baz.foo.detach()
    >>> MyClass.baz.foo
    Traceback (most recent call last):
        ...
    AttributeError: type object 'MyClass' has no attribute 'baz'

This automatically removes the namespace if it is not managing any
additional attributes.

.. _attachable.nested:

Nesting decorators
------------------
We can also modify defaults and add arguments to our
:class:`BoundMethod <pdcast.BoundMethod>` just like normal.

.. doctest::

    >>> @MyClass.foo.register_arg
    ... def baz(val: int, defaults: dict) -> int:
    ...     return int(val)

    >>> MyClass.foo.baz
    2
    >>> MyClass.foo.baz = 3
    >>> MyClass().foo()
    (MyClass(), 3)

.. warning::

    Any arguments (as well as their default values) will be shared
    between the :class:`BoundMethod <pdcast.BoundMethod>` and its
    base :class:`ExtensionFunc <pdcast.ExtensionFunc>`.

    .. doctest::

        >>> foo.baz
        3

    This might lead to unexpected changes in behavior if not properly
    accounted for.

And can register dispatched implementations without much fuss.

.. doctest::

    >>> @MyClass.foo.register_type(types="int")
    ... def boolean_foo(bar: bool, baz: bool = True, **kwargs):
    ...     return (bar, baz)

    >>> foo(False)
    (False, True)


