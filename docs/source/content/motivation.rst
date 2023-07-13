.. currentmodule:: pdcast

.. _motivation:

Motivation
==========
``pdcast`` is meant to be a general-purpose framework for writing type
extensions for the `pandas <https://pandas.pydata.org/>`_ ecosystem.  It offers
a wealth of tools to do this, but in order to understand how they work, we must
first examine of the overall state of the numpy/pandas typing infrastructure
and `type systems <https://en.wikipedia.org/wiki/Type_system>`_ as a whole.

.. contents::
    :local:

.. _motivation.type_systems:

Type systems
------------
In general, `type systems <https://en.wikipedia.org/wiki/Type_system>`_ are
responsible for assigning a type to every term in a program.  These types are
responsible for determining the set of values that a term can take, as well as
the operations that can be performed on them and the ways in which they can be
combined.  Broadly speaking, they vary along 2 main axes: `strong/weak
<https://en.wikipedia.org/wiki/Strong_and_weak_typing>`_ and `dynamic/static
<https://en.wikipedia.org/wiki/Type_system#Type_checking>`_.

.. _motivation.type_systems.strong:

Strong typing
^^^^^^^^^^^^^
Strongly-typed languages are those that enforce strict rules about how data can
be combined.  For example, the following Python code will raise a
:exc:`TypeError <python:TypeError>`:

.. doctest::

    >>> a = 1
    >>> b = "2"
    >>> a + b
    Traceback (most recent call last):
        ...
    TypeError: unsupported operand type(s) for +: 'int' and 'str'

This is because Python doesn't know how to add integers to strings by default,
and rather than guessing, it simply raises an error describing the ambiguity.
This level of strictness is usually a good thing, as it prevents us from
introducing subtle bugs into our code where we least expect them.  If we want
to perform this kind of operation, we need to explicitly convert one of the
values to resolve the ambiguity:

.. doctest::

    >>> a + int(b)
    3
    >>> str(a) + b
    '12'

.. _motivation.type_systems.weak:

Weak typing
^^^^^^^^^^^
Weak typing, by contrast, is a more permissive scheme that allows us wide
latitude in how we combine data.  This is most evident in languages like
JavaScript, where the following code is perfectly valid:

.. code-block:: javascript

    > var a = 1;
    > var b = "2";
    > a + b;
    "12"
    > a - b;
    -1

This often leads to unexpected behavior as the language attempts to implicitly
convert the operands to a common type.  Coercion of this form can be useful in
some cases, but it can also be a source of confusion, as it's not always clear
what the result of an operation will be.  This can make it difficult to reason
about the behavior of weakly-typed code, and can lead to some *very* subtle
bugs.  Consider the following, for example:

.. code-block:: javascript

    > [] + {};
    "[object Object]"
    > {} + [];
    0
    > {} + {};
    "[object Object][object Object]"
    > [] + [];
    ""
    > 0 == [];
    true
    > 0 == "0";
    true
    > "0" == [];
    false

.. _motivation.type_systems.dynamic:

Dynamic typing
^^^^^^^^^^^^^^
Both Python and Javascript are examples of `dynamically-typed
<https://en.wikipedia.org/wiki/Type_system#Dynamic_type_checking_and_runtime_type_information>`_
languages, which assign types to variables at **run time**.  This allows us to
change the type of a variable at any point during the program's execution,
allowing us to write code like this:

.. doctest::

    >>> a = 1
    >>> type(a)
    <class 'int'>
    >>> a = "2"
    >>> type(a)
    <class 'str'>

By assigning types in this way, we avoid the need for explicit type annotations
in our code, which can make it easier to both read and write.  However, this
flexibility comes at a cost, as it requires the language to perform additional
checks at runtime to ensure that the types of our variables are compatible with
the operations we're performing.  This incurs a small performance penalty, but
more importantly, it means that we can't catch type errors until the code is
actually executed.  This can make it difficult to debug our code, as we may not
know that a problem exists until we actually encounter it.

.. doctest::

    >>> def add():
    ...     a = 1
    ...     b = "2"
    ...     return a + b  # Python won't catch this error until runtime

    >>> add()  # we have to actually invoke the function
    Traceback (most recent call last):
      ...
    TypeError: unsupported operand type(s) for +: 'int' and 'str'

.. _motivation.type_systems.static:

Static typing
^^^^^^^^^^^^^
Whereas dynamic typing assigns types to variables at run time, `statically-typed
<https://en.wikipedia.org/wiki/Type_system#Static_type_checking>`_ languages
do so at **compile time**.  This means that types are baked into the code
itself and cannot be changed during execution.  Meanwhile, the compiler gets
full access to these types, allowing it to test the correctness of our code
before it's ever run.  The following C code, for instance, will fail to
compile:

.. code-block:: c

    int main() {
        // initializing an array of integers with a single value
        int a[10] = 1;  // C will catch this error at compile time

        return 0;
    }

Note that we don't have to actually execute the program to catch this error.
Instead, the compiler is smart enough to identify the problem for us, and will
refuse to compile the code until we fix it.  This gives us certain assurances
about the correctness of our code that we can't get from a dynamically-typed
language.  Not only can we exclude such pedantic errors from our test cases,
but we can also avoid the extra runtime checks that are required to catch them
in the first place.  This can greatly enhance our confidence in the codebase,
which becomes increasingly important as it grows in size and complexity.

.. admonition:: A note on Rust
    :class: dropdown, toggle-shown

    `Rust <https://www.rust-lang.org/>`_ takes this form of static analysis to
    the next level by implementing an invasive compiler that can detect a wide
    range of potential errors and give suggestions on how to fix them.  For
    instance, the following code can sometimes compile in C depending on the
    specific compiler and flags we use:

    .. code-block:: c

        int main() {
            int a = 1;
            char b = "2";
            int c = a + b;

            return 0;
        }

    Rust, on the other hand, will refuse to compile this code under any
    circumstances:

    .. code-block:: rust

        fn main() {
            let a: i32 = 1;
            let b: &str = "2";
            let c: i32 = a + b;  // Rust will catch this error at compile time
        }

    It even gives us a helpful error message to explain what went wrong:

    .. code-block:: text

        error[E0277]: cannot add `&str` to `i32`
         --> src/main.rs:4:20
          |
        4 |     let c: i32 = a + b;
          |                    ^ no implementation for `i32 + &str`
          |
          = help: the trait `Add<&str>` is not implemented for `i32`
          = help: the following other types implement trait `Add<Rhs>`:
                    <&'a i32 as Add<i32>>
                    <&i32 as Add<&i32>>
                    <i32 as Add<&i32>>
                    <i32 as Add>

        For more information about this error, try `rustc --explain E0277`.

.. _motivation.python_vs_c:

Python vs C
-----------
As we've seen, the choice of type system can have a significant impact on the
way we write our code.  In fact, much of what gives Python its unique flavor is
due to the dynamic nature of its type system and the
:ref:`data model <python:datamodel>` that it implements.  This allows us to
write code that's both concise and expressive while still retaining the
relative safety of a strongly-typed language.  

The flexible nature of Python's type system also has tangible benefits for the
design and capabilities of our code.  Because types can change dynamically and
are not enforced by the compiler, we can use them in ways that would be
difficult or impossible in a language like C.  `Duck typing
<https://en.wikipedia.org/wiki/Duck_typing>`_, for instance, is trivially
implemented in Python, but would require a significant amount of added
complexity in C.  This allows us to write code in a more `interface-oriented
<https://en.wikipedia.org/wiki/Interface_(object-oriented_programming)>`_
style, which is often more modular and easier to maintain.  Python also
supports high-level `metaprogramming
<https://en.wikipedia.org/wiki/Metaprogramming>`_ techniques like `monkey
patching <https://en.wikipedia.org/wiki/Monkey_patch>`_ and `reflection
<https://en.wikipedia.org/wiki/Reflective_programming>`_, which have no direct
analogue in C.  In fact, we can even emulate weak typing to a certain extent
through the use of Python's :ref:`special methods <python:numeric-types>`,
which can override the behavior of built-in operators.

All of this results in a straightforward, easy-to-use programming language that
supports a wide variety of design patterns with relatively little fuss.  There
are, however, a few important drawbacks, particularly as it relates to
performance, type safety, and interoperability.

