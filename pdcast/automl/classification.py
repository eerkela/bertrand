from __future__ import annotations

import autosklearn
import autosklearn.classification
import numpy as np
import pandas as pd

import pdcast.detect as detect

from pdcast.util.type_hints import datetime_like, timedelta_like

from .encode import FeatureEncoder, TargetEncoder
from .extract import (
    column_specifier, extract_columns, parse_memory_limit, parse_n_jobs,
    parse_time_limit
)


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
        ensemble_size: int = 10,
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
        # initialize encoders
        self.feature_encoder = FeatureEncoder()
        self.target_encoder = TargetEncoder(classify=True)

        # parse model settings
        time_limit = parse_time_limit(time_limit)
        n_jobs = parse_n_jobs(n_jobs)
        memory_limit = parse_memory_limit(memory_limit)

        # parse ensemble_size
        if ensemble_size == 1:
            ensemble_class = autosklearn.ensembles.SingleBest
            ensemble_size = 50  # default for autosklearn
        else:
            ensemble_class = "default"

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

    ###########################
    ####    FIT/PREDICT    ####
    ###########################

    def fit(
        self,
        df: pd.DataFrame,
        target: column_specifier,
        features: column_specifier,
        train_size: float = 0.67,
        seed: int = None
    ):
        """Fit the classifier to the given training set (X, y)."""
        target = extract_columns(df, target)
        features = extract_columns(df, features)
        if target.shape[0] != features.shape[0]:
            raise ValueError(
                f"target length does not match features length: "
                f"{target.shape[0]} != {features.shape[0]}"
            )

        # encode features + target
        target = self.target_encoder.fit_transform(target)
        features = self.feature_encoder.fit_transform(features)
    
        # split train and test sets
        np.random.seed(seed)
        if not 0.0 < train_size < 1.0:
            raise ValueError(f"'train_size' must be between 0 and 1")
        mask = np.random.rand(target.shape[0]) < train_size
        x_train = features[mask]
        x_test = features[~mask]
        y_train = target[mask]
        y_test = target[~mask]

        # do fit
        return super().fit(
            X=x_train,
            y=y_train,
            X_test=x_test,
            y_test=y_test,
            dataset_name=getattr(df, "name", None)
        )

    def predict(
        self,
        X: pd.DataFrame,
        batch_size: int = None,
        n_jobs: int = None
    ) -> pd.DataFrame:
        """Predict target values based on new features."""
        # encode features
        features = self.feature_encoder.transform(X)

        # generate prediction
        result = pd.DataFrame(
            super().predict(features, batch_size=batch_size, n_jobs=n_jobs),
            index=features.index,
            columns=list(self.target_encoder.encoders)
        )

        # decode predictions
        return self.target_encoder.inverse_transform(result)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __repr__(self) -> str:
        targets = " + ".join(str(x) for x in self.target_encoder.encoders)
        features = " + ".join(str(x) for x in self.feature_encoder.encoders)
        return f"AutoClassifier({targets} ~ {features})"


#######################
####    PRIVATE    ####
#######################


import sklearn.datasets
import sklearn.metrics
import sklearn.model_selection


# load data
X, y = sklearn.datasets.load_digits(return_X_y=True)


# split train, test
x_train, x_test, y_train, y_test = (
    sklearn.model_selection.train_test_split(X, y, random_state=1)
)


# convert to dataframe
x_train = pd.DataFrame(x_train)
x_test = pd.DataFrame(x_test)
y_train = pd.DataFrame(y_train)
y_test = pd.DataFrame(y_test)

train = x_train.copy()
train.insert(64, 64, y_train.copy())
test = x_test.copy()
test.insert(64, 64, y_test.copy())


# train
model = AutoClassifier(time_limit=30, ensemble_size=1)
model.fit(train, y_train, x_train)


# compare
result = pd.DataFrame({"observed": y_test.iloc[:, 0]})
result = result.join(model.predict(x_test))
