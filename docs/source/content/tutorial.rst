.. currentmodule:: pdcast

.. _tutorial:

Tutorial: bfloat16
==================
This tutorial will walk through the steps necessary to define and integrate a
new data type into the ``pdcast`` ecosystem.  By the end of it, you should be
able to build arbitrary object types to use with :func:`cast`,
:func:`typecheck`, and other dispatched methods as you see fit.

.. _tutorial.description:

"Brain" floats
--------------
The ``bfloat16`` data type was `developed by Google <https://arxiv.org/pdf/1905.12322.pdf>`_
as a high-performance alternative to `half- <https://en.wikipedia.org/wiki/Half-precision_floating-point_format>`_
and `single-precision <https://en.wikipedia.org/wiki/Single-precision_floating-point_format>`_
floating point numbers.  These "brain" floats have more bits devoted to their
exponents than their `IEEE 754 <https://en.wikipedia.org/wiki/IEEE_754>`_
alternatives, allowing them to represent a wider range of values at the cost of
reduced precision.  This tradeoff is desirable in the case of machine learning
`cost computations <https://en.wikipedia.org/wiki/Gradient_descent>`_, where
precision is relatively unimportant compared to speed and memory efficiency.

`TensorFlow <https://www.tensorflow.org/>`_, one of the primary frameworks for
deep learning, allows us to implement these ``bfloat16`` types in normal
Python:

.. doctest::

    >>> import tensorflow as tf
    >>> bfloat16 = tf.bfloat16.as_numpy_dtype
    >>> bfloat16(1.2)
    1.20312
    >>> print(f"{np.float16(1.2):.6f}")  # equivalent float16 for comparison
    1.200195

This tutorial will describe the process of exposing these objects to pandas via
``pdcast``.

.. _tutorial.new_type:

Creating a new type definition
------------------------------
The first order of business when defining a new data type is to create a
subclass of :class:`AtomicType`.  This can be done from anywhere in your code
base - the new type will be automatically discovered during registration.

We'll start from the bottom and work our way up.  

.. doctest::

    >>> import numpy as np
    >>> import pdcast

    >>> class BFloat16Type(pdcast.AtomicType):
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float", bfloat16, tf.bfloat16}
    ...    type_def = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

This gives us a rudimentary type object that contains all the necessary
information to construct ``bfloat16``-based :class:`AtomicTypes <AtomicType>`.

.. doctest::

    >>> BFloat16Type.instance()
    BFloat16Type()
    >>> BFloat16Type.instance() is BFloat16Type.instance()
    True
    >>> BFloat16Type.flyweights
    {'bfloat16': BFloat16Type()}

Note that just because we inherited from :class:`AtomicType`, the new type has
not yet been added to :func:`resolve_type` or :func:`detect_type` operations.

.. doctest::

    >>> pdcast.resolve_type("bfloat16")
    Traceback (most recent call last):
        ...
    ValueError: could not interpret type specifier: 'bfloat16'
    >>> pdcast.detect_type(bfloat16(1.2))
    ObjectType(type=<class 'bfloat16'>)

To change this, we must :func:`register <register>` the type,
:ref:`validating <AtomicType.required>` its structure and linking its
:ref:`aliases <AtomicType.required>` to the aforementioned functions.

.. doctest::

    >>> @pdcast.register
    ... class BFloat16Type(pdcast.AtomicType):
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float", bfloat16, tf.bfloat16}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

If the above does not trigger a ``TypeError``, then our type is considered
valid and will be accepted by :func:`resolve_type` and :func:`detect_type`. Now
when we run these functions again, they should return our new ``BFloat16Type``
objects.

.. doctest::

    >>> pdcast.resolve_type("bfloat16")
    BFloat16Type()
    >>> pdcast.detect_type(bfloat16(1.2))
    BFloat16Type()
    >>> pdcast.detect_type(bfloat16(1.2)) is pdcast.resolve_type("bfloat16")
    True

.. _tutorial.subtypes:

Registering subtypes
--------------------
Theoretically, if all we wanted to do was detect and resolve our new
``BFloat16Type`` objects, then we could stop here.  However, it would be nice
to integrate them with the existing type hierarchies.

