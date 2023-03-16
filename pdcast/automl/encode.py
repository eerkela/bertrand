from __future__ import annotations

import numpy as np
import pandas as pd
from sklearn.base import BaseEstimator, TransformerMixin
from sklearn.preprocessing import LabelEncoder, OneHotEncoder

import pdcast.convert as convert
import pdcast.detect as detect


features = pd.DataFrame(
    {
        "foo": [1, 2, 3],
        "bar": ["a", "b", "c"],
        "baz": [True, False, True]
    },
    index=["x", "y", "z"]
)
target = pd.DataFrame(
    {
        "qux": [True, True, False]
    },
    index=features.index
)


######################
####    PUBLIC    ####
######################


class FeatureEncoder(BaseEstimator, TransformerMixin):

    def __init__(self):
        self.encoders = {}  # column name -> encoder
        self.feature_names = {}  # column name -> feature name

    def fit(self, X: pd.DataFrame, y=None) -> FeatureEncoder:
        """Fit a FeatureEncoder to test data."""
        # iterate through columns to detect categorical inputs
        for col_name, col in X.items():
            # detect type of series
            col_type = detect.detect_type(col)

            # if series is categorical-like, use a CategoricalEncoder
            if col_type.is_subtype("str, object") or col_type.is_categorical:
                enc = CategoricalEncoder()
            else:
                enc = NumericEncoder()

            # remember settings
            self.encoders[col_name] = enc.fit(col)
            self.feature_names[col_name] = enc.get_feature_names()

        return self

    def fit_transform(self, X: pd.DataFrame, y=None) -> pd.DataFrame:
        """Fit a FeatureEncoder to test data and then encode it."""
        features = None

        # iterate through columns to detect categorical inputs
        for col_name, col in X.items():
            # detect type of series
            col_type = detect.detect_type(col)

            # if series is categorical-like, use a CategoricalEncoder
            if col_type.is_subtype("str, object") or col_type.is_categorical:
                enc = CategoricalEncoder()
            else:  # use a NumericEncoder
                enc = NumericEncoder()

            # remember settings
            result = enc.fit_transform(col)
            features = result if features is None else features.join(result)
            self.encoders[col_name] = enc
            self.feature_names[col_name] = enc.get_feature_names()

        return features

    def transform(self, X: pd.DataFrame) -> pd.DataFrame:
        """Encode test data."""
        if not self.encoders:
            raise RuntimeError(
                f"Cannot encode features: FeatureEncoder has not yet been fit"
            )

        # reorder columns in X to match encoders
        X = reorder_columns(X, list(self.encoders))

        # build encoded result
        result = None
        for col_name, col in X.items():
            encoded = self.encoders[col_name].transform(col)
            result = encoded if result is None else result.join(encoded)
        return result

    def inverse_transform(self, X: pd.DataFrame) -> pd.DataFrame:
        """Decode test data."""
        if not self.encoders:
            raise RuntimeError(
                f"Cannot decode features: FeatureEncoder has not yet been fit"
            )

        # empty DataFrame with same index
        reconstructed = pd.DataFrame(index=X.index)

        # iterate through decoders and invert related features
        for col_name, features in self.feature_names.items():
            features = X.loc[:, features]
            result = self.encoders[col_name].inverse_transform(features)
            reconstructed[col_name] = result

        return reconstructed

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        names = []
        for features in self.feature_names.values():
            names.extend(features)
        return np.array(names, dtype=object)


