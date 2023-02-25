"""This module contains utility functions to help format errors raised by
`pdcast` internals.
"""
from .type_hints import list_like


def shorten_list(list_like: list_like, max_length: int = 5) -> str:
    """Converts a list-like into an abridged string for use in error messages.
    """
    if len(list_like) <= max_length:
        return str(list(list_like))
    shortened = ", ".join(str(i) for i in list_like[:max_length])
    return f"[{shortened}, ...] ({len(list_like)})"