Currently, our ``BFloat16Type`` exists as a *root type*.  This means that it
has no supertypes, and is not included in any other type's ``.subtypes`` tree.

.. doctest::

    >>> pdcast.resolve_type("bfloat16").supertype is None
    True
    >>> pdcast.resolve_type("float").contains("bfloat16")
    False

We can fix this by appending an :func:`@subtype <subtype>` decorator to our
``BFloat16Type`` definition, which specifies it as a member of the
:class:`FloatType` family.

.. testcleanup::

    # first we have to remove the previous definition.  Usually this isn't
    # necessary, since types are only defined once.
    import pdcast
    pdcast.AtomicType.registry.remove(BFloat16Type)

.. doctest::

    >>> @pdcast.register
    ... @pdcast.subtype(pdcast.FloatType)
    ... class BFloat16Type(pdcast.AtomicType):
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float", bfloat16, tf.bfloat16}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

Now, if we repeat our membership checks, we see that ``BFloat16Type`` has been
added to the :class:`FloatType` hierarchy.

.. doctest::

    >>> pdcast.resolve_type("bfloat16").supertype
    FloatType()
    >>> pdcast.resolve_type("float").contains("bfloat16")
    True

If we were to visualize this, the :func:`@subtype <subtype>` decorator would
take us from this:

.. image:: /images/bfloat16_tutorial_before_subtyping.svg

To this:

.. image:: /images/bfloat16_tutorial_after_subtyping.svg

.. _tutorial.backends:

Allowing multiple backends
--------------------------
So far, we have a perfectly usable ``BFloat16Type`` for the purposes of
:func:`typecheck` tests, provided that ``tf.bfloat16`` objects are the only
ones we ever encounter.  What if that's not the case?

TensorFlow isn't the only framework that defines this type.
`PyTorch <https://pytorch.org/>`_, for instance, defines its own ``bfloat16``
implementation that may or may not share the same functionality as its
TensorFlow equivalent.  To account for this and maintain our existing
``BFloat16Type`` functionality, we can introduce it as a generic type.  This
can be done by adding an :func:`@generic <generic>` decorator to our class
definition and creating a new *implementation type* to refer to the TensorFlow
version explicitly.

.. testcleanup::

    # first we have to remove the previous definition.  Usually this isn't
    # necessary, since types are only defined once.
    pdcast.AtomicType.registry.remove(BFloat16Type)

.. doctest::

    >>> @pdcast.register
    ... @pdcast.generic
    ... @pdcast.subtype(pdcast.FloatType)
    ... class BFloat16Type(pdcast.AtomicType):  # generic interface
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float"}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

    >>> @pdcast.register
    ... @BFloat16Type.register_backend("tensorflow")
    ... class TensorFlowBFloat16Type(pdcast.AtomicType):  # tensorflow implementation
    ...    aliases = {bfloat16, tf.bfloat16}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

This gives us two separate types that are linked together via the
``"tensorflow"`` backend.  This deserves some explanation.

Functionally, these types are practically equivalent.  They share the same
name, type, itemsize, and na_value, and neither of them implement any special
logic to distinguish between them.   The only thing that changes are the
aliases for each type.  The literal ``tf.bfloat16`` definitions move from the
generic type to the implementation type.  This subtly changes the behavior of
:func:`resolve_type` and :func:`detect_type`:

.. doctest::

    >>> pdcast.resolve_type("bfloat16")
    BFloat16Type()
    >>> pdcast.resolve_type("bfloat16[tensorflow]")
    TensorFlowBFloat16Type()
    >>> pdcast.resolve_type(bfloat16)
    TensorFlowBFloat16Type()
    >>> pdcast.detect_type(bfloat16(1.2))
    TensorFlowBFloat16Type()

The real differences come when we introduce a third type,
``PyTorchBFloat16Type``.

.. doctest::

    >>> import torch

    >>> @pdcast.register
    ... @BFloat16Type.register_backend("pytorch")
    ... class PytorchBFloat16Type(pdcast.AtomicType):  # pytorch implementation
    ...    aliases = {torch.bfloat16}
    ...    type = bfloat16  # PyTorch doesn't allow bfloat16 outside of tensors
    ...    itemsize = 2
    ...    na_value = np.nan

This allows us to distinguish between the TensorFlow and PyTorch
implementations.

