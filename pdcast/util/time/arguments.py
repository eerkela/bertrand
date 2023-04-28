import pytz
import tzlocal

from .timezone import localize, tz_convert, tz_localize


@localize.register_arg(name="tz")
@localize.register_arg(name="naive_tz")
# @tz_localize.register_arg
# @tz_convert.register_arg
def tz(tz: str | pytz.BaseTzInfo | None, state: dict) -> pytz.BaseTzInfo:
    """Convert a time zone specifier into a ``datetime.tzinfo`` object."""
    if tz is None:
        return None

    # trivial case
    if isinstance(tz, pytz.BaseTzInfo):
        return tz

    # local specifier
    if isinstance(tz, str) and tz.lower() == "local":
        return pytz.timezone(tzlocal.get_localzone_name())

    # IANA string
    return pytz.timezone(tz)