.. _motivation.type_systems.performance:

Performance
^^^^^^^^^^^
On an implementation level, Python achieves dynamic typing by storing a
`pointer <https://en.wikipedia.org/wiki/Pointer_(computer_programming)>`_
to an object's type in its header.  This adds a small overhead for every object
that Python creates - the size of a single pointer on the target system.  We
can observe this by running :func:`sys.getsizeof() <python:sys.getsizeof>` on a
built-in Python type.

.. doctest::

    >>> import sys
    >>> sys.getsizeof(3.14)  # length in bytes
    24

On a `64-bit <https://en.wikipedia.org/wiki/64-bit_computing>`_ system, these
bytes are broken down as follows:

#.  An 8 byte `reference counter
    <https://en.wikipedia.org/wiki/Reference_counting>`_ for automatic `garbage
    collection
    <https://en.wikipedia.org/wiki/Garbage_collection_(computer_science)>`_.
#.  An 8 byte pointer to the :class:`float <python:float>` type object.
#.  8 bytes describing the value of the float as a 64-bit `double
    <https://en.wikipedia.org/wiki/Double-precision_floating-point_format>`_.

This effectively triples the size of every :class:`float <python:float>` that
Python creates and makes storing them in arrays particularly inefficient.  By
contrast, C can store the same value in only 8 bytes of memory thanks to manual
`memory management <https://en.wikipedia.org/wiki/Memory_management>`_ and
static typing.  This allows us to store 3 times as many floats in C as we can
in Python, without compromising their values or exceeding the original memory
footprint.  What's more, C exposes several smaller floating point types with
reduced precision compared to Python's doubles.  By demoting our floats to a
`32 <https://en.wikipedia.org/wiki/Single-precision_floating-point_format>`_ or
`16-bit <https://en.wikipedia.org/wiki/Half-precision_floating-point_format>`_
representation, we can increase memory savings even further, to a maximum 12x
reduction.

.. figure:: /images/motivation/Floating_Point_Data_Formats.svg
    :align: center

    Memory layouts for floating point values according to the `IEEE 754
    <https://en.wikipedia.org/wiki/IEEE_754>`_ standard.

What's more, because C stores its types statically, it does not need to perform
any type checking at runtime.  This means it can perform operations on data
directly without having to go through an external data model.  It can even
`optimize <https://en.wikipedia.org/wiki/Optimizing_compiler>`_ these
operations for us during compilation.  These factors can dramatically increase
the speed and efficiency of statically-typed (and non reference-counted)
languages over Python, especially for numeric computations.

.. figure:: /images/motivation/speed-comparison-of-various-programming-languages.jpg
    :align: center

    Credit: `Niklas Heer <https://github.com/niklas-heer/speed-comparison>`_

.. admonition:: A note on concurrency
    :class: dropdown, toggle-shown

    These disparities become even more pronounced when `parallelism
    <https://en.wikipedia.org/wiki/Parallel_computing>`_ is introduced.  This
    is due to Python's `Global Interpreter Lock
    <https://realpython.com/python-gil/>`_, which effectively means that only
    one thread can execute Python code at a time.  If we want to handle
    multiple tasks simultaneously, then we need to use
    :mod:`asyncio <python:asyncio>` or
    :mod:`multiprocessing <python:multiprocessing>` instead, both of which
    incur additional overhead.

..
    In fact, this is the primary reason numpy implements
    its own type system in the first place, effectively bypassing the
    inefficiencies of the Python data model.  Instead, numpy stores data in
    :ref:`packed arrays <numpy:arrays>`: contiguous blocks of memory whose indices
    correspond to scalars of the associated
    :ref:`array-scalar <numpy:arrays.scalars>` type.  These are essentially
    identical to their C counterparts, bringing the same performance advantages to
    Python without sacrificing its overall convenience.

    .. figure:: /images/motivation/Numpy_Packed_Arrays.png
        :align: center

        Basic schematic for numpy's packed array structure.

.. _motivation.type_systems.safety:

Safety
^^^^^^
We've already seen some of the consequences that dynamic typing can have on
error detection and `type safety <https://en.wikipedia.org/wiki/Type_safety>`_.
This, even more than performance, is perhaps the biggest drawback of Python's
type system, so much so that the community has been `steadily moving away
<https://peps.python.org/pep-0484/>`_ from it in recent years (toward a system
known as `gradual typing <https://en.wikipedia.org/wiki/Gradual_typing>`_).
Static analysis tools like `pylint <https://pypi.org/project/pylint/>`_ and
`mypy <https://mypy.readthedocs.io/en/stable/>`_ have become increasingly
popular, and can be directly integrated into many popular `IDEs
<https://en.wikipedia.org/wiki/Integrated_development_environment>`_.  These
tools can help us approach the level of safety we would expect from a
statically-typed language, but they are not perfect and can never be as
exhaustive as a true static compiler.

As of now, type hints remain optional in Python, and are not enforced by the
interpreter.  This means that we can still write code that violates our
expectations, and we will not know about it until we actually run our program.
This is a particular problem for public-facing APIs, which are free to accept
any type of input, even if they are not designed to handle it.  The only way to
detect these errors is to add explicit checks to our code via the
:func:`isinstance() <python:isinstance>` and
:func:`issubclass() <python:issubclass>` built-ins, which can be cumbersome
and inefficient.  This is generally discouraged, and can even be considered an
`anti-pattern <https://en.wikipedia.org/wiki/Anti-pattern>`_ in large projects.

Including these checks in our code means that we need to write additional tests
to ensure that they are working properly, increasing the size and complexity of
our testing framework.  Worse still, checks like these are not always
sufficient, particularly when dealing with collections of objects rather than
single values.  For example, suppose we have a function that accepts a list of
:class:`ints <python:float>` and returns their sum.  We might be tempted to
write something like this:

.. doctest::

    >>> from typing import List

    >>> def sum_ints(values: List[int]) -> int:
    ...     return sum(values)

    >>> sum_ints([1, 2, 3])
    6

This seems reasonable enough, but it is not actually type-safe.  The
:func:`sum() <python:sum>` function is designed to work with any iterable
object, and will happily accept a list of :class:`floats <python:float>` as
well.  This will cause our function to produce a non-int output, even though we
explicitly told Python that we expect all ints.  The only way to avoid this is
to add a manual check to our code:

.. doctest::

    >>> def sum_ints(values: List[int]) -> int:
    ...     if not all(isinstance(v, int) for v in values):
    ...         raise TypeError("Expected integer input")
    ... 
    ...     return sum(values)

    >>> sum_ints([1.0, 2.0, 3.0])
    Traceback (most recent call last):
      ...
    TypeError: Expected integer input

This is not only inefficient, it makes the code harder to read and more brittle
in its design.  We could not, for instance, use this function to sum the values
of a numpy array, even though numpy arrays are perfectly capable of storing
integers.

.. doctest::

    >>> import numpy as np
    >>> sum_ints(np.array([1, 2, 3]))
    Traceback (most recent call last):
      ...
    TypeError: Expected integer input

This is because the :class:`numpy.int64` type (which numpy defaults to) does
not inherit from the standard library's :class:`int <python:int>` primitive,
causing it to fail our type check.  To fix this, we would need to amend the
check to include numpy's integer types as well:

.. doctest::

    >>> def sum_ints(values: List[int]) -> int:
    ...     if not all(isinstance(v, (int, np.integer)) for v in values):
    ...         raise TypeError("Expected integer input")
    ... 
    ...     return sum(values)

    >>> sum_ints(np.array([1, 2, 3]))
    6

Our code is already starting to get messy and we haven't even considered the
other complicated relationships that inheritance can create for us.  Consider
:class:`bools <python:bool>`, which inherit from :class:`int <python:int>` and
therefore pass our type check:

.. doctest::

    >>> sum_ints([True, False])
    1