.. doctest::

    >>> pdcast.resolve_type("bfloat16")
    BFloat16Type()
    >>> pdcast.resolve_type("bfloat16[tensorflow]")
    TensorFlowBFloat16Type()
    >>> pdcast.resolve_type("bfloat16[pytorch]")
    PyTorchBFloat16Type()

.. note::

    The ``[tensorflow]``\/``[pytorch]`` extensions can be applied to any string
    backend that is registered to the generic container.
    ``"bfloat16[tensorflow]"`` is thus equivalent to
    ``"brain float[tensorflow]"``, ``"bf16[tensorflow]"``, etc.

This updates our type hierarchy as follows:

.. image:: /images/bfloat16_tutorial_backends.svg

.. _tutorial.conditional:

Conditional types
-----------------
Our current ``BFloat16Type`` definitions assume that both TensorFlow and
PyTorch are always going to be present on our target system.  What if this
isn't the case?  What if we don't know ahead of time?

To handle this, we can use the optional ``cond`` argument of the
:func:`@register <register>` decorator.  If this evaluates to ``False``, then
the type definition will never be executed at all.  We can combine this with a
conditional import to only include our ``BFloat16Type``\s on systems that
support them.

.. testcleanup::

    # first we have to remove the previous definitions.  Usually this isn't
    # necessary, since types are only defined once.
    pdcast.AtomicType.registry.remove(BFloat16Type)
    pdcast.AtomicType.registry.remove(TensorFlowBFloat16Type)
    pdcast.AtomicType.registry.remove(PyTorchBFloat16Type)

.. doctest::

    # check if tensorflow is installed on the target system
    >>> try:
    ...     import tensorflow as tf
    ...     tensorflow_installed = True
    ... except ImportError:
    ...     tensorflow_installed = False

    # check if pytorch is installed on the target system
    >>> try:
    ...     import torch
    ...     pytorch_installed = True
    ... except ImportError:
    ...     pytorch_installed = False

    # generic interface
    >>> @pdcast.register(cond=tensorflow_installed)
    ... @pdcast.generic
    ... @pdcast.subtype(pdcast.FloatType)
    ... class BFloat16Type(pdcast.AtomicType):
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float"}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

    # tensorflow implementation
    >>> @pdcast.register(cond=tensorflow_installed)
    ... @BFloat16Type.register_backend("tensorflow")
    ... class TensorFlowBFloat16Type(pdcast.AtomicType):
    ...    aliases = {bfloat16}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = np.nan

    # pytorch implementation
    >>> @pdcast.register(cond=tensorflow_installed and pytorch_installed)
    ... @BFloat16Type.register_backend("pytorch")
    ... class PytorchBFloat16Type(pdcast.AtomicType):
    ...    aliases = {torch.bfloat16}
    ...    type = bfloat16  # currently, torch doesn't allow naked bf16 scalars
    ...    itemsize = 2
    ...    na_value = np.nan

Note that whenever you define a conditional type, each of its *subtypes* and
*implementation types* must inherit the same condition.  If this is not
observed, then the subsequent definitions will raise a ``TypeError`` indicating
that the parent type could not be found.

.. _tutorial.conversions:

Adding conversions
------------------
So far, we've set up a small family of bfloat16 types, including two separate
implementations for different TensorFlow and PyTorch backends.  We've also
seen how these types can be automatically integrated into schema validation
and inference operations, and be ignored if the required dependency(s) do not
exist.

In addition, we can tell ``pdcast`` how to convert data to and from our various
bfloat16 types.  This consists of defining the special
:meth:`.to_boolean() <AtomicType.to_boolean>`,
:meth:`.to_integer() <AtomicType.to_integer>`,
:meth:`.to_float() <AtomicType.to_float>`,
:meth:`.to_complex() <AtomicType.to_complex>`,
:meth:`.to_decimal() <AtomicType.to_decimal>`,
:meth:`.to_datetime() <AtomicType.to_datetime>`, and
:meth:`to_string() <AtomicType.to_string>` methods.  :class:`AtomicType`
provides some minimal support for these in its base definition, but they can be
altered or redefined as needed to give the correct behavior.

Here is an example ``.to_integer()`` method for our bfloat16 objects.

