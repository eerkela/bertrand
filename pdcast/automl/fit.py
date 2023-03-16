from __future__ import annotations

import autosklearn
import pandas as pd

import pdcast.detect as detect
from pdcast.util.type_hints import datetime_like, timedelta_like

from .parse import (
    column_specifier, extract_columns, parse_memory_limit, parse_n_jobs,
    parse_time_limit
)
from .models import AutoClassifier, AutoRegressor


def fit(
    df: pd.DataFrame,
    target: column_specifier,
    features: column_specifier = None,
    algorithms: str | list[str] = None,
    data_preprocessors: str | list[str] = None,
    balancers: str | list[str] = None,
    feature_preprocessors: str | list[str] = None,
    train_size: float = 0.9,  # for performance over time
    ensemble_size: int = 10,
    resampling: str = "holdout",
    folds: int = 5,
    time_limit: int | datetime_like | timedelta_like = 3600,
    memory_limit: int | float = 3072,
    seed: int = 1,
    n_jobs: int = -1,
    metric: autosklearn.metrics.Scorer = None,
    dask_client = None,
    smac_scenario_args: dict = None,
    logging_config: dict = None
) -> AutoClassifier | AutoRegressor:
    """Do a generalized automl fit on a DataFrame.

    Model selection is determined by the inferred type of the ``target``
    column(s).
    """
    # parse model settings
    time_limit = parse_time_limit(time_limit)
    n_jobs = parse_n_jobs(n_jobs)
    memory_limit = parse_memory_limit(memory_limit)
    if isinstance(algorithms, str):
        algorithms = [algorithms]
    if isinstance(data_preprocessors, str):
        data_preprocessors = [data_preprocessors]
    if isinstance(balancers, str):
        balancers = [balancers]
    if isinstance(feature_preprocessors, str):
        feature_preprocessors = [feature_preprocessors]

    # extract columns
    target = extract_columns(df, target)
    if features is None:
        features = df.drop(target.columns, axis=1)
    else:
        features = extract_columns(df, features)

    # iterate through target columns to determine model
    model_select = None
    for col_name, col in target.items():
        # detect type of column
        col_type = detect.detect_type(col)

        # if column is categorical, use an AutoClassifier
        if col_type.is_subtype("bool, str, object") or col_type.is_categorical:
            model_select = "classify" if model_select is None else model_select
            if model_select != "classify":
                raise TypeError(
                    f"target columns must be uniformly categorical or "
                    f"numeric, not a mixture of both:\n{target}"
                )

        # if column is numeric, use an AutoRegressor
        else:
            model_select = "regress" if model_select is None else model_select
            if model_select != "regress":
                raise TypeError(
                    f"target columns must be uniformly categorical or "
                    f"numeric, not a mixture of both:\n{target}"
                )

    # choose a model
    model_kwargs = {
        "target": target.columns.tolist(),
        "features": features.columns.tolist(),
        "data_preprocessors": data_preprocessors,
        "balancers": balancers,
        "feature_preprocessors": feature_preprocessors,
        "ensemble_size": ensemble_size,
        "resampling": resampling,
        "folds": folds,
        "metric": metric,
        "time_limit": time_limit,
        "memory_limit": memory_limit,
        "seed": seed,
        "n_jobs": n_jobs,
        "dask_client": dask_client,
        "smac_scenario_args": smac_scenario_args,
        "logging_config": logging_config
    }
    if model_select == "classify":
        print("Generating AutoClassifier...")
        model = AutoClassifier(classifiers=algorithms, **model_kwargs)
    elif model_select == "regress":
        print("Generating AutoRegressor...")
        model = AutoRegressor(regressors=algorithms, **model_kwargs)
    else:
        raise ValueError(f"no model could be chosen for target:\n{target}")

    # return fitted model
    return model.fit(df, train_size=train_size, seed=seed)





# import sklearn.datasets
# import sklearn.metrics
# import sklearn.model_selection
# import pdcast.convert as convert


# # load data
# data = sklearn.datasets.load_digits()  # returned as Bunch object

# # convert to DataFrame
# df = pd.DataFrame(data["data"], columns=data["feature_names"])
# df.insert(df.shape[1], "digit", convert.cast(data["target"], categorical=True))
# # df.insert(df.shape[1], "digit", pd.Series(data["target"]))

# # split train, test
# train, test = sklearn.model_selection.train_test_split(df, random_state=1)

# # train
# model = fit(df, "digit", time_limit=30, ensemble_size=1)

# # compare
# result = pd.DataFrame({"observed": test["digit"]})
# result.insert(1, "predicted", model.predict(test)["digit"])
