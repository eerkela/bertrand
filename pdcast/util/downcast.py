from pdcast import convert
from pdcast import types
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension

from pdcast.util import wrapper
from pdcast.util.round import Tolerance


@extension.extension_func
@dispatch.dispatch
def downcast(
    series: wrapper.SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType
) -> wrapper.SeriesWrapper:
    """Downcast an :class:`AtomicType` to compress an observed series.

    Parameters
    ----------
    series : SeriesWrapper
        The series to be fitted.  This is always provided as a
        :class:`SeriesWrapper` object.
    tol : Tolerance
        The tolerance to use for downcasting.  This sets an upper bound on
        the amount of precision loss that can occur during this operation.
    smallest : CompositeType, default None
        Forbids downcasting past the specified types.  If this is omitted,
        then downcasting will proceed to the smallest possible type rather
        than stopping early.

    Returns
    -------
    SeriesWrapper
        The transformed series, returned as a :class:`SeriesWrapper`.

    Notes
    -----
    Downcasting allows users to losslessly compress numeric data by
    reducing the precision or range of a data type.
    """
    raise NotImplementedError(
        f"could not downcast data of type: {series.element_type}"
    )


@downcast.overload("int")
def downcast_integer(
    series: wrapper.SeriesWrapper,
    tol: Tolerance,
    smallest: CompositeType = None
) -> wrapper.SeriesWrapper:
    """Reduce the itemsize of an integer type to fit the observed range."""
    # get downcast candidates
    smaller = series.element_type.smaller
    if smallest is not None:
        if series.element_type in smallest:
            smaller = []
        else:
            filtered = []
            for typ in reversed(smaller):
                filtered.append(typ)
                if typ in smallest:
                    break
            smaller = reversed(filtered)

    # convert range to python int for consistent comparison
    if self.is_na(series.min):
        min_val = self.max  # NOTE: we swap these to maintain upcast()
        max_val = self.min  # behavior for upcast-only types
    else:
        min_val = int(series.min)
        max_val = int(series.max)

    # search for smaller data type that fits observed range
    for t in smaller:
        if min_val < t.min or max_val > t.max:
            continue
        return super().to_integer(
            series,
            dtype=t,
            downcast=None,
            errors="raise"
        )

    return series



@downcast.overload("float")
def downcast_float(
    series: wrapper.SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType
) -> wrapper.SeriesWrapper:
    """Reduce the itemsize of a float type to fit the observed range."""
    # get downcast candidates
    smaller = series.element_type.smaller
    if smallest is not None:
        filtered = []
        for typ in reversed(smaller):
            filtered.append(typ)
            if typ in smallest:
                break  # stop at largest type contained in `smallest`
        smaller = reversed(filtered)

    # try each candidate in order
    for smol in smaller:
        try:
            attempt = convert.to_float(
                series,
                dtype=smol,
                tol=tol,
                downcast=None,
                errors="raise"
            )
        except Exception:
            continue

        # candidate is valid
        if attempt.within_tol(series, tol=tol.real).all():
            return attempt

    # return original
    return series


@downcast.overload("complex")
def downcast_complex(
    series: wrapper.SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType
) -> wrapper.SeriesWrapper:
    """Reduce the itemsize of a complex type to fit the observed range."""
    # downcast real and imaginary component separately
    real = downcast(
        series.real,
        tol=tol,
        smallest=smallest
    )
    imag = downcast(
        series.imag,
        tol=Tolerance(tol.imag),
        smallest=smallest
    )

    # use whichever type is larger
    largest = max(
        [real.element_type, imag.element_type],
        key=lambda x: x.itemsize or np.inf
    )
    target = largest.equiv_complex
    result = real + imag * 1j
    return wrapper.SeriesWrapper(
        result.series.astype(target.dtype, copy=False),
        hasnans=real.hasnans or imag.hasnans,
        element_type=target
    )

