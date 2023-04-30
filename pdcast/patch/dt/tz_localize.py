# pylint: disable=redefined-outer-name, unused-argument
import pytz

from pdcast.decorators.attachable import attachable
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper

from pdcast.util.time import localize, tz


@attachable
@extension_func
def tz_localize(
    series: SeriesWrapper,
    tz: str | pytz.BaseTzInfo | None,
    **unused
) -> SeriesWrapper:
    """TODO"""
    # emulate pandas tz_localize limitation
    if series.element_type.tz:
        raise TypeError("Already tz-aware, use tz_convert to convert.")

    return localize(series, tz=tz, naive_tz=None)


#########################
####    ARGUMENTS    ####
#########################


tz_localize.register_arg(tz)