We might not want this, but there is no way to prevent it without writing a
separate check for :class:`bools <python:bool>` specifically.  While we're at
it, we should add another check to make sure that our input is actually a
:class:`list <python:list>` or 1D numpy :class:`array <numpy.ndarray>`, and
that it isn't empty.  By now, however, we can see the problem with this
approach: the more we try to make our code type-safe, the more we need to rely
on manual checks, and the more we rely on manual checks, the more obscure and
brittle our code becomes.  This is a vicious cycle that can quickly spiral out
of control, and it is one of the biggest reasons why Python is not considered a
type-safe language by default.  Python's answer to this is `duck typing
<https://en.wikipedia.org/wiki/Duck_typing>`_, but as we will see, this is not
always sufficient.

..
    Probably the most significant ramifications of dynamic typing are in error
    detection and `type safety <https://en.wikipedia.org/wiki/Type_safety>`_.
    Because C has access to full type information at `compile
    <https://en.wikipedia.org/wiki/Compiler>`_ time, it can warn users of
    mismatches before the program is ever run.  Python, on the other hand, forces
    users to rely on **runtime** type checks via the built-in
    :func:`isinstance() <python:isinstance>` and
    :func:`issubclass() <python:issubclass>` functions.  This has a number of
    negative consequences.

    First and most importantly, we are unable to catch errors until we actually run
    our program.  This means we can never have absolute confidence that our
    constructs are receiving the data they expect in all cases, and the only way we
    can make sure is by adding an explicit branch to our production code.  This is
    inefficient and cumbersome to the extent that it is often considered an
    `anti-pattern <https://en.wikipedia.org/wiki/Anti-pattern>`_ in large projects.

    Instead, we are encouraged to use static analysis tools like `mypy
    <https://mypy-lang.org/>`_, which can analyze `type hints
    <https://peps.python.org/pep-0484/>`_ that are separate from logic.  This
    solves most issues with type safety on an internal level, but public-facing
    functions still need explicit checks to handle arbitrary user input, where
    static analysis cannot reach.  This forces us back into the
    :func:`isinstance() <python:isinstance>`\ /
    :func:`issubclass() <python:issubclass>` paradigm for at least some portion of
    our code base.

    If our inputs are scalar, then this isn't the end of the world.  Where it
    becomes especially pathological is when the data we're expecting is `vectorized
    <https://en.wikipedia.org/wiki/Array_programming>`_ in some way, as might be
    the case for numpy arrays or pandas data structures.  Running
    :func:`isinstance() <python:isinstance>` on these objects will simply check the
    type of the vector itself rather than any of its contents.  If we want to
    determine the type of each element, then we have 2 options.  Either we check
    the vector's :class:`dtype <numpy.dtype>` attribute (if it has one), or iterate
    through it manually, applying :func:`isinstance() <python:isinstance>` at
    every index.  The former is fast, but restricts us to numpy/pandas types, and
    the latter is slow, but works on generic data.

    .. figure:: /images/motivation/Checking_Numpy_Packed_Arrays.svg
        :align: center

        An illustration of the two type checking algorithms.

.. _motivation.type_systems.stability:

Interoperability
^^^^^^^^^^^^^^^^
So what can we do to address these issues?  Well, we could simply ditch Python
and use a statically typed language like C from the ground up, but this would
be throwing the baby out with the bathwater.  Python is a powerful language
with unique features and a rich ecosystem of libraries and tools.  It is the
language of choice for many scientists and engineers, and consistently ranks
among the `most popular programming languages
<https://octoverse.github.com/2022/top-programming-languages>`_ in the world,
and for good reason.  Writing C is extremely error-prone and requires a level
of expertise that is its own kind of deterrent.  In the time it takes to
rewrite our code in C, we could have written a dozen new features in Python,
despite all its shortcomings.

Instead, we could try a more nuanced, `"Python as glue"
<https://numpy.org/doc/stable/user/c-info.python-as-glue.html>`_ approach,
where we write our performance-critical code in C and then call it from Python
using a `foreign function interface
<https://en.wikipedia.org/wiki/Foreign_function_interface>`_.  Python has
several tools for this, including the built-in :mod:`ctypes <python:ctypes>`
module as well as external libraries like `Cython <https://cython.org/>`_ and
`numba <https://numba.pydata.org/>`_.  In fact, numpy does this internally for
many of its core functions, which are written in C and then exposed to Python
via Cython.  This is an extremely powerful technique that can virtually
eliminate Python's relative performance issues, but like anything else, it
comes with its own set of unique challenges to consider.

Suppose we were to revisit the ``sum_ints()`` example from earlier.  This time,
however, we've decided to write the function in C and call it from Python using
one of the aforementioned interfaces.  Here's how we might do that:

First we need to write the C function that we want to invoke.

.. code-block:: c

    // sum.c
    int sum_ints_c(int *arr, int arr_length) {
        int total = 0;
        for (int i = 0; i < arr_length; i++) {
            total += arr[i];
        }
        return total;
    }

Next we expose it to Python using our choice of interface.

.. tabs::

    .. tab:: ctypes

        Before we can use the function in :mod:`ctypes <python:ctypes>`, we
        need to compile it into a shared library.  This process differs
        somewhat based on operating system, but in Linux we can do it like
        this:

        .. code-block:: bash

            $ gcc -shared -fPIC -o libsum.so sum.c

        Windows is much the same except that we need to use a ``.dll``
        extension instead of ``.so``.

        .. code-block:: bash

            $ gcc -shared -o libsum.dll sum.c

        And Mac is similar, using ``.dylib`` instead of ``.so`` or ``.dll``.

        .. code-block:: bash

            $ gcc -dynamiclib -o libsum.dylib sum.c

        Now we can load the library using :mod:`ctypes <python:ctypes>` and
        define a Python wrapper for our function:

        .. code-block:: python

            # sum_ctypes.py
            import ctypes
            from pathlib import Path
            from typing import List

            # Load the compiled C library
            lib = ctypes.CDLL(str(Path("libsum.so").absolute()))  # or "libsum.dll" on Windows

            # Define the function signature
            lib.sum_ints_c.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int]
            lib.sum_ints_c.restype = ctypes.c_int

            # Create a Python wrapper
            def sum_ints(values: List[int]) -> int:
                # Convert the Python list into a C array
                arr_length = len(values)
                arr = (ctypes.c_int * arr_length)(*values)

                # Call the C function
                return lib.sum_ints_c(arr, arr_length)

            # The Python wrapper can now be used like any other function
            sum_ints([1, 2, 3, 4, 5])  # returns 15

        This is a fairly complicated process, and it's easy to make mistakes
        along the way.

    .. tab:: Cython

        Cython simplifies things somewhat by allowing us to link directly to
        the C source code rather than building it ourselves and linking it
        manually.  In fact, Cython can do this for *any* C library (including
        the `standard library
        <https://en.wikipedia.org/wiki/C_standard_library>`_), not just the
        ones we write ourselves.  If you have an existing C library that you'd
        like to interface with, Cython makes it easy to do so.

        In our case, we just want to use the C function we wrote above.  To do
        this, we need to create a Cython implementation file
        (``sum_cython.pyx``) that imports the C source code (``sum.c``) and
        exposes the function we want to use:

        .. code-block:: python

            # sum_cython.pyx
            from cpython.array cimport array
            from typing import List

            cdef extern from "sum.c":
                int sum_ints_c(int *arr, int arr_length)

            def sum_ints(values: List[int]) -> int:
                cdef int[:] arr = array("i", values)  # typecode "i" is equivalent to ctypes.c_int
                return sum_ints_c(&arr[0], arr.shape[0])

            sum_ints([1, 2, 3, 4, 5])  # returns 15

        We can then use Cython to compile the implementation file directly, skipping
        the intermediate shared library and :mod:`ctypes <python:ctypes>` wrapper:

        .. code-block:: bash

            $ cythonize -i sum_cython.pyx

        This allows us to import the cythonized module and use it like any other Python
        code:

        .. code-block:: python

            from sum_cython import sum_ints

            sum_ints([1, 2, 3, 4, 5])  # returns 15

        .. note::

            Cython also supports an alternate syntax for defining C functions
            that is similar to Python's built-in language style.  This is an
            equivalent implementation of the ``sum_ints()`` function using this
            syntax:

            .. code-block:: python

                cpdef int sum_ints(list values):
                    cdef int[:] arr = array("i", values)
                    cdef int arr_length = arr.shape[0]
                    cdef int total = 0
                    cdef int i

                    for i in range(arr_length):
                        total += arr[i]
                    return total

            Both of them perform the same in practice, but the second one
            avoids the need to write separate C code.

    .. tab:: numba

        Numba makes this process even simpler by allowing us to write the C
        function directly in Python, and then compiling it automatically
        `whenever it is requested
        <https://en.wikipedia.org/wiki/Just-in-time_compilation>`_.  This way,
        we don't even need the wrapper function or any complicated interfaces:

        .. code-block:: python

            # sum_numba.py
            from numba import njit
            from numba.typed import List
            from numba.types import intc

            @njit(locals={"total": intc, "i": intc})
            def sum_ints(values: List[int]) -> int:
                total = 0
                for i in range(len(values)):
                    total += values[i]
                return total

            sum_ints(List([1, 2, 3, 4, 5]))  # returns 15

        .. note::

            Note the use of a :class:`numba.typed.List` instead of a regular
            Python list.  This is because Numba needs to know the exact type of
            the list elements in order to correctly compile the function.  If
            we used a regular Python list instead, Numba would fall back to the
            Python interpreter and the function would not be compiled.

            Normally, this isn't a problem because Numba is most often used in
            conjunction with numpy :class:`arrays <numpy.ndarray>`, which
            provide this information automatically.

    .. tab:: benchmarks

        If we benchmark these three implementations, we can compare their
        relative performance:

        .. code-block:: python

            # benchmark.py
            from timeit import timeit
            import numba
            import sum_ctypes
            import sum_cython
            import sum_numba

            x = list(range(10**5))
            y = numba.typed.List(x)

            results = {
                "ctypes": timeit(lambda: sum_ctypes.sum_ints(x), number=1000),   # 4.011803085999418
                "cython": timeit(lambda: sum_cython.sum_ints(x), number=1000),   # 0.8762848040023528
                "numba": timeit(lambda: sum_numba.sum_ints(y), number=1000),     # 0.7847791439999128
            }

        As we can see, the Cython and Numba implementations are both
        significantly faster than the :mod:`ctypes <python:ctypes>` version.
        This is because :mod:`ctypes <python:ctypes>` creates an *interactive*
        wrapper around the C library, which limits the amount of optimization
        it can perform.  The C function itself is correctly compiled into
        native machine code, but the wrapper is not, nor are any of the
        internals it needs to translate data between Python and C.  Instead,
        the wrapper is interpreted at runtime, which incurs the full cost of
        the Python interpreter at every step of the process.  Cython and Numba,
        on the other hand, can compile the wrapper together with the function
        and optimize both as a single unit.

        .. warning::

            Now that we've seen how to use Cython and Numba to speed up our
            code, let's compare them with the built-in Python solution that we
            started with:

            .. code-block:: python

                x = list(range(10**5))
                timeit.timeit(lambda: sum(x), number=1000)  # 0.16240401699906215

            It turns out our original answer was actually faster than any of
            the "optimized" versions we've written so far!  This is a
            cautionary tale for this kind of optimization.  Built-in solutions
            are already highly optimized for their specific use-case and are
            almost certainly going to be faster than any naive implementation
            we're likely to write.  They should always be preferred where
            possible, and custom solutions should only be used if there's a
            compelling reason to do so.  As always, we should benchmark our
            code before and after optimization to ensure that we're actually
            improving performance.

