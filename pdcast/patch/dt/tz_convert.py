# pylint: disable=redefined-outer-name, unused-argument
import pytz

from pdcast.decorators.attachable import attachable
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper

from pdcast.util.time import localize, tz


@attachable
@extension_func
def tz_convert(
    series: SeriesWrapper,
    tz: str | pytz.BaseTzInfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_convert limitation
    if not series.element_type.tz:
        raise TypeError(
            "Cannot convert tz-naive Timestamp, use tz_localize to localize"
        )

    return localize(series, tz=tz, naive_tz=None)


#########################
####    ARGUMENTS    ####
#########################


tz_convert.register_arg(tz)
