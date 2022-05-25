# pdtypes
Pandas data types made easy.

## Why Use This Package?
### 1. Versatile
`pdtypes` works on a wide variety of data,
from simple numerics to arbitrary precision
decimals, datetimes, timedeltas, strings,
and categoricals.  The data does not even
have to be properly formatted: it can be of
mixed types (`dtype=object`), outside the
range of 64-bit arithmetic, and/or
contain missing values with no hindrance.

### 2. Reliable
`pdtypes` does not alter data in any way.
If a conversion would lead to information
loss (such as through overflow,
integer/boolean coercion, etc.), an
appropriate error is raised instead.
This ensures that every conversion is
reversible (i.e. int -> timedelta -> int),
without changing the underlying value in
any way.  The only exception to this is
the natural precision loss intrinsic to
floating point numbers, though this is
mitigated wherever possible.

The only way `pdtypes` can change your
data is if you explicitly tell it to.
This allows the user to gain fine control
over the data types they're working with,
without fear of artifacts or bias.

### 3. Simple
Unlike the utility functions found in
`pandas.api.types`, `pdtypes` has a
straightforward and intuitive interface,
consisting of only two functions,
`check_dtypes` and `convert_dtypes`.
Both are self explanatory and work on
arbitrary input, including raw types,
string aliases, numpy dtypes, and
array protocol type strings ("i4", "f8",
"datetime64[ns]", etc).  Multiple types
can be checked at once, following the
iterable pattern set out in the built-in
`isinstance` function.

When used without type arguments,
`check_dtypes` will attempt to infer the
underlying data type just like
`pandas.api.types.infer_dtype` would.
When used with type arguments, it behaves
like the various
`pandas.api.types.is_[blank]_dtype`
functions, with the added benefit of
inferance, which allows fuzzy matching of
malformed input.  When applied to a
dataframe, it returns a schema outlining
the type of each column.

When `convert_dtypes` is used without
type arguments, it attempts to convert
to extension types and infer/repair
malformed objects like
`pandas.Series.convert_dtypes` and
`pandas.Dataframe.convert_dtypes` methods.
When used with type arguments, it functions
Like `pandas.Series.astype`, with checks
to prevent information loss and tune the
conversion.  If the provided type is
unable to hold missing values (such as
`numpy.int64`), then automatic conversion
to the appropriate extension type is
performed.  What's more, any type given to
`convert_dtypes` will reflect the actual
contents of the resulting series, meaning
that
`convert_dtypes(series, "timedelta64[s])"`
will result in a series whose values are
actually stored as NumPy timedelta64
objects, not silently converted to
`pd.Timedelta`.

### 4. Powerful
`pdtypes` is vectorized as much as
possible, taking full advantage of speedy
NumPy operations that reduce runtime and
memory footprints.  In addition,
`convert_dtypes` supports an optional
`downcast` argument, which mimics the
downcast functionality of
`pandas.to_numeric`.  This allows the user
to losslessly shrink the memory footprint
of integer, float, and complex data by up
to a factor of 8, increasing performance
and allowing the user to load larger
datasets.  This is done by manually
inspecting the bit arrays of each value,
with conversion only taking place if no
significant figures are lost in the
process, thus ensuring that data integrity
is left unaffected.  Enabling this allows
for more or less free performance
improvements, at the potential cost of
arithmetic accuracy (multiplying lower
precision floats may lead to rounding
errors, for instance).
