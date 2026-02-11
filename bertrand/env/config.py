"""Read-only pyproject configuration rooted at a Bertrand environment.

`pyproject.toml` is user-owned and treated as read-only by this module. Reads are
lock-free by design; if Bertrand-managed writes are added later, they should use a
separate write path protected by the environment metadata lock.
"""
from __future__ import annotations

import tomllib

from dataclasses import dataclass, field
from pathlib import Path
from types import TracebackType
from typing import Any

import yaml

# pylint: disable=broad-exception-caught


PYPROJECT_FILE: str = "pyproject.toml"
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
EDITORS: dict[str, tuple[str, ...]] = {
    "vscode": ("code",),
}
AGENTS: dict[str, tuple[str, ...]] = {
    "none": (),
    "claude": ("anthropic.claude-code",),
    "codex": ("openai.chatgpt",),
}
ASSISTS: dict[str, tuple[str, ...]] = {
    "none": (),
    "copilot": ("GitHub.copilot", "GitHub.copilot-chat"),
}

DEFAULT_SHELL: str = "bash"
DEFAULT_EDITOR: str = "vscode"
DEFAULT_AGENT: str = "none"
DEFAULT_ASSIST: str = "none"
if DEFAULT_SHELL not in SHELLS:
    raise RuntimeError(f"default shell is unsupported: {DEFAULT_SHELL}")
if DEFAULT_EDITOR not in EDITORS:
    raise RuntimeError(f"default editor is unsupported: {DEFAULT_EDITOR}")
if DEFAULT_AGENT not in AGENTS:
    raise RuntimeError(f"default agent is unsupported: {DEFAULT_AGENT}")
if DEFAULT_ASSIST not in ASSISTS:
    raise RuntimeError(f"default assist is unsupported: {DEFAULT_ASSIST}")


