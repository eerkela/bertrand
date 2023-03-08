AdapterType
===========
AdapterTypes are types that modify other types.  They can dynamically wrap
``AtomicType`` objects to change their behavior on a programmatic basis and
are not cached as flyweights like ``AtomicType``\s are.

``AdapterType``\s are examples of the Gang of Four `Decorator Pattern <https://python-patterns.guide/gang-of-four/decorator-pattern/>`_
(not to be confused with the built-in python ``@decorator`` syntax).  This
leverages `composition over inheritance <https://en.wikipedia.org/wiki/Composition_over_inheritance>`_,
to prevent subclass explosions for type objects.  They can be nested to form
a singly-linked list that sits on top of the base ``AtomicType`` object,
successively wrapping it to modify its behavior.

Common use cases for ``AdapterType``\s include sparse and categorical data.
These are handled by ``pdcast.SparseType`` and ``pdcast.CategoricalType``
respectively, which can be applied to any ``AtomicType`` object to convert it
into a sparse/categorical representation.  They are designed to be as
minimally-intrusive as possible, and can be constructed in the same way as
``AtomicTypes`` via the type-specification mini-language, as shown:

.. doctest::

    >>> import pdcast
    >>> pdcast.resolve_type("sparse[float32]")
    SparseType(wrapped=Float32Type(), fill_value=nan)
    >>> pdcast.resolve_type("categorical[string]")
    CategoricalType(wrapped=StringType(), levels=None)
    >>> pdcast.resolve_type("sparse[categorical[bool]]")
    SparseType(wrapped=CategoricalType(wrapped=BooleanType(), levels=None), fill_value=<NA>)

If you'd like to write your own ``AdapterType``, the best place to start would
be to examine these types to see how they work.
