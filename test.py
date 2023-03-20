import pandas as pd
import pdcast


pdcast.attach()


@pdcast.dispatch(
    namespace="test",
    types="int64[numpy], float32",
    exact=False
)
def test(series: pdcast.SeriesWrapper) -> pdcast.SeriesWrapper:
    print("Hello, World!")
    return series