class TargetEncoder(BaseEstimator, TransformerMixin):

    def __init__(self, classify: bool = False):
        self.classify = classify
        self.encoders = {}  # column name -> encoder
        self.feature_names = {}  # column name -> feature name

    def fit(self, X: pd.DataFrame, y=None) -> FeatureEncoder:
        """Fit a TargetEncoder to test data."""
        # iterate through columns to detect categorical inputs
        for col_name, col in X.items():
            # if TargetEncoder is meant for classification, use ObjectEncoder
            if self.classify:
                enc = ObjectEncoder()
            else:
                enc = NumericEncoder()

            # remember settings
            self.encoders[col_name] = enc.fit(col)
            self.feature_names[col_name] = enc.get_feature_names()

        return self

    def fit_transform(self, X: pd.DataFrame, y=None) -> pd.DataFrame:
        """Fit a FeatureEncoder to test data and then encode it."""
        target = None

        # iterate through columns to detect categorical inputs
        for col_name, col in X.items():
            # if TargetEncoder is meant for classification, use ObjectEncoder
            if self.classify:
                enc = ObjectEncoder()
            else:  # use a NumericEncoder
                enc = NumericEncoder()

            # remember settings
            result = enc.fit_transform(col)
            target = result if target is None else target.join(result)
            self.encoders[col_name] = enc
            self.feature_names[col_name] = enc.get_feature_names()

        return target

    def transform(self, X: pd.DataFrame) -> pd.DataFrame:
        """Encode test data."""
        if not self.encoders:
            raise RuntimeError(
                f"Cannot encode targets: TargetEncoder has not yet been fit"
            )

        # reorder columns in X to match encoders
        X = reorder_columns(X, list(self.encoders))

        # build encoded result
        result = None
        for col_name, col in X.items():
            encoded = self.encoders[col_name].transform(col)
            result = encoded if result is None else result.join(encoded)
        return result

    def inverse_transform(self, X: pd.DataFrame) -> pd.DataFrame:
        """Decode test data."""
        if not self.encoders:
            raise RuntimeError(
                f"Cannot decode targets: TargetEncoder has not yet been fit"
            )

        # empty DataFrame with same index
        reconstructed = pd.DataFrame(index=X.index)

        # iterate through decoders and invert related features
        for col_name, encoder in self.encoders.items():
            target = X.loc[:, [col_name]]
            reconstructed[col_name] = encoder.inverse_transform(target)

        return reconstructed

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        names = []
        for features in self.feature_names.values():
            names.extend(features)
        return np.array(names, dtype=object)


#######################
####    PRIVATE    ####
#######################


# TODO: BooleanEncoder -> just translates booleans into equivalent int8s to
# save space.


class CategoricalEncoder(BaseEstimator, TransformerMixin):
    """An sklearn encoder that converts categorical inputs of any kind into a
    corresponding one-hot matrix for use in model fits.
    """

    def __init__(self):
        self.labels = LabelEncoder()
        self.one_hot = OneHotEncoder(sparse=False, dtype=np.int8)
        self.col_name = None
        self.dtype = None

    def fit(self, X: pd.Series, y=None) -> CategoricalEncoder:
        """Fit a CategoricalEncoder to test data."""
        # remember series attributes
        self.col_name = X.name
        self.dtype = detect.detect_type(X)

        # pass through LabelEncoder
        result = self.labels.fit_transform(X)

        # pass through OneHotEncoder
        self.one_hot.fit(np.atleast_2d(result).T)
        return self

    def fit_transform(self, X: pd.Series) -> pd.DataFrame:
        """Fit a CategoricalEncoder to test data and then encode it."""
        # remember series attributes
        self.col_name = X.name
        self.dtype = detect.detect_type(X)

        # pass through LabelEncoder
        result = self.labels.fit_transform(X)

        # pass through OneHotEncoder
        result = self.one_hot.fit_transform(np.atleast_2d(result).T)
        return pd.DataFrame(
            result,
            columns=self.get_feature_names(),
            index=X.index
        )

    def transform(self, X: pd.Series) -> pd.DataFrame:
        """Transform test data using categorical one-hot encoding."""
        result = self.labels.transform(X)
        result = self.one_hot.transform(np.atleast_2d(result).T)
        return pd.DataFrame(
            result,
            columns=self.get_feature_names(),
            index=X.index
        )

    def inverse_transform(self, X: pd.DataFrame) -> pd.Series:
        """Convert encoded data back to its original representation."""
        result = self.one_hot.inverse_transform(X)
        result = self.labels.inverse_transform(result.ravel())
        return pd.Series(
            result,
            dtype=self.dtype.dtype,
            name=self.col_name,
            index=X.index
        )

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        return np.array(
            [f"{self.col_name}.{str(x)}" for x in self.labels.classes_],
            dtype=object
        )