There's still one additional complication that we haven't discussed yet.  If we
take a look at the values that are returned by each of our implementations, we
might notice a discrepancy:

.. code-block:: python

    import numba

    x = list(range(10**5))
    y = numba.typed.List(x)

    sum_ctypes.sum_ints(x)   # 704982704
    sum_cython.sum_ints(x)   # 704982704 
    sum_numba.sum_ints(y)    # 704982704
    sum(x)                   # 4999950000

All of our custom implementations overflow!  This is because the C integer type
we used to represent the sum is only 32 bits wide, meaning it can only
represent values up to ``2**32 - 1``, which is less than the sum we are trying
to compute.  The built-in Python function, on the other hand, uses a
variable-length integer type that can represent arbitrarily large values
without overflowing.

.. figure:: /images/motivation/Integer_Data_Formats.svg
    :align: center

    Memory layouts for numpy/C vs. Python integers.

This is a common problem when working with C libraries.


.. TODO: end this section by mentioning numpy


Most Python objects have no direct C equivalent, and must be translated into a
compatible type before they can be passed to a C function.  This is true even
for simple data types like integers and booleans, which are represented
differently in C than they are in Python.  C integers, for example, come in
several `varieties <https://en.wikipedia.org/wiki/C_data_types>`_ based on the
size of their underlying memory buffer and signed/unsigned status.  This means
they can only represent values within a certain fixed range, and are subject
to `overflow <https://en.wikipedia.org/wiki/Integer_overflow>`_ errors if they
exceed it.  By contrast, Python (since `3.0
<https://peps.python.org/pep-0237>`_) uses a `variable-length integer
<https://en.wikipedia.org/wiki/Arbitrary-precision_arithmetic>`_ type that can
represent arbitrarily large values without overflow.  This means that any
integer we pass from Python to C must be translated into a C integer of the
appropriate size, and vice versa.  This brings us back to the type-safety
problems we were trying to avoid in the first place.