@dataclass
class Config:
    """Read-only view of `pyproject.toml` configuration for Bertrand's internal
    toolchain.  The configuration is loaded on demand when entering the context
    manager, and cached for the duration of the context.  No writes will be persisted.
    """

    root: Path
    _data: dict[str, Any] | None = field(default=None, init=False, repr=False)

    def __post_init__(self) -> None:
        self.root = self.root.expanduser().resolve()

    @staticmethod
    def _expect_table(name: str, value: Any, *, required: bool = False) -> dict[str, Any]:
        if value is None:
            if required:
                raise OSError(f"missing required table in pyproject.toml: {name}")
            return {}
        if not isinstance(value, dict):
            raise OSError(f"invalid table in pyproject.toml: {name}")
        if not all(isinstance(k, str) for k in value):
            raise OSError(f"invalid key type in pyproject.toml table: {name}")
        return value

    @staticmethod
    def _reject_reserved_keys(section_name: str, section: dict[str, Any]) -> None:
        for key in ("yaml",):
            if key in section:
                raise OSError(
                    f"Reserved key in pyproject.toml field '{section_name}': '{key}'"
                )

    @staticmethod
    def _validate_string(name: str, value: Any) -> str:
        if not isinstance(value, str):
            raise OSError(f"invalid scalar in pyproject.toml field: {name}")
        text = value.strip()
        if not text:
            raise OSError(f"invalid empty string in pyproject.toml field: {name}")
        return text

    @classmethod
    def _validate_string_list(cls, name: str, value: Any) -> list[str]:
        if not isinstance(value, list):
            raise OSError(f"invalid list in pyproject.toml field: {name}")
        return [cls._validate_string(name, item) for item in value]

    def _validate_bertrand(self, tool: dict[str, Any]) -> None:
        bertrand = self._expect_table("tool.bertrand", tool.get("bertrand"), required=True)
        unknown = set(bertrand) - {"shell", "code", "agent", "assist"}
        if unknown:
            raise OSError(f"unknown key(s) in [tool.bertrand]: {', '.join(sorted(unknown))}")

        # parse tool.bertrand.shell
        shell = bertrand.get("shell")
        if shell is None:
            raise OSError("missing required field in pyproject.toml: tool.bertrand.shell")
        shell = self._validate_string("tool.bertrand.shell", shell)
        if shell not in SHELLS:
            raise OSError(
                f"unsupported shell in pyproject.toml [tool.bertrand].shell: {shell}"
            )

        # parse tool.bertrand.code
        code = bertrand.get("code")
        if code is None:
            raise OSError("missing required field in pyproject.toml: tool.bertrand.code")
        code = self._validate_string("tool.bertrand.code", code)
        if code not in EDITORS:
            raise OSError(
                f"unsupported editor in pyproject.toml [tool.bertrand].code: {code}"
            )

        # parse tool.bertrand.agent
        agent = bertrand.get("agent")
        if agent is None:
            raise OSError("missing required field in pyproject.toml: tool.bertrand.agent")
        agent = self._validate_string("tool.bertrand.agent", agent)
        if agent not in AGENTS:
            raise OSError(
                f"unsupported agent in pyproject.toml [tool.bertrand].agent: {agent}"
            )

        # parse tool.bertrand.assist
        assist = bertrand.get("assist")
        if assist is None:
            raise OSError("missing required field in pyproject.toml: tool.bertrand.assist")
        assist = self._validate_string("tool.bertrand.assist", assist)
        if assist not in ASSISTS:
            raise OSError(
                f"unsupported assist in pyproject.toml [tool.bertrand].assist: {assist}"
            )

    def _validate_clang_format(self, tool: dict[str, Any]) -> None:
        name = "tool.clang_format"
        section = self._expect_table(name, tool.get("clang_format"), required=True)
        self._reject_reserved_keys(name, section)

    def _validate_clang_tidy(self, tool: dict[str, Any]) -> None:
        name = "tool.clang_tidy"
        section = self._expect_table(name, tool.get("clang_tidy"), required=True)
        self._reject_reserved_keys(name, section)

    def _validate_clangd(self, tool: dict[str, Any]) -> None:
        name = "tool.clangd"
        section = self._expect_table(name, tool.get("clangd"), required=True)
        self._reject_reserved_keys(name, section)

        # parse tool.clangd.arguments
        arguments = section.get("arguments")
        if arguments is None:
            raise OSError(f"missing required field in pyproject.toml: {name}.arguments")
        self._validate_string_list(f"{name}.arguments", arguments)

    def __enter__(self) -> Config:
        if not self.file.exists():
            raise OSError(f"missing {self.file}")
        if not self.file.is_file():
            raise OSError(f"path is not a file: {self.file}")

        # load raw TOML data
        try:
            with self.file.open("rb") as f:
                data = tomllib.load(f)
        except Exception as err:
            raise OSError(f"failed to parse {self.file}: {err}") from err
        if not isinstance(data, dict):
            raise OSError(f"invalid TOML data in {self.file}")

        # load tools
        tool = self._expect_table("tool", data.get("tool"))
        self._validate_bertrand(tool)
        self._validate_clang_format(tool)
        self._validate_clang_tidy(tool)
        self._validate_clangd(tool)

        self._data = data
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        self._data = None

    @property
    def file(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the pyproject.toml file in this config's root directory.
        """
        return self.root / PYPROJECT_FILE

    def __getitem__(self, key: str | tuple[str, ...]) -> Any:
        if self._data is None:
            raise RuntimeError("config not loaded")
        if isinstance(key, str):
            return self._data[key]
        if isinstance(key, tuple):
            value: Any = self._data
            for part in key:
                if not isinstance(value, dict):
                    raise KeyError(key)
                value = value[part]
            return value
        raise TypeError(f"invalid key type: {type(key)}")

    @staticmethod
    def _render_yaml(name: str, table: dict[str, Any]) -> str:
        try:
            text = yaml.safe_dump(
                table,
                sort_keys=False,
                default_flow_style=False,
            )
        except Exception as err:
            raise OSError(f"failed to render {name}: {err}") from err
        if not isinstance(text, str):
            raise OSError(f"failed to render {name}: serializer returned non-string")
        if not text.endswith("\n"):
            text += "\n"
        return text

    def render_clang_format(self) -> str:
        """Render `.clang-format` body from `[tool.clang_format]`.

        Returns
        -------
        str
            The body of the `.clang-format` file to be generated, based on the
            configuration in `[tool.clang_format]`.
        """
        return self._render_yaml("tool.clang_format", self["tool", "clang_format"])

    def render_clang_tidy(self) -> str:
        """Render `.clang-tidy` body from `[tool.clang_tidy]`.

        Returns
        -------
        str
            The body of the `.clang-tidy` file to be generated, based on the
            configuration in `[tool.clang_tidy]`.
        """
        return self._render_yaml("tool.clang_tidy", self["tool", "clang_tidy"])

    def render_clangd(self) -> str:
        """Render `.clangd` body from `[tool.clangd]`.

        Returns
        -------
        str
            The body of the `.clangd` file to be generated, based on the
            configuration in `[tool.clangd]`.
        """
        return self._render_yaml("tool.clangd", self["tool", "clangd"])
