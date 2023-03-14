from __future__ import annotations

import autosklearn
import autosklearn.classification
import numpy as np
import pandas as pd
import psutil
from sklearn.preprocessing import LabelEncoder

import pdcast.convert as convert
import pdcast.detect as detect

from pdcast.util.type_hints import datetime_like, timedelta_like


class AutoClassifier(autosklearn.classification.AutoSklearnClassifier):
    """An ``AutoSklearnClassifier`` that automatically encodes and decodes
    its input data and prediction results.
    """

    def __init__(
        self,
        classifiers: list[str] = None,
        data_preprocessors: list[str] = None,
        balancers: list[str] = None,
        feature_preprocessors: list[str] = None,
        ensemble_size: int = 1,
        resampling: str = "holdout",
        folds: int = 5,
        metric: autosklearn.metrics.Scorer = None,
        time_limit: int | datetime_like | timedelta_like = 3600,
        memory_limit: int | float = 3072,
        seed: int = 1,
        n_jobs: int = -1,
        dask_client = None,
        smac_scenario_args: dict = None,
        logging_config: dict = None
    ):
        # parse ensemble_size
        if ensemble_size == 1:
            ensemble_class = autosklearn.ensembles.SingleBest
            ensemble_size = 50  # default for autosklearn
        else:
            ensemble_class = "default"

        # parse time_limit
        time_limit = convert.cast(
            time_limit,
            "int[python]",
            unit="s",
            since=pd.Timestamp.now()
        )
        if len(time_limit) != 1:
            raise ValueError(f"'time_limit' must be scalar:\n{time_limit}")
        time_limit = time_limit[0]

        # parse parallel jobs
        if n_jobs is None:
            n_jobs = 1
        elif n_jobs == -1:
            n_jobs = psutil.cpu_count()
        elif n_jobs < 1:
            raise ValueError(
                f"'n_jobs' must be None, -1, or >= 1, not {n_jobs}"
            )

        # parse memory_limit
        if isinstance(memory_limit, float):
            total_memory = psutil.virtual_memory().total // (1024**2)
            memory_limit = int(memory_limit * total_memory)

        # build include list
        include = [
            ("data_preprocessor", data_preprocessors),
            ("balancing", balancers),
            ("feature_preprocessor", feature_preprocessors),
            ("classifier", classifiers)
        ]       

        # construct classifier
        super().__init__(
            time_left_for_this_task=time_limit,
            ensemble_class=ensemble_class,
            ensemble_nbest=ensemble_size,
            seed=seed,
            memory_limit=memory_limit,
            include={k: v for k, v in include if v is not None},
            resampling_strategy=resampling,
            resampling_strategy_arguments={
                "train_size": 0.67,  # for cross-validation, not overall
                "shuffle": True,
                "folds": folds,
            },
            n_jobs=n_jobs,
            dask_client=dask_client,
            smac_scenario_args=smac_scenario_args,
            logging_config=logging_config,
            metric=metric
        )

    # def fit(
    #     self,
    #     X,
    #     y,
    #     X_test=None,
    #     y_test=None,
    #     feat_type=None,
    #     dataset_name=None
    # ):
    #     """Fit the classifier to the given training set (X, y)."""


#######################
####    PRIVATE    ####
#######################


def extract_columns(
    df: pd.DataFrame,
    cols: str | int | pd.Series | list[str | int | pd.Series] | pd.DataFrame
) -> pd.DataFrame:
    """Extract columns from a ``pandas.DataFrame`` for fitting."""
    # column name
    if isinstance(cols, str):
        result = df[cols]

    # column index
    elif isinstance(cols, int):
        result = df.iloc[:, cols]

    # multiple columns
    elif isinstance(cols, list):
        result = pd.DataFrame()
        for x in cols:
            if isinstance(x, str):
                result[x] = df[x]
            elif isinstance(x, int):
                label = df.columns[x]
                result[label] = df[label]
            else:
                result[x.name] = x

    # pandas data structure
    else:
        result = cols

    # wrap as dataframe
    if isinstance(result, pd.Series):
        return pd.DataFrame(result)
    return result


def fit(
    df: pd.DataFrame,
    target: str | int | pd.Series | list[str | int | pd.Series] | pd.DataFrame,
    features: str | int | pd.Series | list[str | int | pd.Series] | pd.DataFrame,
    train_size: float = 0.7,
    seed: int = None,
    **kwargs
) -> AutoClassifier:
    """Build and fit an autoclassifier for the given DataFrame."""
    # get dependent, independent columns
    target = extract_columns(df, target)
    features = extract_columns(df, features)
    if target.shape[0] != features.shape[0]:
        raise ValueError(
            f"output length does not match input length: {target.shape[0]} != "
            f"{features.shape[0]}"
        )

    # ensure output types match (encode if necessary)
    target_type = None
    for x in target.columns:
        col_type = detect.detect_type(target[x])
        if col_type is None:
            continue
        if target_type is None:
            target_type = col_type
        if col_type != target_type:
            raise TypeError(f"Output is of mixed type: {target}")

    # detect input features
    feature_types = []
    for x in features.columns:
        col = features[x]
        col_model = detect.detect_type(col).model_type
        if col_model == "Categorical":
            # features[x] = LabelEncoder().fit_transform(col)
            raise NotImplementedError()
        feature_types.append(col_model)

    # split train and test
    np.random.seed(seed)
    if not 0.0 < train_size < 1.0:
        raise ValueError(f"'train_size' must be between 0 and 1")
    mask = np.random.rand(target.shape[0]) < train_size
    x_train = features[mask]
    y_train = target[mask]
    x_test = features[~mask]
    y_test = target[~mask]

    # generate regressor/classifier from target type
    model = AutoClassifier(**kwargs)

    return model.fit(
        X=x_train,
        y=y_train,
        X_test=x_test,
        y_test=y_test,
        # feat_type=feature_types,
        dataset_name=getattr(df, "name", None)
    )


def main(**kwargs):
    import sklearn.datasets
    import sklearn.metrics
    import sklearn.model_selection

    X, y = sklearn.datasets.load_digits(return_X_y=True)
    X = pd.DataFrame(X)
    y = pd.DataFrame(y)
    df = X.copy()
    df[64] = y.copy()

    model = fit(df, y, X, **kwargs)

    final = pd.DataFrame(
        {
            "predicted": model.predict(X),
            "observed": y.iloc[:, 0]
        }
    )
    return final