.. 
    Everything we've seen thus far encourages us to couple our code to the numpy
    type system for vectorized operations.  The trouble with this is that we
    inevitably have to translate values from Python to numpy and vice versa, which
    is non-trivial in many cases.  This is true even for simple data types, like
    integers and booleans.

    In C, integers come in several `varieties
    <https://en.wikipedia.org/wiki/C_data_types>`_ based on the size of their
    underlying memory buffer and signed/unsigned status.  This means they can
    only represent values within a certain fixed range, and are subject to
    `overflow <https://en.wikipedia.org/wiki/Integer_overflow>`_ errors if they
    exceed it.  By contrast, Python (since `3.0
    <https://peps.python.org/pep-0237/>`_) exposes only a single
    :class:`int <python:int>` type with unlimited precision.  Whenever one of these
    integers overflows, Python simply adds another 32-bit buffer to store the
    larger value.  In this way, Python is limited only by the amount of available
    memory, and can work with integers that far exceed the C limitations.

    .. figure:: /images/motivation/Integer_Data_Formats.svg
        :align: center

        Memory layouts for numpy/C vs. Python integers.

    This presents a problem for numpy, which has to coerce these integers into a
    C-compatible format for use in its arrays.  As long as they fall within the
    `64-bit <https://en.wikipedia.org/wiki/64-bit_computing>`_ limit, this can be
    done without issue:

    .. doctest::

        >>> import numpy as np

        >>> np.array([1, 2, 3])
        array([1, 2, 3])
        >>> _.dtype
        dtype('int64')

    However, as soon as we exceed this limit, we get inconsistent behavior.  For
    values > int64 but < uint64, this results in an implicit conversion to a
    floating point data type.

    .. doctest::

        >>> np.array([1, 2, 2**63])
        array([1.00000000e+00, 2.00000000e+00, 9.22337204e+18])
        >>> _.dtype
        dtype('float64')

    For even larger values, we get a ``dtype: object`` array, which is essentially
    just a Python :class:`list <python:list>` with extra operations.

    .. doctest::

        >>> np.array([1, 2, 2**64])
        array([1, 2, 18446744073709551616], dtype=object)
        >>> _.dtype
        dtype('O')

    This raises the specter of weak typing that we fought so hard to eliminate.
    Such implicit conversions are often unexpected, and can easily result in hidden
    bugs if not properly accounted for.  Worse still, numpy doesn't even warn us
    when this occurs, which makes diagnosing the problem that much more difficult.
    These distinctions become especially problematic when we start doing math on
    our arrays.

    .. doctest::

        >>> np.array([1, 2, 2**63 - 1]) + 1   # doctest: +NORMALIZE_WHITESPACE
        array([1, 2, -9223372036854775808])
        >>> np.array([1, 2, 2**63]) + 1
        array([2.00000000e+00, 3.00000000e+00, 9.22337204e+18])
        >>> np.array([1, 2, 2**64]) + 1
        array([2, 3, 18446744073709551617], dtype=object)

    .. warning::

        The first case is an example of `integer overflow
        <https://en.wikipedia.org/wiki/Integer_overflow>`_. Some languages (like
        `Rust <https://en.wikipedia.org/wiki/Rust_(programming_language)>`_) will
        raise an exception in this circumstance, but as we can see, numpy does not.

    This gives us 3 different results depending on the input data, further
    compounding the implicit conversion problem.  This means that our answers for
    even simple arithmetic problems depend (in a non-trivial manner) on
    our data.  We can't really be sure which of these we're going to get in
    practice, or what the intended representation was before we constructed the
    array.  We just have to make a note of it in our documentation counseling users
    not to try something like this and move on.

    This works for us in the short term and our analysis is progressing smoothly.
    But what if we start adding `missing values <https://en.wikipedia.org/wiki/NaN>`_
    to our data set?

    .. doctest::

        >>> np.array([1, 2, np.nan])  # returns floats
        array([ 1.,  2., nan])
        >>> np.array([1, 2, None])
        array([1, 2, None], dtype=object)

    .. warning::

        This occurs because C integers are `unable
        <https://en.wikipedia.org/wiki/NaN#Integer_NaN>`_ to hold missing values
        due to their fixed memory layout.  There is no specific bit pattern that
        can be reserved for these kinds of `special values
        <https://en.wikipedia.org/wiki/IEEE_754#Special_values>`_, since every bit
        is significant.  This is not the case for floating points, which
        `restrict a particular exponent
        <https://en.wikipedia.org/wiki/Double-precision_floating-point_format#Exponent_encoding>`_
        specifically for such purposes, or for Python integers, which are nullable
        by default.

    We now have an entirely new set of implicit conversions to deal with.  Now we
    can't even tell whether our operations are failing due to overflow or the
    presence of some illegal value, which can occur anywhere in the array.
    Needless to say, this is unsustainable, so we decide to move to pandas hoping
    for a better solution.  Unfortunately, since pandas shares numpy's type system,
    all the same problems are reflected there as well.

    .. doctest::

        >>> import pandas as pd

        >>> pd.Series([1, 2, 2**63 - 1])
        0                      1
        1                      2
        2    9223372036854775807
        dtype: int64
        >>> pd.Series([1, 2, 2**63])  # numpy gave us a float64 array
        0                      1
        1                      2
        2    9223372036854775808
        dtype: uint64
        >>> pd.Series([1, 2, 2**64])
        0                       1
        1                       2
        2    18446744073709551616
        dtype: object

        >>> pd.Series([1, 2, np.nan])
        0    1.0
        1    2.0
        2    NaN
        dtype: float64
        >>> pd.Series([1, 2, None])  # why is this not dtype: object?
        0    1.0
        1    2.0
        2    NaN
        dtype: float64

    At this point, we might not even be sure if *we* are real, let alone our data.

    .. note::

        Pandas does expose its own :ref:`nullable integer types <pandas:integer_na>`
        to bypass the missing value restriction, but they must be set manually
        ahead of time, and are easily overlooked.

        .. doctest::

            >>> pd.Series([1, 2, np.nan], dtype=pd.Int64Dtype())
            0       1
            1       2
            2    <NA>
            dtype: Int64
            >>> pd.Series([1, 2, None], dtype=pd.Int64Dtype())
            0       1
            1       2
            2    <NA>
            dtype: Int64



.. 
    Limitations of Numpy/Pandas
    ---------------------------
    Consider a pandas series containing the integers 1 through 3:

    .. doctest:: limitations

        >>> import pandas as pd
        >>> pd.Series([1, 2, 3])
        0    1
        1    2
        2    3
        dtype: int64

    By default, this is automatically converted to a 64-bit integer data type, as
    represented by its ``dtype`` attribute.  If we request a value at a specific
    index of the series, it will be returned as a ``numpy.int64`` object:

    .. doctest:: limitations

        >>> val = pd.Series([1, 2, 3])[0]
        >>> print(type(val), val)
        <class 'numpy.int64'> 1

    So far, so good.  But what if we add a missing value to the series?

    .. doctest:: limitations

        >>> pd.Series([1, 2, 3, None])
        0    1.0
        1    2.0
        2    3.0
        3    NaN
        dtype: float64

    It changes to ``float64``!  This happens because ``numpy.int64`` objects
    cannot contain missing values.  There is no particular bit pattern in their
    binary representation that can be reserved to hold `special values <https://en.wikipedia.org/wiki/IEEE_754#Special_values>`_
    like ``inf`` or ``NaN``.  This is not the case for floating points, which
    `restrict a particular exponent <https://en.wikipedia.org/wiki/Double-precision_floating-point_format#Exponent_encoding>`_
    specifically for such purposes.  Because of this discrepancy, pandas silently
    converts our integer series into a float series to accomodate the missing
    value.

    Pandas does expose an ``Int64Dtype()`` that bypasses this restriction, but it
    must be set manually:

    .. doctest:: limitations

        >>> pd.Series([1, 2, 3, None], dtype=pd.Int64Dtype())
        0       1
        1       2
        2       3
        3    <NA>
        dtype: Int64

    This means that unless you are aware of it ahead of time, your data could very
    well be converted to floats *without your knowledge!* Why is this a problem?
    Well, let's see what happens when our integers are very large:

    .. doctest:: limitations

        >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1])
        0    9223372036854775805
        1    9223372036854775806
        2    9223372036854775807
        dtype: int64

    These integers are very large indeed.  In fact, they are almost overflowing
    their 64-bit buffers.  If we add 1 to this series, we might expect to
    receive some kind of overflow error informing us of our potential mistake.  Do
    we get such an error?

    .. doctest:: limitations

        >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1]) + 1
        0    9223372036854775806
        1    9223372036854775807
        2   -9223372036854775808
        dtype: int64

    No, the data type stays 64-bits wide and we simply wrap around to the
    negative side of the number line.  Again, if you aren't aware of this behavior,
    you might have just introduced an outlier to your data set unexpectedly.

    It gets even worse when you combine large integers with missing values:

    .. doctest:: limitations

        >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
        0    9.223372e+18
        1    9.223372e+18
        2    9.223372e+18
        3             NaN
        dtype: float64

    As before, this converts our data into a floating point format.  What happens
    if we add 1 to this series?

    .. _floating_point_rounding_error:

    .. doctest:: limitations

        >>> pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None]) + 1
        0    9.223372e+18
        1    9.223372e+18
        2    9.223372e+18
        3             NaN
        dtype: float64

    This time we don't wrap around the number line like before.  This is because in
    `floating point arithmetic <https://en.wikipedia.org/wiki/Floating-point_arithmetic>`_,
    we have plenty of extra numbers to work with above the normal 64-bit limit.
    However, if we look at the values at each index, what integers are we actually
    storing?

    .. doctest:: limitations

        >>> series = pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
        >>> for val in series[:3]:
        ...     print(int(val))
        9223372036854775808
        9223372036854775808
        9223372036854775808

    They're all the same!  This is an example of a
    `floating point rounding error <https://en.wikipedia.org/wiki/Round-off_error>`_
    in action.  Each of our integers is above the integral range of ``float64``
    objects, which is defined by the number of bits in their significand 
    (`53 <https://en.wikipedia.org/wiki/Double-precision_floating-point_format#IEEE_754_double-precision_binary_floating-point_format:_binary64>`_
    in the case of ``float64`` objects).  Only integers within this range can be
    exactly represented with exponent 1, meaning that any integer outside the range
    ``(-2**53, 2**53)`` must increment the exponent and therefore lose exact
    integer precision.  In this case it's even worse, since our values are ~10
    factors of 2 outside that range, meaning that the exponent portion of our
    floats must be >= 10.  This leaves approximately ``2**10 = 1024`` unique values
    that we are masking with the above data.  We can confirm this by doing the
    following:

    .. doctest:: limitations

        >>> import numpy as np
        >>> val = np.float64(2**63 - 1)
        >>> i, j = 0, 0
        >>> while val + i == val:  # count up
        ...     i += 1
        >>> while val - j == val:  # count down
        ...     j += 1
        >>> print(f"up: {i}\ndown: {j}\ntotal: {i + j}")
        up: 1025
        down: 513
        total: 1538

    So it turns out we have over 1500 different values within error of the observed
    result.  Once more, if we weren't aware of this going in to our analysis, we
    may have just unwittingly introduced a form of systematic error by accident.
    This is not ideal!

    .. note::

        The discrepancy from our predicted value of 1024 comes from the fact
        that ``2**63 - 1`` is on the verge of overflowing past its current
        exponent.  Once we reach ``2**63``, we must increment our exponent to 11,
        giving us twice as many values above ``2**63`` as below it.

