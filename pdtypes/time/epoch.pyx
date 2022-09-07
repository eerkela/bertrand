import datetime

from pdtypes.check import check_dtype
from pdtypes.util.array import is_scalar
from pdtypes.util.type_hints import date_like, datetime_like

from .date import decompose_date


# TODO: epoch() returns a datetime corresponding to the given epoch, which
# can then be passed through decompose_date/datetime_to_ns to obtain the
# required offsets


#########################
####    Constants    ####
#########################


cdef dict common_epoch_dates = {
    "julian": (-4713, 11, 24),
    "gregorian": (1582, 10, 14),
    "reduced_julian": (1858, 11, 16),
    "lotus": (1899, 12, 30),
    "sas": (1960, 1, 1),
    "utc": (1970, 1, 1),
    "gps": (1980, 1, 6),
    "cocoa": (2001, 1, 1)
}


######################
####    Public    ####
######################


def epoch_date(epoch: tuple | str | date_like) -> tuple[int, int, int]:
    """Split an epoch date or string specifier into its individual components
    `(year, month, day)`.
    """
    # base case - integer tuple `(year, month, day)`
    if (isinstance(epoch, tuple) and
        len(epoch) == 3 and
        check_dtype(epoch, int)):
        return epoch

    # parsing required - ensure scalar
    if not is_scalar(epoch):
        raise ValueError(f"`epoch` must be scalar, not {type(epoch)}")

    # string epoch identifier
    if isinstance(epoch, str):
        try:
            return common_epoch_dates[epoch]
        except KeyError as err:
            err_msg = (f"if `epoch` is a string, it must be one of "
                       f"{tuple(common_epoch_dates)}, not {epoch}")
            raise ValueError(err_msg) from err

    # custom date-like
    if check_dtype(epoch, ("datetime", datetime.date)):
        components = decompose_date(epoch)
        return tuple(components.values())

    raise TypeError(f"could not parse epoch: {epoch}")