class NumericEncoder(BaseEstimator, TransformerMixin):
    """An sklearn encoder that converts numeric inputs into a continuous float
    representation for use in model fits.
    """

    def __init__(self):
        self.col_name = None
        self.dtype = None

    def fit(self, X: pd.Series, y=None) -> NumericEncoder:
        """Fit a NumericEncoder to test data."""
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        return self

    def fit_transform(self, X: pd.Series) -> pd.DataFrame:
        """Fit a NumericEncoder to test data and then encode it."""
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        return pd.DataFrame(
            {self.col_name: convert.cast(X, "float64")},
            index=X.index
        )

    def transform(self, X: pd.Series) -> pd.DataFrame:
        """Transform test data, converting it into float representation."""
        return pd.DataFrame(
            {self.col_name: convert.cast(X, "float64")},
            index=X.index
        )

    def inverse_transform(self, X: pd.DataFrame) -> pd.Series:
        """Convert encoded data back to its original representation."""
        if X.shape[1] != 1:
            raise ValueError(f"Input data must have only one column:\n{X}")

        result = convert.cast(X.iloc[:, 0], self.dtype, tol=np.inf)
        result.name = self.col_name
        return result

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        return np.array([self.col_name], dtype=object)


class ObjectEncoder(BaseEstimator, TransformerMixin):
    """An sklearn encoder that converts classification targets into integer
    labels.
    """

    def __init__(self):
        self.labels = LabelEncoder()
        self.col_name = None
        self.dtype = None

    def fit(self, X: pd.Series, y=None) -> ObjectEncoder:
        """Fit a ObjectEncoder to test data."""
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        self.labels.fit(X)
        return self

    def fit_transform(self, X: pd.Series) -> pd.DataFrame:
        """Fit a ObjectEncoder to test data and then encode it."""
        self.col_name = X.name
        self.dtype = detect.detect_type(X)
        return pd.DataFrame(
            {self.col_name: self.labels.fit_transform(X)},
            index=X.index
        )

    def transform(self, X: pd.Series) -> pd.DataFrame:
        """Transform test data, labeling each level separately."""
        return pd.DataFrame(
            {self.col_name: self.labels.transform(X)},
            index=X.index
        )

    def inverse_transform(self, X: pd.DataFrame) -> pd.Series:
        """Convert encoded data back to its original representation."""
        if X.shape[1] != 1:
            raise ValueError(f"Input data must have only one column:\n{X}")

        result = self.labels.inverse_transform(X.iloc[:, 0])
        return pd.Series(
            result,
            dtype=self.dtype.dtype,
            name=self.col_name,
            index=X.index
        )

    def get_feature_names(self) -> np.array:
        """Get output feature names for transformation."""
        return np.array(
            [f"{self.col_name}.{str(x)}" for x in self.labels.classes_],
            dtype=object
        )


def reorder_columns(df: pd.DataFrame, order: list) -> pd.DataFrame:
    """Reorder columns in ``df`` to match the given order."""
    cols = df.columns.tolist()

    # check that every required column is present in df
    missing = [x for x in order if x not in cols]
    if missing:
        raise KeyError(f"DataFrame is missing required columns: {missing}")

    # check that every column in df is present in order
    extra = [x for x in cols if x not in order]
    if extra:
        raise KeyError(f"DataFrame has extra columns: {extra}")

    # rearrange columns to match expected order
    cols = sorted(cols, key=lambda x: order.index(x))
    return df[cols]