.. _motivation.pdcast:

pdcast: a safer alternative
-------------------------------
Let's see how ``pdcast`` handles the above examples:

.. doctest::

    >>> import pdcast

    >>> pdcast.to_integer([1, 2, 3])
    0    1
    1    2
    2    3
    dtype: int64
    >>> _.dtype
    dtype('int64')

So far this is exactly the same as before.  However, when we add missing
values, we see how ``pdcast`` diverges from normal pandas:

.. doctest::

    >>> pdcast.to_integer([1, 2, 3, None])
    0       1
    1       2
    2       3
    3    <NA>
    dtype: Int64

Instead of coercing integers to floats, we skip straight to the
:class:`pd.Int64Dtype() <pandas.Int64Dtype>` implementation.  This doesn't
just happen for int64s either, it also applies for booleans and all other
non-nullable data types.

.. doctest:: pdcast_intro

    >>> pdcast.to_boolean([True, False, None])
    0     True
    1    False
    2     <NA>
    dtype: boolean
    >>> pdcast.to_integer([1, 2, 3, None], "uint32")
    0       1
    1       2
    2       3
    3    <NA>
    dtype: UInt32

By avoiding a floating point intermediary, we can ensure that no data is lost
during these conversions, even if the values are very large:

.. doctest:: pdcast_intro

    >>> pdcast.to_integer([2**63 - 3, 2**63 - 2, 2**63 - 1, None])
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    3                   <NA>
    dtype: Int64

Conversions
-----------
The problems discussed above are multiplied tenfold when converting from one
representation to another.  This is where ``pdcast`` really shines.

Case study: integers & floats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Before we dive into the differences, let's see how pandas handles conversions
in cases of precision loss and/or overflow.  We'll start with our large
integers from before:

.. testsetup:: conversions

    import numpy as np
    import pandas as pd
    import pdcast

.. doctest:: conversions

    >>> series = pd.Series([2**63 - 3, 2**63 - 2, 2**63 - 1])
    >>> series
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    dtype: int64
    >>> series.astype(float)
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    dtype: float64

As we can see, pandas doesn't even emit a warning about the precision loss we
demonstrated previously.  If we reverse this conversion, we can see why that
might be a problem:

.. doctest:: conversions

    >>> series.astype(float).astype(int)
    0   -9223372036854775808
    1   -9223372036854775808
    2   -9223372036854775808
    dtype: int64

Note that we don't get our original data back.  In fact we don't even end
up on the same side of the number line, thanks to silent overflow.  Simply by
converting our data, we have implicitly changed its value.  In contrast,
``pdcast`` requires explicit approval to change data in this way.

.. doctest:: conversions

    >>> import pdcast; pdcast.attach()
    >>> series.cast("float")
    Traceback (most recent call last):
        ...
    ValueError: precision loss exceeds tolerance 1e-06 at index [0, 1, 2]
    >>> series.cast("float", errors="coerce")
    0    9.223372e+18
    1    9.223372e+18
    2    9.223372e+18
    dtype: float64

And we can reverse our conversion without overflowing:

.. doctest:: conversions

    >>> series.cast("float", errors="coerce").cast("int")
    0    9223372036854775808
    1    9223372036854775808
    2    9223372036854775808
    dtype: uint64

Which preserves the actual value of the coerced floats.

What if we wanted to convert our series to ``int32`` rather than ``float``? 
Obviously the values won't fit, but what does pandas do in this situation?

.. doctest:: conversions

    >>> series
    0    9223372036854775805
    1    9223372036854775806
    2    9223372036854775807
    dtype: int64
    >>> series.astype(np.int32)
    0   -3
    1   -2
    2   -1
    dtype: int32

At this point, you might be tearing out your hair in frustration.  Not only
does pandas *not emit a warning* in this situation, but it also gives results
that are almost unintelligible and very likely not what we were expecting.

.. note::

    The actual values we observe here are due to the same overflow wrapping
    behavior as above, except that we're doing it with a smaller container
    (2**32 possible values vs 2**64).  This means that our nearly-overflowing
    64-bit values wrap around the number line not just once, but *32 times* to
    arrive at their final result.

In contrast, ``pdcast`` is aware of this and raises an ``OverflowError`` as
you might expect.

.. doctest:: conversions

    >>> series.cast(np.int32)
    Traceback (most recent call last):
        ...
    OverflowError: values exceed int32[numpy] range at index [0, 1, 2]

If we try to coerce the previous operation, then the overflowing values will be
replaced with NAs to avoid biasing the result:

.. doctest:: conversions

    >>> series.cast(np.int32, errors="coerce")
    0    <NA>
    1    <NA>
    2    <NA>
    dtype: Int32

If any of our values *had* fit into the available range for ``int32``, they
would have been preserved.

.. doctest:: conversions

    >>> pd.Series([1, 2, 3, 2**63 - 1]).cast(np.int32, errors="coerce")
    0       1
    1       2
    2       3
    3    <NA>
    dtype: Int32

Note that a nullable dtype is returned even though the original input had no
missing values.  ``pdcast`` knows when a value is being coerced and can adjust
accordingly.

Case study: datetimes
^^^^^^^^^^^^^^^^^^^^^
Now let's look at a different case: converting to and from **datetimes**.  You
could slowly go insane doing this in pandas:

.. figure:: /images/motivation/pandas_time_conversions_naive.png
    :align: center

    (And this doesn't even consider timezones)

Or you could let ``pdcast`` work out all the messy details for you:

.. doctest:: conversions

    >>> integers = pd.Series([1, 2, 3])
    >>> integers.cast("datetime", unit="s")
    0   1970-01-01 00:00:01
    1   1970-01-01 00:00:02
    2   1970-01-01 00:00:03
    dtype: datetime64[ns]

With expanded support for different epochs and timezones:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="ns", since="j2000", tz="US/Pacific")
    0   2000-01-01 04:00:00.000000001-08:00
    1   2000-01-01 04:00:00.000000002-08:00
    2   2000-01-01 04:00:00.000000003-08:00
    dtype: datetime64[ns, US/Pacific]
    >>> integers.cast("datetime", unit="h", since="03/27/22", tz="utc")
    0   2022-03-27 01:00:00+00:00
    1   2022-03-27 02:00:00+00:00
    2   2022-03-27 03:00:00+00:00
    dtype: datetime64[ns, UTC]

And calendar-accurate unit conversions:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="Y")  # 1972 was a leap year
    0   1971-01-01
    1   1972-01-01
    2   1973-01-01
    dtype: datetime64[ns]
    >>> integers.cast("datetime", unit="M", since="utc")
    0   1970-02-01
    1   1970-03-01
    2   1970-04-01
    dtype: datetime64[ns]
    >>> integers.cast("datetime", unit="M", since=pd.Timestamp("1972-01-01"))
    0   1972-02-01
    1   1972-03-01
    2   1972-04-01
    dtype: datetime64[ns]

Without data loss:

.. doctest:: conversions

    >>> integers.cast("datetime", unit="h").cast("int", unit="h")
    0    1
    1    2
    2    3
    dtype: int64

To and from any representation:

.. doctest:: conversions

    >>> pd.Series([1.3, -4.8])
    0    1.3
    1   -4.8
    dtype: float64
    >>> _.cast("datetime", unit="D")
    0   1970-01-02 07:12:00.000000000
    1   1969-12-27 04:48:00.000000001
    dtype: datetime64[ns]
    >>> _.cast("float", unit="D")
    0    1.3
    1   -4.8
    dtype: float64

With arbitrary string parsing:

