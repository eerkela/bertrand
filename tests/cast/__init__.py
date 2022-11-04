from __future__ import annotations

import numpy as np
import pandas as pd


class Parameters:

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def __init__(
        self,
        pars_list: list[tuple],
        **overrides
    ) -> None:
        if self.has_nonseries_input:
            raise ValueError(
                f"tuples must contain a pd.Series object as test input at "
                f"second index, not {self.nonseries_input_type}"
            )

    @classmethod
    def nocheck(
        cls,
        pars_list: list[tuple],
        **overrides
    ):
        return cls.__new__(cls, pars_list, **overrides)

    def __new__(
        cls,
        pars_list: list[tuple],
        **overrides
    ):
        has_nonseries_input = False
        nonseries_input_type = None

        # check pars_list is a list
        if not isinstance(pars_list, list):
            raise TypeError(f"pars_list must be a list, not {type(pars_list)}")

        # check whether pars_list contains expected test records
        for element in pars_list:
            # check list contains only tuples
            if not isinstance(element, tuple):
                raise TypeError(
                    f"pars_list must only contain tuples, not {type(element)}"
                )

            # check tuples are length 3
            if len(element) != 3:
                raise ValueError(
                    f"tuples must all be of length 3, not {len(element)}"
                )

            # check tuples contain a **kwargs dict as first index
            if not isinstance(element[0], dict):
                raise ValueError(
                    f"tuples must contain a **kwargs dict at first index, not "
                    f"{type(element[0])}"
                )

            # check tuples contain an input Series as second index
            if not isinstance(element[1], pd.Series):
                has_nonseries_input = True
                nonseries_input_type = type(element[1])

            # check tuples contain an output Series as third index
            if not isinstance(element[2], pd.Series):
                raise ValueError(
                    f"tuples must contain a pd.Series object as test output "
                    f"at third index, not {type(element[2])}"
                )

        # construct Parameters object
        result = object.__new__(cls)
        result.pars_list = [
            ({**overrides, **kwarg_dict}, test_input, test_output)
            for kwarg_dict, test_input, test_output in pars_list
        ]
        result.has_nonseries_input = has_nonseries_input
        result.nonseries_input_type = nonseries_input_type
        return result

    ###############################
    ####    UTILITY METHODS    ####
    ###############################

    def repeat_with_na(self, input_val, output_val) -> Parameters:
        extension_types = {
            np.dtype(bool): pd.BooleanDtype(),
            np.dtype(np.int8): pd.Int8Dtype(),
            np.dtype(np.int16): pd.Int16Dtype(),
            np.dtype(np.int32): pd.Int32Dtype(),
            np.dtype(np.int64): pd.Int64Dtype(),
            np.dtype(np.uint8): pd.UInt8Dtype(),
            np.dtype(np.uint16): pd.UInt16Dtype(),
            np.dtype(np.uint32): pd.UInt32Dtype(),
            np.dtype(np.uint64): pd.UInt64Dtype()
        }

        not_nullable = lambda s: (
            pd.api.types.is_bool_dtype(s) or
            pd.api.types.is_integer_dtype(s)
        ) and not pd.api.types.is_extension_array_dtype(s)

        def transform(kwarg_dict, test_input, test_output):
            # ensure test_input is a Series
            if not isinstance(test_input, pd.Series):
                raise TypeError(
                    f"test_input must be a pd.Series object, not "
                    f"{type(test_input)}"
                )

            # make test_input nullable if not already
            if not_nullable(test_input):
                test_input = test_input.astype(
                    extension_types[test_input.dtype]
                )

            # make test_output nullable if not already
            if not_nullable(test_output):
                test_output = test_output.astype(
                    extension_types[test_output.dtype]
                )

            # add missing vals
            test_input = pd.Series(
                list(test_input) + [input_val],
                index=list(test_input.index) + [test_input.index[-1] + 1],
                dtype=test_input.dtype
            )
            test_output = pd.Series(
                list(test_output) + [output_val],
                index=list(test_output.index) + [test_output.index[-1] + 1],
                dtype=test_output.dtype
            )

            # return modified record
            return kwarg_dict, test_input, test_output

        # extend with NAs added
        self.pars_list.extend([transform(*rec) for rec in self.pars_list])
        return self

    def apply(self, callable):
        # Parameters.apply(lambda rec: skip(*rec))
        self.pars_list = [callable(rec) for rec in self]
        return self

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __iter__(self):
        return self.pars_list.__iter__()

    def __next__(self):
        return self.pars_list.__next__()

    def __len__(self) -> int:
        return self.pars_list.__len__()

    def __str__(self) -> str:
        fmt_str = ",\n".join(
            "(" + ",\n".join(str(y) for y in x) + ")" for x in self.pars_list
        )
        return f"[{fmt_str}]"

    def __repr__(self) -> str:
        fmt_repr = ",\n".join(
            "(" + ",\n".join(repr(y) for y in x) + ")" for x in self.pars_list
        )
        return f"[{fmt_repr}]"
