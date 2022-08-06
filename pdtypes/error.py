"""This module contains utility functions to help format errors raised by
`pdtypes` internals.
"""
import inspect

from pdtypes.util.type_hints import array_like


def error_trace(self_name: str = "self",
                cls_name: str = "cls",
                stack_index: int = 1,
                include_module: bool = True,
                include_class: bool = True) -> str:
    """Returns a quick trace to the calling namespace for a function/method,
    in case of an error.

    When invoked from within a function/method, this will yield the module
    name, class name, and function/method name of the invoking code block,
    separated by '.' characters and returned as a string.  This is intended to
    add diagnostic information to an error message, to better facilitate
    logging and debugging.
    """
    stack = inspect.stack()
    if len(stack) < stack_index + 1:
        return ""
    parentframe = stack[stack_index][0]
    name = []

    # get module name (if applicable)
    if include_module:
        module = inspect.getmodule(parentframe)
        if module:  # module can be None if executed directly in console
            name.append(module.__name__)

    # get class name (if applicable and not static)
    if include_class:
        if self_name in parentframe.f_locals:  # for regular methods
            name.append(parentframe.f_locals[self_name].__class__.__name__)
        elif cls_name in parentframe.f_locals:  # for classmethods
            name.append(parentframe.f_locals[cls_name].__name__)

    # get function/method name
    callable_name = parentframe.f_code.co_name
    if callable_name != "<module>":
        name.append(callable_name)

    # avoid circular refs and frame leaks
    del parentframe, stack
    return ".".join(name)


def shorten_list(list_like: array_like, max_length: int = 5) -> str:
    """Converts a list-like into an abridged string for use in error messages.
    """
    if len(list_like) <= max_length:
        return str(list(list_like))
    shortened = ", ".join(str(i) for i in list_like[:max_length])
    return f"[{shortened}, ...] ({len(list_like)})"
