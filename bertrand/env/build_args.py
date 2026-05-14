"""Shared image build-argument normalization and serialization."""

from __future__ import annotations

import json
import math
import re
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Mapping
    from typing import Any

IMAGE_BUILD_ARGS_FILE = Path("/opt/bertrand/build-args.json")
IMAGE_BUILD_ARGS_CONFIG_SETTING = "bertrand.build-args"
_BUILD_ARG_KEY_RE = re.compile(r"^[a-zA-Z](?:[a-zA-Z0-9_]*[a-zA-Z0-9])?$")


def normalize_image_build_args(args: Mapping[str, object]) -> dict[str, str]:
    """Normalize image build arguments into stable strings.

    Parameters
    ----------
    args : Mapping[str, object]
        Raw build arguments from config, CLI parsing, or decoded JSON.

    Returns
    -------
    dict[str, str]
        Build arguments sorted by key with deterministic string values.

    Raises
    ------
    ValueError
        If a key or value is invalid.
    """
    normalized: dict[str, str] = {}
    for key, value in sorted(args.items()):
        key = _normalize_build_arg_key(key)
        if key in normalized:
            msg = f"duplicate image build argument: {key!r}"
            raise ValueError(msg)
        normalized[key] = _normalize_build_arg_value(value)
    return normalized


def encode_image_build_args(args: dict[str, str]) -> str:
    """Encode normalized image build arguments as compact JSON.

    Parameters
    ----------
    args : dict[str, str]
        Normalized image build arguments.

    Returns
    -------
    str
        Compact, deterministic JSON object.
    """
    return json.dumps(
        normalize_image_build_args(args),
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )


def decode_image_build_args(text: str) -> dict[str, str]:
    """Decode normalized image build arguments from compact JSON.

    Parameters
    ----------
    text : str
        JSON object containing image build arguments.

    Returns
    -------
    dict[str, str]
        Normalized image build arguments.

    Raises
    ------
    TypeError
        If the payload is not an object.
    ValueError
        If the payload is not valid JSON or contains invalid build arguments.
    """
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        msg = f"invalid image build argument JSON: {err}"
        raise ValueError(msg) from err
    if not isinstance(payload, dict):
        msg = "image build arguments must be encoded as a JSON object"
        raise TypeError(msg)
    return normalize_image_build_args(payload)


def _normalize_build_arg_key(key: str) -> str:
    key = key.strip()
    if not _BUILD_ARG_KEY_RE.fullmatch(key):
        msg = (
            "image build argument keys must be non-empty snake-case identifiers: "
            f"{key!r}"
        )
        raise ValueError(msg)
    return key


def _normalize_build_arg_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            msg = f"image build argument floats must be finite: {value!r}"
            raise ValueError(msg)
        return str(value)
    if isinstance(value, str):
        return value
    msg = (
        "image build argument values must be strings, booleans, integers, or finite "
        f"floats: {value!r}"
    )
    raise ValueError(msg)
