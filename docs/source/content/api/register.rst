.. currentmodule:: pdcast

.. testsetup::

    import pdcast

pdcast.register
===============

.. autodecorator:: register

.. _register.init:

Initialization
--------------
This decorator instantiates the decorated type, converting it into a base
instance that serves as a factory for other instances of the same type.

.. doctest::

    >>> @pdcast.register
    ... class CustomType(pdcast.ScalarType):
    ...     """A user-defined extension type."""
    ... 
    ...     name = "foo"
    ...     aliases = {"bar"}
    ... 
    ...     def __init__(self, x=None):
    ...         super().__init__(x=x)

    >>> CustomType
    CustomType(x=None)
    >>> isinstance(CustomType, type)
    False

In most cases, this base instance can be used interchangeably with the class
itself, including for :func:`isinstance() <python:isinstance>` and
:func:`issubclass() <python:issubclass>` checks.

.. doctest::

    >>> isinstance(CustomType, CustomType)
    True
    >>> issubclass(type(CustomType), CustomType)
    True

New instances can be created by calling the base instance just like a normal
class object.

.. doctest::

    >>> CustomType("baz")
    CustomType(x='baz')

This enforces the :ref:`flyweight <ScalarType.flyweight>` protocol
automatically, ensuring memory efficiency.

.. doctest::

    >>> CustomType("baz") is CustomType("baz")
    True

Additionally, any aliases that are defined on the parent class will
automatically be transfered to the base instance, making them accessible from
:func:`detect_type() <pdcast.detect_type>` and
:func:`resolve_type() <pdcast.resolve_type>`.

.. doctest::

    >>> pdcast.resolve_type("bar")
    CustomType(x=None)
    >>> pdcast.resolve_type("bar") is CustomType
    True
    >>> pdcast.resolve_type("bar[baz]") is CustomType("baz")
    True

.. _register.validation:

Validation
----------
A type cannot be initialized unless the following conditions are met:

    *   The class's :attr:`name <ScalarType.name>` must be unique or
        inherited from an abstract type (via
        :meth:`AbstractType.implementation <pdcast.AbstractType.implementation>`).
    *   If given, The class's :attr:`aliases <ScalarType.aliases>` must
        contain unique specifiers of a recognizable type.

If either of these conditions are broken, then an error will be raised
explaining the conflict.

.. testsetup::

    pdcast.registry.remove(CustomType)

.. doctest::

    >>> @pdcast.register
    ... class CustomType(pdcast.ScalarType):
    ...     """A user-defined extension type."""
    ... 
    ...     name = "int"   # reserved for IntegerType
    ...     aliases = {"bar"}
    ... 
    ...     def __init__(self, x=None):
    ...         super().__init__(x=x)
    ... 
    Traceback (most recent call last):
        ...
    TypeError: CustomType(x=None) name must be unique: 'int' is currently registered to IntegerType()

.. _register.conditional:

Conditional types
-----------------
Conditional types can be declared using the ``cond`` argument of
:func:`@register <pdcast.register>`.  

.. doctest::

    >>> @pdcast.register(cond=False)
    ... class CustomType(pdcast.ScalarType):
    ...     """A user-defined extension type."""
    ... 
    ...     name = "foo"
    ...     aliases = {"bar"}
    ... 
    ...     def __init__(self, x=None):
    ...         super().__init__(x=x)

Unless the condition evaluates to ``True``, these types will not be added to
the registry, and will therefore not be accessible from
:func:`detect_type() <pdcast.detect_type>` or
:func:`resolve_type() <pdcast.resolve_type>`.

.. doctest::

    >>> pdcast.resolve_type("bar")
    Traceback (most recent call last):
        ...
    ValueError: invalid specifier: 'bar'

They will also not be instantiated or validated, preserving them as explicit
class objects.

.. doctest::

    >>> CustomType
    <class 'CustomType'>

In general, conditionals like these are useful for modeling types that are
platform-specific or rely on an external dependency for some or all of their
behavior.  By using the conditional syntax, we can include these types without
resorting to import hacks, making importing and manipulating them more
straightforward.

.. note::

    Unregistered types can always be instantiated and
    :meth:`added <pdcast.TypeRegistry.add>` to the registry manually if
    necessary.

    .. doctest::

        >>> pdcast.registry.add(CustomType)
        >>> pdcast.resolve_type("bar")
        CustomType(x=None)
