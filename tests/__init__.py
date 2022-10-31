from functools import reduce


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
