from __future__ import annotations

import numpy as np
import pandas as pd
from sklearn.base import BaseEstimator, TransformerMixin
from sklearn.preprocessing import LabelEncoder, OneHotEncoder

import pdcast.convert as convert
import pdcast.detect as detect


# TODO: encode_target uses simple LabelEncoder or NumericEncoder


######################
####    PUBLIC    ####
######################


def encode_features(
    df: pd.DataFrame
) -> tuple[pd.DataFrame, dict[str, CategoricalEncoder]]:
    features = None
    encoders = {}

    # iterate through columns to detect categorical inputs
    for col_name, col in df.items():
        col_type = detect.detect_type(col)
        if col_type.is_subtype("str, object") or col_type.is_categorical:
            cat = CategoricalEncoder()
            result = cat.fit_transform(col)
            encoders[col_name] = cat
        else:
            num = NumericEncoder()
            result = num.fit_transform(col)
            encoders[col_name] = num
        features = result if features is None else features.join(result)

    return features, encoders


#######################
####    PRIVATE    ####
#######################


class CategoricalEncoder(BaseEstimator, TransformerMixin):
    """An sklearn encoder that converts categorical inputs of any kind into a
    corresponding one-hot matrix.
    """

    def __init__(self):
        self.labels = LabelEncoder()
        self.one_hot = OneHotEncoder(sparse=False, dtype=np.int8)
        self.col_name = None
        self.dtype = None

    def fit(self, X: pd.Series, y=None) -> CategoricalEncoder:
        """Fit a CategoricalEncoder to X."""
        # remember series attributes
        self.col_name = X.name
        self.dtype = detect.detect_type(X)

        # pass through LabelEncoder
        X = self.labels.fit_transform(X)

        # pass through OneHotEncoder
        self.one_hot.fit(np.atleast_2d(X).T)
        return self

    def fit_transform(self, X: pd.Series) -> pd.DataFrame:
        """Fit to data, then transform it."""
        # remember series attributes
        self.col_name = X.name
        self.dtype = detect.detect_type(X)

        # pass through LabelEncoder
        X = self.labels.fit_transform(X)

        # pass through OneHotEncoder
        X = self.one_hot.fit_transform(np.atleast_2d(X).T)
        return pd.DataFrame(X, columns=self.get_feature_names())

    def transform(self, X: pd.Series) -> pd.DataFrame:
        """Transform X using categorical one-hot encoding."""
        X = self.labels.transform(X)
        X = self.one_hot.transform(np.atleast_2d(X).T)
        return pd.DataFrame(X, columns=self.get_feature_names())

    def inverse_transform(self, X: pd.DataFrame) -> pd.Series:
        """Convert the data back to the original representation."""
        X = self.one_hot.inverse_transform(X)
        X = self.labels.inverse_transform(X.ravel())
        return pd.Series(X, dtype=self.dtype.dtype, name=self.col_name)

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        return np.array(
            [f"{self.col_name}.{str(x)}" for x in self.labels.classes_],
            dtype=object
        )


class NumericEncoder(BaseEstimator, TransformerMixin):
    """An sklearn encoder that converts numeric inputs into a continuous float
    representation.
    """

    def __init__(self):
        self.col_name = None
        self.dtype = None

    def fit(self, X: pd.Series, y=None) -> NumericEncoder:
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        return self

    def fit_transform(self, X: pd.Series) -> pd.DataFrame:
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        return pd.DataFrame(
            {self.col_name: convert.cast(X, "float64", downcast=True)}
        )

    def transform(self, X: pd.Series) -> pd.DataFrame:
        return pd.DataFrame(
            {self.col_name: convert.cast(X, "float64", downcast=True)}
        )

    def inverse_transform(self, X: pd.DataFrame) -> pd.Series:
        result = convert.cast(X.iloc[:, 0], self.dtype)
        result.name = self.col_name
        return result

    def get_feature_names(self) -> np.array:
        return np.array([self.col_name], dtype=object)


def cols_are_homogenous(df: pd.DataFrame) -> bool:
    """Ensure that a dataframe's columns are homogenously-typed.

    This is used to validate the input to model fits.
    """
    shared_type = None

    for col_name, col in df.items():
        # detect column type
        col_type = detect.detect_type(col)

        # disregard empty columns
        if col_type is None:
            continue

        # assign shared_type
        if shared_type is None:
            shared_type = col_type

        # ensure match
        if col_type != shared_type:
            return False

    # types are homogenous
    return True

