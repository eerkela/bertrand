# """Flexible type extensions for pandas.

# Subpackages
# -----------
# convert
#     Extendable conversions between types in the ``pdcast`` type system.

# patch
#     Direct ``pdcast`` integration and type-aware attribute dispatch for
#     ``pandas.Series`` and ``pandas.DataFrame`` objects.

# types
#     Defines the structure and contents of the ``pdcast`` type system.

# util
#     Utilities for ``pdcast``-related functionality.

# Modules
# -------
# check
#     Fast type checks within the ``pdcast`` type system.

# detect
#     Type inference for arbitrary, vectorized data.

# resolve
#     Easy construction of data types from type specifiers, including a
#     domain-specific mini-language for referring to types.
# """
# # pylint: disable=undefined-variable, redefined-builtin
# from .convert import (
#     cast, categorize, decategorize, densify, sparsify, to_boolean, to_integer,
#     to_float, to_complex, to_decimal, to_datetime, to_timedelta, to_string
# )
# from .decorators.base import FunctionDecorator, Signature, Arguments
# from .decorators.attachable import (
#     attachable, Attachable, ClassMethod, InstanceMethod, Namespace, Property,
#     StaticMethod, VirtualAttribute
# )
# from .decorators.extension import (
#     extension_func, ExtensionFunc, ExtensionSignature, ExtensionArguments
# )
# from .decorators.dispatch import (
#     dispatch, DispatchFunc, DispatchSignature, DispatchArguments
# )
# from .patch.base import attach, detach


# # from .decorators import attach, dispatch, introspect
# # from .convert import cast
# from .types import *
# from .structs import LinkedList, LinkedSet, LinkedDict


# # importing * from types also masks module names, which can be troublesome
# del base            # type: ignore
# del boolean         # type: ignore
# del categorical     # type: ignore
# del complex
# # del datetime      # type: ignore
# del decimal         # type: ignore
# del float
# del integer         # type: ignore
# del missing         # type: ignore
# del object
# del sparse          # type: ignore
# del string          # type: ignore
# del timedelta       # type: ignore


__version__ = "0.5.1"  # handled by bumpver


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    from pathlib import Path
    return str(Path(__file__).absolute().parent.parent)
