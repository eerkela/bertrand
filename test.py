import pandas as pd
import pdcast


pdcast.attach()


@pdcast.dispatch(
    namespace="test",
    types="int64[numpy], int32, sparse[int]",
    include_subtypes=True
)
def test(series: pdcast.SeriesWrapper) -> pdcast.SeriesWrapper:
    print("Hello, World!")
    return series
