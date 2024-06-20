"""Example Python module."""


def fibonacci1(n: int = 0, m: int = 1, steps: int = 10) -> list[int]:
    """Compute the fibonacci sequence starting at n, m and continuing for `length`
    iterations.

    Parameters
    ----------
    n : int, default 0
        first number in the sequence
    m : int, default 1
        second number in the sequence
    steps : int, default 10
        number of iterations to compute

    Returns
    -------
    list[int]
        The computed sequence.
    """
    result = [n, m]

    def helper(n: int, m: int, steps: int) -> None:
        if steps > 0:
            x = n + m
            result.append(x)
            helper(m, x, steps - 1)

    helper(n, m, steps - 2)
    return result