.. doctest:: conversions

    >>> pd.Series(["2022-01-12", "2022-01-30 07:30", "2022-03-27 12:00:00-0800"]).cast("datetime")
    0   2022-01-12 00:00:00
    1   2022-01-30 07:30:00
    2   2022-03-27 20:00:00
    dtype: datetime64[ns]
    >>> pd.Series(["Jan 12 2022", "January 30th, 2022 at 7:30", "27 mar 22"]).cast("datetime")
    0   2022-01-12 00:00:00
    1   2022-01-30 07:30:00
    2   2022-03-27 00:00:00
    dtype: datetime64[ns]

And support for several different datetime representations:

.. doctest:: conversions

    >>> integers.cast("datetime[pandas]", unit="s", since="jan 30 2022 at 7 AM")
    0   2022-01-30 07:00:01
    1   2022-01-30 07:00:02
    2   2022-01-30 07:00:03
    dtype: datetime64[ns]
    >>> integers.cast("datetime[python]", unit="D", since="cocoa", tz="Asia/Hong_Kong")
    0    2001-01-02 08:00:00+08:00
    1    2001-01-03 08:00:00+08:00
    2    2001-01-04 08:00:00+08:00
    dtype: datetime[python, Asia/Hong_Kong]
    >>> integers.cast("datetime[numpy]", unit="Y", since="-4713-11-24 12:00:00")
    0    -4712-11-24T12:00:00.000000
    1    -4711-11-24T12:00:00.000000
    2    -4710-11-24T12:00:00.000000
    dtype: object

.. note::

    ``pdcast`` doesn't just handle homogenous data - it can even process
    inputs that are of mixed type using a split-apply-combine strategy.
    Elements are grouped by their inferred type, converted independently, and
    then stitched together along with missing values to achieve a final
    result.

    .. doctest:: conversions

        >>> from decimal import Decimal
        >>> mixed_data = [2**63, "1979", True, 4+0j, Decimal(18), None]
        >>> pdcast.to_integer(mixed_data)
        0    9223372036854775808
        1                   1979
        2                      1
        3                      4
        4                     18
        5                   <NA>
        dtype: UInt64
        >>> pdcast.to_datetime(mixed_data)
        0       2262-04-11 23:47:16.854775
        1              1979-01-01 00:00:00
        2    1970-01-01 00:00:00.000000001
        3    1970-01-01 00:00:00.000000004
        4    1970-01-01 00:00:00.000000018
        5                             <NA>
        dtype: object

.. TODO: the datetime example from above does not result in a shared data type.
    The first result is a datetime.datetime while all others are pd.Timestamp.

.. testcleanup:: conversions

    # detach pdcast for next section
    pdcast.detach()

Type Checks
-----------
Another area where pandas could be improved is in runtime type checking.
Baseline, it includes a number of `utility functions <https://pandas.pydata.org/pandas-docs/stable/reference/arrays.html#utilities>`_
under :mod:`pandas.api.types` that are meant to do this, but each of them essentially
boils down to a naive ``.dtype`` check.  This leads to questionable (and even
inaccurate) results, such as:

.. testsetup:: validation

    import numpy as np
    import pandas as pd
    import pdcast

.. doctest:: validation

    >>> series = pd.Series([1, 2, 3], dtype="O")
    >>> pd.api.types.is_string_dtype(series)
    True

This happens because pandas stores strings as generic python objects by
default.  We can observe this by creating a basic string series.

.. doctest:: validation

    >>> pd.Series(["foo", "bar", "baz"])
    0    foo
    1    bar
    2    baz
    dtype: object

Note that the series is returned with ``dtype: object``.  This ambiguity means
that ``pd.api.types.is_string_dtype()`` (which implies specificity to strings)
has to include ``dtype: object`` in its comparisons.  Because of this, **any
series with** ``dtype: object`` **will be counted as a string series**, even
if it *does not* contain strings.  This is confusing to say the least, and
makes it practically impossible to distinguish between genuine object arrays
and those containing only strings.  Pandas does have a specialized
``pd.StringDtype()`` just to represent strings, but - like with
``pd.Int64Dtype()`` - it must be set manually, and is often overlooked in
practice.  With this dtype enabled, we can unambiguously check for strings by
doing:

.. doctest:: validation

    >>> series1 = pd.Series(["foo", "bar", "baz"], dtype=pd.StringDtype())
    >>> series2 = pd.Series([1, 2, 3], dtype="O")
    >>> pd.api.types.is_string_dtype(series1) and not pd.api.types.is_object_dtype(series1)
    True
    >>> pd.api.types.is_string_dtype(series2) and not pd.api.types.is_object_dtype(series2)
    False

But this is long and cumbersome, not to mention requiring an extra
preprocessing step to work at all.  ``pdcast`` has a better solution:

.. doctest:: validation

    >>> import pdcast; pdcast.attach()
    >>> series1.typecheck("string")
    True
    >>> series2.typecheck("string")
    False

And it even works on ``dtype: object`` series:

.. doctest:: validation

    >>> series = pd.Series(["foo", "bar", "baz"])
    >>> series
    0    foo
    1    bar
    2    baz
    dtype: object
    >>> series.typecheck("string")
    True

This is accomplished by a combination of :ref:`inference <detect_type>` and
:doc:`validation <../../generated/pdcast.ScalarType.contains>`, which can
be customized on a per-type basis.  Since these functions do not rely on a
potentially inaccurate ``.dtype`` field, we can apply this to arbitrary data:

.. doctest:: validation

    >>> from decimal import Decimal
    >>> class CustomObj:
    ...     def __init__(self, x):  self.x = x

    >>> pd.Series([1, 2, 3], dtype="O").typecheck("int")
    True
    >>> pd.Series([Decimal(1), Decimal(2), Decimal(3)]).typecheck("decimal")
    True
    >>> pd.Series([CustomObj("python"), CustomObj("is"), CustomObj("awesome")]).typecheck(CustomObj)
    True

And even to non-homogenous data:

.. doctest:: validation

    >>> series = pd.Series([1, Decimal(2), CustomObj("awesome")])
    >>> series.typecheck("int, decimal, object[CustomObj]")
    True

.. note::

    :func:`typecheck` mimics the built-in ``isinstance()`` function.  If
    multiple types are provided to compare against, it will return ``True`` if
    and only if the inferred types form a **subset** of the comparison type(s).

.. testcleanup:: validation

    pdcast.detach()

Expanded Support
----------------
``pdcast`` also exposes several new types for use in pandas data structures.

Case Study: decimals
^^^^^^^^^^^^^^^^^^^^
Earlier, we saw how :ref:`floating point rounding errors <floating_point_rounding_error>`
can influence common data analysis tasks.  One way to avoid these is to use
Python's built-in `decimal <https://docs.python.org/3/library/decimal.html>`_
library, which provides data types for arbitrary precision arithmetic.  Pandas
does not expose these by default, but ``pdcast`` makes it easy to integrate
them into our typing ecosystem.

.. testsetup:: decimal_support

    import numpy as np
    import pandas as pd

.. doctest:: decimal_support

    >>> import decimal
    >>> import pdcast
    >>> pdcast.to_decimal([1, 2, 3])
    0    1
    1    2
    2    3
    dtype: decimal

Note that we get a ``dtype: decimal`` series in return.  This is because
``pdcast`` has *automatically generated* an appropriate ``ExtensionDtype`` for
this data.  In practice, this is simply a ``dtype: object`` array with some
extra functionality added on top for comparisons, coercion, and arithmetic.

.. doctest:: decimal_support

    >>> series = pdcast.to_decimal([1, 2, 3])
    >>> series.dtype
    ImplementationDtype(decimal)
    >>> series.array
    <ImplementationArray>
    [Decimal('1'), Decimal('2'), Decimal('3')]
    Length: 3, dtype: decimal
    >>> series.array.data
    array([Decimal('1'), Decimal('2'), Decimal('3')], dtype=object)

Most importantly, this label provides an explicit hook for :func:`detect_type`
to interpret.  This turns type inference into an *O(1)* operation.

