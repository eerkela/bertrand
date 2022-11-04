from functools import reduce

import pytest


# TODO: unwrap `param` tuples directly?


def skip(*values, reason=None):
    # TODO: handle Parameters as input to skip chained iterables
    return pytest.param(*values, marks=pytest.mark.skip(reason=reason))


def skipif(condition, *values, reason=None):
    if not values:
        raise TypeError(
            "skipif() missing 1 required positional argument: 'value'"
        )

    return pytest.param(
        *values,
        marks=pytest.mark.skipif(condition, reason=reason)
    )


def xfail(
    *values,
    condition=None,
    reason=None,
    raises=None,
    run=True,
    strict=False
):
    return pytest.param(
        *values,
        marks=pytest.mark.xfail(
            condition=condition,
            reason=reason,
            raises=raises,
            run=run,
            strict=strict
        )
    )








def make_parameters(*args) -> list[tuple]:
    """Fuse one or more arguments into a list of tuples, which can be used for
    pytest-style parametrized unit tests.
    """
    # get output length
    length = None

    for a in args:
        if isinstance(a, list):
            if length is None:
                length = len(a)
            elif len(a) != length:
                raise RuntimeError(
                    f"parameters must have the same shape (received list of "
                    f"length {len(a)}, expected {length})"
                )

    if length is None:
        length = 1

    # normalize inputs
    normalized = []
    for a in args:
        if isinstance(a, list):
            normalized.append(a)
        else:
            normalized.append([a] * length)

    # concatenate records
    return reduce(
        lambda a, b: [
            old + new if isinstance(new, tuple) else old + (new,)
            for old, new in zip(a, b)
        ],
        normalized,
        [tuple()] * length  # initializer
    )
