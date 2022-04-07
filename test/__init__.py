from datetime import datetime, timedelta, timezone
import random

import pandas as pd
import pytz


class NonCastableObject:
    pass


class CastableObject:
    
    def to_datetime(self) -> datetime:
        return datetime.fromtimestamp(random.randint(0, 86400),
                                        tz=timezone.utc)

    def to_timedelta(self) -> timedelta:
        return timedelta(seconds=random.randint(0, 86400))
    
    def __int__(self) -> int:
        return random.randint(0, 10)

    def __float__(self) -> float:
        return random.random()

    def __complex__(self) -> complex:
        return complex(random.random(), random.random())

    def __str__(self) -> str:
        return chr(random.randint(0, 26) + ord("a"))

    def __bool__(self) -> bool:
        return bool(random.randint(0, 1))


random.seed(12345)
SIZE = 3
INTEGERS = [-1 * SIZE // 2 + i + 1 for i in range(SIZE)]
FLOATS = [-1 * SIZE // 2 + i + 1 + random.random() for i in range(SIZE)]
TEST_DATA = {
    int: {
        "integers": INTEGERS,
        "whole floats": [float(i) for i in INTEGERS],
        "real whole complex": [complex(i, 0) for i in INTEGERS],
    },
    float: {
        "decimal floats": FLOATS,
        "real decimal complex": [complex(f, 0) for f in FLOATS],
    },
    complex: {
        "imaginary complex": [complex(f, f) for f in FLOATS],
    },
    str: {
        "integer strings": [str(i) for i in INTEGERS],
        "whole float strings": [str(float(i)) for i in INTEGERS],
        "decimal float strings": [str(f) for f in FLOATS],
        "real whole complex strings": [str(complex(i, 0)) for i in INTEGERS],
        "real decimal complex strings": [str(complex(f, 0)) for f in FLOATS],
        "imaginary complex strings": [str(complex(f, f)) for f in FLOATS],
        "character strings": [chr(i % 26 + ord("a")) for i in range(SIZE)],
        "boolean strings": [str(bool((i + 1) % 2)) for i in range(SIZE)],
        "aware datetime strings":
            [str(datetime.fromtimestamp(i, tz=timezone.utc)) for i in INTEGERS],
        "aware ISO 8601 strings":
            [datetime.fromtimestamp(i, tz=timezone.utc).isoformat()
             for i in INTEGERS],
        "naive datetime strings":
            [str(datetime.fromtimestamp(i)) for i in INTEGERS],
        "naive ISO 8601 strings":
            [datetime.fromtimestamp(i).isoformat() for i in INTEGERS],
        "aware/naive datetime strings":
            [str(datetime.fromtimestamp(i, tz=timezone.utc)) if i % 2
             else str(datetime.fromtimestamp(i)) for i in INTEGERS],
        "aware/naive ISO 8601 strings":
            [datetime.fromtimestamp(i, tz=timezone.utc).isoformat() if i % 2
             else datetime.fromtimestamp(i).isoformat() for i in INTEGERS],
        "mixed timezone datetime strings":
            [str(
                datetime.fromtimestamp(
                    i,
                    tz=pytz.timezone(
                        pytz.all_timezones[i % len(pytz.all_timezones)]
                    )
                )
             ) for i in INTEGERS],
        "mixed timezone ISO 8601 strings":
            [datetime.fromtimestamp(
                i,
                tz=pytz.timezone(
                    pytz.all_timezones[i % len(pytz.all_timezones)]
                )
             ).isoformat() for i in INTEGERS],
        "timedelta strings":
            [str(timedelta(seconds=i + 1)) for i in range(SIZE)],
        "pd.Timedelta strings":
            [str(pd.Timedelta(timedelta(seconds=i + 1))) for i in range(SIZE)]
    },
    bool: {
       "booleans":
            [bool((i + 1) % 2) for i in range(SIZE)] 
    },
    datetime: {
        "aware datetimes":
            [datetime.fromtimestamp(i, tz=timezone.utc) for i in INTEGERS],
        "naive datetimes": [datetime.fromtimestamp(i) for i in INTEGERS],
        "aware/naive datetimes":
            [datetime.fromtimestamp(i, tz=timezone.utc) if i % 2
             else datetime.fromtimestamp(i) for i in INTEGERS],
        "mixed timezone datetimes":
            [datetime.fromtimestamp(
                i,
                tz = pytz.timezone(
                    pytz.all_timezones[i % len(pytz.all_timezones)]
                )
             ) for i in INTEGERS]
    },
    timedelta: {
        "timedeltas": [timedelta(seconds=i + 1) for i in INTEGERS]
    },
    object: {
        "nones":
            [None for _ in range(SIZE)],
        "castable objects":
            [CastableObject() for _ in range(SIZE)],
        "non castable objects":
            [NonCastableObject() for _ in range(SIZE)]
    }
}
ALL_DATA = {col_name: data for v in TEST_DATA.values()
            for col_name, data in v.items()}