.. doctest:: decimal_support

    >>> import timeit
    >>> series = pdcast.to_decimal(np.arange(10**6))
    >>> series
    0              0
    1              1
    2              2
    3              3
    4              4
            ...
    999995    999995
    999996    999996
    999997    999997
    999998    999998
    999999    999999
    Length: 1000000, dtype: decimal
    >>> timeit.timeit(lambda: pdcast.detect_type(series), number=10**3)   # doctest: +SKIP
    0.0024710440047783777

If we stored the same values in a ``dtype: object`` series, we would be forced
to iterate over the entire array to find its type.

.. doctest:: decimal_support

    >>> series = series.astype(object)
    >>> series
    0              0
    1              1
    2              2
    3              3
    4              4
            ...
    999995    999995
    999996    999996
    999997    999997
    999998    999998
    999999    999999
    Length: 1000000, dtype: object
    >>> timeit.timeit(lambda: pdcast.detect_type(series), number=1)   # doctest: +SKIP
    0.22230663200025447

This is still fast, but it's nowhere near the constant-time equivalent.  Such
speed allows us to do near-instantaneous type checks on properly-formatted
data, and as a result, we are at liberty to sprinkle these checks throughout
our code without worrying about incurring significant overhead.

.. _decimal_round_demo:

Now, if we want to use our ``decimal`` objects for some practical math, we
might notice that some things are broken.  While the normal operators work as
expected, some of the more specialized methods (like ``round()``) may not work
properly.

.. doctest:: decimal_support

    >>> series = pdcast.to_decimal([-1.8, 0.5, 1.5, 2.4])
    >>> series.round()
    Traceback (most recent call last):
        ...
    TypeError: loop of ufunc does not support argument 0 of type decimal.Decimal which has no callable rint method

This can be fixed by :func:`attaching <attach>` ``pdcast`` to pandas, which
we've been doing throughout this documentation already.  This gives us a new
implementation of the ``round()`` method designed specifically for ``decimal``
objects.

.. doctest:: decimal_support

    >>> pdcast.attach()
    >>> series.round
    <pdcast.patch.virtual.DispatchMethod object at ...>
    >>> series.round()
    0    -2
    1     0
    2     2
    3     2
    dtype: decimal

.. note::

    This is actually even stronger than the normal ``round()`` method since it
    allows for customizable rules.  For instance:

    .. doctest:: decimal_support

        >>> series.round(rule="down")  # toward zero
        0    -1
        1     0
        2     1
        3     2
        dtype: decimal
        >>> series.round(rule="up")  # away from zero
        0    -2
        1     1
        2     2
        3     3
        dtype: decimal

Case Study: datetimes
^^^^^^^^^^^^^^^^^^^^^
By default, pandas stores datetimes in its own ``pandas.Timestamp`` format,
which is built on top of a ``numpy.datetime64`` object with nanosecond
precision.

.. testsetup:: datetime_support

    import numpy as np
    import pandas as pd

.. doctest:: datetime_support

    >>> pd.Timestamp(1)
    Timestamp('1970-01-01 00:00:00.000000001')
    >>> pd.Timestamp(1).asm8
    numpy.datetime64('1970-01-01T00:00:00.000000001')

In essence, these are just 64-bit integers with extra support for timezones,
formatting, etc.  They simply count the number of nanoseconds from the UTC
epoch (1970-01-01 00:00:00), and as such are subject to overflow errors if the
dates are too extreme:

.. doctest:: datetime_support

    >>> pd.to_datetime(2**63 - 1)
    Timestamp('2262-04-11 23:47:16.854775807')
    >>> pd.to_datetime(2**63)
    Traceback (most recent call last):
        ...
    pandas._libs.tslibs.np_datetime.OutOfBoundsDatetime: Out of bounds nanosecond timestamp

This means that only dates within the range

.. doctest:: datetime_support

    >>> str(pd.Timestamp.min), str(pd.Timestamp.max)
    ('1677-09-21 00:12:43.145224193', '2262-04-11 23:47:16.854775807')

can actually be represented by pandas.  In most practical applications, this is
plenty, but it may not be in all cases.  In historical and astronomical data,
for instance, one may need to represent values above or below this range, and
full nanosecond precision may not be necessary.  In these cases, you'll have to
choose some other format to store your data, and consequentially, you **may not
be able to use pandas** for your analysis.  In normal python, you could
represent your dates as built-in ``datetime.datetime`` objects.

.. doctest:: datetime_support

    >>> import datetime
    >>> datetime.datetime.fromisoformat("1500-01-01")
    datetime.datetime(1500, 1, 1, 0, 0)

And in numpy, you could reduce the precision of ``datetime64`` objects to
expand their range.

.. doctest:: datetime_support

    >>> np.datetime64("1500-01-01", "us")  # with microsecond precision
    numpy.datetime64('1500-01-01T00:00:00.000000')
    >>> np.datetime64("-400-01-01", "s")  # with second precision
    numpy.datetime64('-400-01-01T00:00:00')

But if you try to store these in a ``pandas.Series``, you're left with a
``dtype: object`` series that lacks access to the ``.dt`` namespace.

.. doctest:: datetime_support

    >>> pd.Series([datetime.datetime.fromisoformat("1500-01-01")])
    0    1500-01-01 00:00:00
    dtype: object
    >>> pd.Series([np.datetime64("1500-01-01", "us")])
    0    1500-01-01T00:00:00.000000
    dtype: object
    >>> pd.Series([datetime.datetime.fromisoformat("1500-01-01")]).dt.tz_localize("US/Pacific")
    Traceback (most recent call last):
        ...
    AttributeError: Can only use .dt accessor with datetimelike values. Did you mean: 'at'?

.. TODO:
    .. note::

        Pandas has a habit of automatically converting these into
        ``pandas.Timestamp`` objects, even when it's not appropriate.

        .. doctest:: datetime_support

            >>> pd.Series(np.array(["1500-01-01"], dtype="M8[us]"))
            Traceback (most recent call last):
                ...
            pandas._libs.tslibs.np_datetime.OutOfBoundsDatetime: Out of bounds nanosecond timestamp: 1500-01-01 00:00:00

This makes them somewhat unreliable and hard to manipulate.  It also leaves you
unable to use ``pandas.to_datetime()`` to easily convert your data.  In
contrast, ``pdcast`` handles these objects gracefully:

.. doctest:: datetime_support

    >>> import pdcast
    >>> pdcast.to_datetime(2**63 - 1)
    0   2262-04-11 23:47:16.854775807
    dtype: datetime64[ns]
    >>> pdcast.to_datetime(2**63)
    0    2262-04-11 23:47:16.854775
    dtype: datetime[python]
    >>> _[0]
    datetime.datetime(2262, 4, 11, 23, 47, 16, 854775)
    >>> pdcast.to_datetime("October 12th, 400AD")
    0    0400-10-12 00:00:00
    dtype: datetime[python]
    >>> _[0]
    datetime.datetime(400, 10, 12, 0, 0)
    >>> pdcast.to_datetime("-400-01-01")
    0    -400-01-01T00:00:00.000000
    dtype: object
    >>> _[0]
    numpy.datetime64('-400-01-01T00:00:00.000000')

Which lets users represent dates all the way back to the
`age of the universe <https://en.wikipedia.org/wiki/Age_of_the_universe>`_.

.. doctest:: datetime_support

    >>> pdcast.to_datetime([-13.8e9], unit="Y")
    0    -13799998030-01-01T00:00:00
    dtype: object
    >>> _[0]
    numpy.datetime64('-13799998030-01-01T00:00:00')

And, using the same :ref:`dispatching mechanism <decimal_round_demo>` as
earlier, we can even make use of the ``.dt`` namespace just like we would for
``pandas.Timestamp`` objects.

.. doctest:: datetime_support

    >>> pdcast.attach()
    >>> pdcast.to_datetime("1500-01-01")
    0    1500-01-01 00:00:00
    dtype: datetime[python]
    >>> _.dt.tz_localize("Europe/Berlin")
    0    1500-01-01 00:00:00+00:53
    dtype: datetime[python, Europe/Berlin]
    >>> _[0]
    datetime.datetime(1500, 1, 1, 0, 0, tzinfo=<DstTzInfo 'Europe/Berlin' LMT+0:53:00 STD>)

.. warning::

    Extended datetime support is still experimental and may be subject to
    change without notice.
