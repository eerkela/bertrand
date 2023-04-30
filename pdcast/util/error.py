"""This module contains utility functions to help format errors raised by
`pdcast` internals.
"""
from .type_hints import list_like


def shorten_list(seq: list_like, max_length: int = 5) -> str:
    """Converts a list-like into an abridged string for use in error messages.
    """
    if len(seq) <= max_length:
        return str(list(seq))
    shortened = ", ".join(str(i) for i in seq[:max_length])
    return f"[{shortened}, ...] ({len(seq)})"