.. code:: python

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert bfloat16 objects to an equivalent integer representation."""
        print("Hello, World!")
        series, dtype = series.boundscheck(dtype, errors=errors)
        return super().to_integer(
            series,
            dtype=dtype,
            downcast=downcast,
            errors=errors
        )

There are a couple things to note about this example.

First, it accepts a :class:`SeriesWrapper` object as its first argument, and
returns a corresponding :class:`SeriesWrapper` with the appropriate
transformation applied.  This is a standard pattern for dispatched methods,
which ``to_integer()`` implicitly is.

Second, it must take variable-length keyword arguments (``**unused``), which
allow it to be used cooperatively with other ``to_integer()`` methods from
different data types.

Third, it uses an internal :meth:`SeriesWrapper.boundscheck` utility method,
which ensures that the data contained in ``series`` do not exceed the min/max
values of the target ``dtype``, and adjusts either the series data or the
``dtype`` to fit in the event of overflow.  Above this, we've added a simple
print statement to indicate that we are actually executing our type-specific
implementation method.

Fourth, the actual conversion itself is delegated to
:meth:`AtomicType.to_integer` in the final line.  This is the same method that
would be called if we omitted our custom method entirely.  Its logic is very
basic, consisting only of a naive ``astype()`` operation followed by optional
downcasting.  By adding the previous ``boundscheck`` step, we are ensuring that
this process occurs without error, and that the correct error-handling rules
are applied beforehand in the event of overflow.

Additionally, it is worth noting the types of each of the input arguments.
The standalone :func:`pdcast.to_integer` function preprocesses each of these
before passing them to our type-specific implementation, ensuring that they are
always provided in a standardized format.  As such, our type-specific
conversions should never need to implement any custom argparsing themselves,
unless they accept a keyword argument that is not found in the base
:func:`pdcast.to_integer` signature.


Before we distribute our conversion method to our ``BFloat16Type``\s, we can
try doing a naive conversion just to see what happens.

.. doctest::

    >>> pdcast.to_integer([bfloat16(1.2), bfloat16(2.8)])

Note that the conversion works, but no ``Hello, World!`` output is generated.
This is because we are effectively bypassing the first two lines of our
``to_integer()`` method.

Now, We can distribute our custom conversion method by packaging it into a
`Mixin <https://dev.to/bikramjeetsingh/write-composable-reusable-python-classes-using-mixins-6lj>`_
class.

.. doctest::

    >>> class BFloat16Mixin:
    ...     def to_integer(
    ...         self,
    ...         series: pdcast.cast.SeriesWrapper,
    ...         dtype: pdcast.AtomicType,
    ...         downcast: pdcast.CompositeType,
    ...         errors: str,
    ...         **unused
    ...     ) -> cast.SeriesWrapper:
    ...         """Convert bfloat16 objects to an equivalent integer representation."""
    ...         print("Hello, World!")
    ...         series, dtype = series.boundscheck(dtype, errors=errors)
    ...         return super().to_integer(
    ...             series,
    ...             dtype=dtype,
    ...             downcast=downcast,
    ...             errors=errors
    ...         )

Which we then distribute to our ``BFloat16Type``\s using multiple inheritance.

.. testcleanup::

    # first we have to remove the previous definitions.  Usually this isn't
    # necessary, since types are only defined once.
    pdcast.AtomicType.registry.remove(BFloat16Type)
    pdcast.AtomicType.registry.remove(TensorFlowBFloat16Type)
    pdcast.AtomicType.registry.remove(PyTorchBFloat16Type)

.. doctest::

    # check if tensorflow is installed on the target system
    >>> try:
    ...     import tensorflow as tf
    ...     tensorflow_installed = True
    ... except ImportError:
    ...     tensorflow_installed = False

    # check if pytorch is installed on the target system
    >>> try:
    ...     import torch
    ...     pytorch_installed = True
    ... except ImportError:
    ...     pytorch_installed = False

    # generic interface
    >>> @pdcast.register(cond=tensorflow_installed)
    ... @pdcast.generic
    ... @pdcast.subtype(pdcast.FloatType)
    ... class BFloat16Type(BFloat16Mixin, pdcast.AtomicType):
    ...    name = "bfloat16"
    ...    aliases = {"bfloat16", "bf16", "brain float"}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = bfloat16(np.nan)

    # tensorflow implementation
    >>> @pdcast.register(cond=tensorflow_installed)
    ... @BFloat16Type.register_backend("tensorflow")
    ... class TensorFlowBFloat16Type(BFloat16Mixin, pdcast.AtomicType):
    ...    aliases = {bfloat16}
    ...    type = bfloat16
    ...    itemsize = 2
    ...    na_value = bfloat16(np.nan)

    # pytorch implementation
    >>> @pdcast.register(cond=tensorflow_installed and pytorch_installed)
    ... @BFloat16Type.register_backend("pytorch")
    ... class PytorchBFloat16Type(BFloat16Mixin, pdcast.AtomicType):
    ...    aliases = {torch.bfloat16}
    ...    type = bfloat16  # PyTorch doesn't allow bfloat16 scalars atm
    ...    itemsize = 2
    ...    na_value = np.nan

.. note::

    ``BFloat16Mixin`` comes **before** :class:`AtomicType` to ensure correct
    `Method Resolution Order (MRO) <https://en.wikipedia.org/wiki/C3_linearization>`_.
    If we were to reverse this, then our custom ``to_integer()`` method would
    not be called at all, and we would instead default to
    :meth:`AtomicType.to_integer`.

This ensures that our new conversion method will be called any time we do a
:func:`pdcast.to_integer` or :func:`pdcast.cast` operation on objects that
contain one or more of our ``BFloat16Type`` types.  If we re-run the previous
test, we can see our ``hello world`` message being printed to the console.

.. doctest::

    >>> pdcast.to_integer([bfloat16(1.2), bfloat16(2.8)])
    Hello, World!

In reality, float types already have an extensive ``FloatMixin`` class that
defines its own ``to_integer()`` method, which is more powerful than the one
shown here, including rules for customizable rounding and tolerances.  In the
majority of cases, one can fully integrate a new type by simply inheriting from
one of these mixins, rather than writing your own.  If we wanted to apply this
to our ``BFloat16Type`` objects, we would simply replace the ``BFloat16Mixin``
class we just defined with ``pdcast.types.float.FloatMixin`` where applicable.

.. _tutorial.dispatch:

Adding custom dispatch methods
------------------------------
Conversions might not be the only thing you want to dispatch in this way.
Luckily, ``pdcast`` allows users to append arbitrary methods to ``pd.Series``
objects, provided their elements match the attached type.  This can be done
using the :func:`@dispatch <dispatch>` decorator.

Returning to our ``BFloat16Type``\s, we might want to be able to round these
numbers as we would any other floating point representation.  If we try this
in base pandas, however, we get an error indicating that our ``bfloat16``
objects do not have an appropriate ``rint`` method:

.. doctest::

    >>> pdcast.cast([1.2, 2.8], "bfloat16").round()
    Traceback:
        ...
    TypeError: loop of ufunc does not support argument 0 of type bfloat16 which has no callable rint method

We can fix this by patching in a new ``round()`` function that works for
``bfloat16`` objects.  Here's what that might look like

.. code:: python

    def round(
        self,
        series: pdcast.cast.SeriesWrapper,
        decimals: int = 0,
        rule: str = "half_even"
    ) -> cast.SeriesWrapper:
        """Round a bfloat16 series to the given number of decimal places using
        the specified rounding rule.
        """
        print("Hello, World!")
        rule = pdcast.defaults.arguments["rounding"](rule)
        return pdcast.cast.SeriesWrapper(
            pdcast.util.round.round_float(
                series.series,
                rule=rule,
                decimals=decimals
            ),
            hasnans=series.hasnans,
            element_type=series.element_type
        )

.. note::
    
    Note that we have the same :ref:`SeriesWrapper <SeriesWrapper_description>`
    inputs and outputs as we had for our example ``to_integer()`` conversion.

This function works slightly differently from the ``to_integer()`` method we
defined earlier.

First, it has no equivalent ``super().round()`` method to call, so it must
implement all of its logic from start to finish.

Second, it has to validate all of its own inputs itself, since there is no
standalone ``pdcast.round()`` function to do this automatically.

Third, it delegates to a specialized ``pdcast.util.round.round_float()``
function that does the actual rounding.  This operates only on raw
``pd.Series`` objects, not :class:`SeriesWrapper`s, and is type-agnostic, meaning
that it can bypass the ``TypeError`` we got previously.  We can confirm this
manually:

.. doctest::

    >>> pdcast.util.round.round_float(pdcast.cast([1.2, 2.8], "bfloat16"))

So, all we need to do is to make this function available under
``pd.Series.round()``.  This is where the :func:`@dispatch <dispatch>`
decorator comes in.

We can add this function to our ``BFloat16Type``\s in one of two ways.  The
first is to manually append it to our ``BFloat16Mixin``, like so:

.. code:: python

    class BFloat16Mixin:
        ...

        @pdcast.dispatch
        def round(
            self,
            series: pdcast.cast.SeriesWrapper,
            decimals: int = 0,
            rule: str = "half_even"
        ) -> cast.SeriesWrapper:
            """Round a bfloat16 series to the given number of decimal places using
            the specified rounding rule.
            """
            rule = pdcast.defaults.arguments["rounding"](rule)
            return pdcast.cast.SeriesWrapper(
                pdcast.util.round.round_float(
                    series.series,
                    rule=rule,
                    decimals=decimals
                ),
                hasnans=series.hasnans,
                element_type=series.element_type
            )

        ...

The second is to define it as a standalone function and then dynamically patch
it into the appropriate types.  This can be done using the ``types`` keyword
argument in :func:`@dispatch <dispatch>`

.. doctest::

    >>> @pdcast.dispatch(types="bfloat16, bfloat16[tensorflow], bfloat16[pytorch]")
    ... def round(
    ...     self,
    ...     series: pdcast.cast.SeriesWrapper,
    ...     decimals: int = 0,
    ...     rule: str = "half_even"
    ... ) -> cast.SeriesWrapper:
    ...     """Round a bfloat16 series to the given number of decimal places using
    ...     the specified rounding rule.
    ...     """
    ...     rule = pdcast.defaults.arguments["rounding"](rule)
    ...     return pdcast.cast.SeriesWrapper(
    ...         pdcast.util.round.round_float(
    ...             series.series,
    ...             rule=rule,
    ...             decimals=decimals
    ...         ),
    ...         hasnans=series.hasnans,
    ...         element_type=series.element_type
    ...     )

The first option is more explicit, and is preferred if you have control over
the type definition itself.  The second allows you to add new dispatch methods
to existing types that are already defined.  We've chosen the second approach
for demonstration purposes here.

Now, if we run our ``pd.Series.round()`` test from before, we'll see that we
are using our dispatched implementation, and that no ``TypeError`` is raised
as a result.

.. doctest::

    >>> pdcast.cast([1.2, 2.8], "bfloat16").round()
    Hello, World!

.. note::

    Original functionality can be recovered by appending ``.original`` to the
    call invocation, like so:

    .. doctest::

        >>> pdcast.cast([1.2, 2.8], "bfloat16").round.original()
        Traceback:
            ...
        TypeError: loop of ufunc does not support argument 0 of type bfloat16 which has no callable rint method

If we wanted to, we could also hide our new ``round()`` method behind a custom
namespace to avoid confusion with the default ``pd.Series.round()``
implementation.  This mirrors the pandas convention, where datetime-related
functionality is contained in a separate ``.dt`` namespace (or ``.str`` for
string-related operations, ``.sparse`` for sparse, etc.).

We can accomplish this by using the optional ``namespace`` keyword argument
in :func:`@dispatch <dispatch>`, like so:

.. doctest::

    >>> @pdcast.dispatch(
    ...     namespace="bfloat16",
    ...     types="bfloat16, bfloat16[tensorflow], bfloat16[pytorch]"
    ... )
    ... def round(
    ...     self,
    ...     series: pdcast.cast.SeriesWrapper,
    ...     decimals: int = 0,
    ...     rule: str = "half_even"
    ... ) -> cast.SeriesWrapper:
    ...     """Round a bfloat16 series to the given number of decimal places using
    ...     the specified rounding rule.
    ...     """
    ...     rule = pdcast.defaults.arguments["rounding"](rule)
    ...     return pdcast.cast.SeriesWrapper(
    ...        pdcast.util.round.round_float(
    ...             series.series,
    ...             rule=rule,
    ...             decimals=decimals
    ...         ),
    ...         hasnans=series.hasnans,
    ...         element_type=series.element_type
    ...     )

Our dispatched implementation can then only be accessed through the
newly-defined ``.bfloat16`` accessor.

.. doctest::

    >>> pdcast.cast([1.2, 2.8], "bfloat16").round()
    Traceback:
        ...
    TypeError: loop of ufunc does not support argument 0 of type bfloat16 which has no callable rint method
    >>> pdcast.cast([1.2, 2.8], "bfloat16").bfloat16.round()

This pattern is recommended if your dispatched method overloads an existing
``pd.Series`` method that you want to retain in its original form, or you want
to make the dispatching explicit rather than implicit.  By not cluttering the
base ``pd.Series`` namespace, methods are free to use whatever names you'd
like without risk of collision.

.. note::

    Technically, the ``round()`` implementation we created in this tutorial
    ought to be hidden behind some kind of ``.numeric`` namespace, but it turns
    out to be unnecessary here.  This is because our ``round()`` implementation
    is actually more generic than the default. All it adds is an extra ``rule``
    argument and a special case for ``dtype: object`` arrays.

.. danger::

    In general, extra care should be taken whenever dispatching to an existing
    pandas method, since the dispatched method is attached directly to the
    ``pd.Series`` class itself.  This means that it will also be called in
    pandas internals, which can alter existing behavior in unexpected ways.
    To address this, care must be taken to ensure that core functionality
    (including the argument signature) is retained.  In fact, this is why we
    placed the ``decimals`` argument *before* ``rule`` in our ``round()``
    signature, to mirror :meth:`pandas:pandas.Series.round`.

.. caution::

    Methods that are already hidden behind a ``pd.Series`` accessor (like
    ``.dt.tz_localize()``) can also be overloaded in the same way as any other
    method.  In this case, the ``.original`` extension can be used to recover
    the original implementation, and all the same risks with name collisions
    apply as above.

.. _tutorial.modifying:

Appendix: modifying existing types
----------------------------------
In the previous section, we saw how new dispatch methods can be dynamically
added to existing types.  But previously, we specified that :class:`AtomicType`
objects are strictly read-only.  What gives?

It's still the case that :class:`AtomicType`s are read-only, but only *after*
they are constructed.  The base class can still be modified at will, with any
changes being patched in to existing objects.  This can be used to reassign
type attributes like ``name``, ``type``, ``dtype``, ``na_value``, etc.

.. note::

    If you change the ``name`` or ``aliases`` fields of an :class:`AtomicType`
    definition, you'll have to manually refresh the registry to rebuild
    regular expressions and integrate the new values.  This can be done by
    calling:

    .. code::

        pdcast.AtomicType.registry.flush()

.. _tutorial.abstract:

Appendix: integrating with custom ExtensionDtypes
-------------------------------------------------
Notice how whenever we convert to one of our custom ``BFloat16Type``\s, the
result always has ``dtype: object``?  This is because we failed to assign an
appropriate ``.dtype`` field for our AtomicTypes.  This is fine in the default
case, but it effectively eliminates any advantage we would get from static
typing in the first place.  In essence, these are nothing more than standard
python lists (with a bunch of extra pandas functionality) that just happen to
contain ``bfloat16`` objects.  This is not ideal from a performance
perspective, and if you try to do math with the results of our ``BFloat16Type``
conversions, you will find that it is quite slow indeed.

We can accelerate this by integrating with Pandas' own ``ExtensionDtype``/
``ExtensionArray`` infrastructure.  Provided you can construct or
`otherwise obtain <https://github.com/GreenWaves-Technologies/bfloat16>`_ these
for yourself, you can integrate them with ``pdcast`` by assigning to
``BFloat16Type.dtype`` in its class definition:

.. code:: python

    ...
    class BFloat16Type(pdcast.AtomicType):
        ...
        dtype = # ExtensionDtype goes here
        ...

Or dynamically for existing types by overriding their ``.dtype`` attribute:

.. code:: python

    BFloat16Type.dtype = # ExtensionDtype

If everything is configured correctly then performance should be dramatically
improved, and conversion results should be automatically labeled with your new
``ExtensionType`` rather than ``dtype: object``.
