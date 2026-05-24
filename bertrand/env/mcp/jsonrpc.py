"""Shared JSON-RPC framing helpers for MCP-adjacent subprocess protocols."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING, Any, cast

if TYPE_CHECKING:
    import asyncio

type JSON = dict[str, Any]
type RequestID = int | str


class JsonRpcError(RuntimeError):
    """Raised when a JSON-RPC message cannot be framed or decoded."""


def params(payload: JSON | None) -> JSON:
    """Return a JSON-RPC request fragment containing params when present.

    Parameters
    ----------
    payload : JSON | None
        Optional request params object.

    Returns
    -------
    JSON
        Empty mapping when no params are present, otherwise a ``params`` fragment.
    """
    return {} if payload is None else {"params": payload}


def encode_content_length_message(payload: JSON) -> bytes:
    """Encode one JSON-RPC message using Content-Length framing.

    Parameters
    ----------
    payload : JSON
        JSON-RPC object to encode.

    Returns
    -------
    bytes
        Header-framed UTF-8 JSON bytes.
    """
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    header = b"Content-Length: " + str(len(data)).encode("ascii") + b"\r\n\r\n"
    return header + data


async def read_content_length_message(stdout: asyncio.StreamReader) -> JSON:
    """Read one Content-Length framed JSON-RPC object from a stream.

    Parameters
    ----------
    stdout : asyncio.StreamReader
        Stream containing header-framed JSON-RPC messages.

    Returns
    -------
    JSON
        Decoded JSON-RPC object.

    Raises
    ------
    JsonRpcError
        If the message is missing headers or contains invalid JSON.
    """
    headers: dict[str, str] = {}
    while True:
        line = await stdout.readline()
        if not line:
            msg = "JSON-RPC peer closed stdout"
            raise JsonRpcError(msg)
        stripped = line.decode("ascii", errors="replace").strip()
        if not stripped:
            break
        key, sep, value = stripped.partition(":")
        if sep:
            headers[key.lower()] = value.strip()

    raw_length = headers.get("content-length")
    if raw_length is None:
        msg = "JSON-RPC message is missing Content-Length header"
        raise JsonRpcError(msg)
    try:
        length = int(raw_length)
    except ValueError as err:
        msg = f"invalid Content-Length from JSON-RPC peer: {raw_length!r}"
        raise JsonRpcError(msg) from err
    payload = await stdout.readexactly(length)
    try:
        decoded = json.loads(payload.decode("utf-8"))
    except json.JSONDecodeError as err:
        msg = f"invalid JSON-RPC payload: {err}"
        raise JsonRpcError(msg) from err
    if not isinstance(decoded, dict):
        msg = "JSON-RPC payload must be an object"
        raise JsonRpcError(msg)
    return cast("JSON", decoded)


def format_error(error: Any) -> str:
    """Format a JSON-RPC error response for humans.

    Parameters
    ----------
    error : Any
        Raw JSON-RPC error payload.

    Returns
    -------
    str
        Human-readable error summary.
    """
    if not isinstance(error, dict):
        return str(error)
    message = error.get("message")
    code = error.get("code")
    if isinstance(message, str):
        return f"JSON-RPC error {code}: {message}"
    return f"JSON-RPC error {error}"
