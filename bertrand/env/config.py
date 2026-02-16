"""Read-only pyproject configuration rooted at a Bertrand environment.

`pyproject.toml` is user-owned and treated as read-only by this module. Reads are
lock-free by design; if Bertrand-managed writes are added later, they should use a
separate write path protected by the environment metadata lock.
"""
from __future__ import annotations

import json
import tomllib

from dataclasses import dataclass, field
from pathlib import Path, PosixPath
from types import TracebackType
from typing import Any

import yaml

from .run import atomic_write_text

# pylint: disable=broad-exception-caught


PYPROJECT_FILE: str = "pyproject.toml"
SHELLS: dict[str, tuple[str, ...]] = {
    "bash": ("bash", "-l"),
}
EDITORS: dict[str, str] = {
    "vscode": "code",
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

CONTAINER_ID_ENV: str = "BERTRAND_CONTAINER_ID"
CONTAINER_BIN_ENV: str = "BERTRAND_CODE_PODMAN_BIN"
EDITOR_BIN_ENV: str = "BERTRAND_CODE_EDITOR_BIN"
HOST_ENV: str = "BERTRAND_HOST_ENV"
MOUNT: PosixPath = PosixPath("/env")
assert MOUNT.is_absolute()

CLANG_FORMAT_FILE: str = ".clang-format"
CLANG_TIDY_FILE: str = ".clang-tidy"
CLANGD_FILE: str = ".clangd"



######################
####    CONFIG    ####
######################


# TODO: config should really use another pydantic model to handle validation if
# possible.


# TODO: maybe I need different layouts per language/tool, and then join them all
# into a single layout for the whole environment?  Then,
# `bertrand init --lang=python --lang=cpp --lsp=python=ty --lsp=cpp=clangd --layout=src`,
# would generate a `src/` layout with all the right config paths.


@dataclass
class Config:
    """Read-only view of `pyproject.toml` configuration for Bertrand's internal
    toolchain.  The configuration is loaded on demand when entering the context
    manager, and cached for the duration of the context.  No writes will be persisted.
    """
    root: Path = field()
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
                f"unsupported editor in pyproject.toml [tool.bertrand].code: {code} "
                f"(supported editors: {', '.join(sorted(EDITORS))})"
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
        name = "tool.clang-format"
        section = self._expect_table(name, tool.get("clang-format"), required=True)
        self._reject_reserved_keys(name, section)

    def _validate_clang_tidy(self, tool: dict[str, Any]) -> None:
        name = "tool.clang-tidy"
        section = self._expect_table(name, tool.get("clang-tidy"), required=True)
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
        return (
            f"# This file is auto-generated by bertrand from [{name}] in\n"
            "# pyproject.toml. Edit pyproject.toml, not this file.\n\n"
            f"{text}"
        )

    def sync(self) -> None:
        """Generate .clang-format, .clang-tidy, and .clangd configuration files from
        the pyproject.toml configuration.

        Raises
        ------
        OSError
            If there is an error reading or writing the generated configuration files, or
            if the existing configuration files are not regular files.

        Notes
        -----
        This is called before every command that touches LLVM tooling, to ensure that
        they always see the configuration specified in pyproject.toml.  Users should never
        edit these files directly, and should centralize all configuration in
        pyproject.toml instead.
        """
        outputs = {
            MOUNT / CLANG_FORMAT_FILE: self._render_yaml(
                "tool.clang-format",
                self["tool", "clang-format"]
            ),
            MOUNT / CLANG_TIDY_FILE: self._render_yaml(
                "tool.clang-tidy",
                self["tool", "clang-tidy"]
            ),
            MOUNT / CLANGD_FILE: self._render_yaml(
                "tool.clangd",
                self["tool", "clangd"]
            ),
        }
        for path, text in outputs.items():
            if path.exists() and not path.is_file():
                raise OSError(f"cannot write generated config (not a file): {path}")
            if path.exists():
                try:
                    current = path.read_text(encoding="utf-8")
                except OSError as err:
                    raise OSError(f"failed to read generated config: {path}: {err}") from err
                if current == text:
                    continue
            atomic_write_text(path, text, encoding="utf-8")


def compile_commands_sources() -> list[Path]:
    """Extract the unique source files from a container's compile_commands.json
    database, which is used to determine the translation units that LLVM tools like
    `clang-format` and `clang-tidy` should operate on.

    Returns
    -------
    list[Path]
        A list of unique source file paths extracted from the compile_commands.json
        file.

    Raises
    ------
    OSError
        If there is an error reading or parsing the compile_commands.json file, or if
        the file is not a regular file.

    Notes
    -----
    compile_commands.json represents a snapshot of the last build of the workspace,
    and is not guaranteed to be up-to-date or complete.  If it is missing or empty,
    then LLVM tools will not operate on any C/C++ files, and the project will be
    treated as pure Python.
    """
    compile_commands = MOUNT / "compile_commands.json"
    if not compile_commands.exists():
        atomic_write_text(compile_commands, "[]\n", encoding="utf-8")
    if not compile_commands.is_file():
        raise OSError(f"compile_commands.json path is not a file: {compile_commands}")

    # load compile_commands.json
    try:
        entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    except Exception as err:
        raise OSError(f"failed to parse compile_commands.json: {err}") from err
    if not isinstance(entries, list):
        raise OSError("compile_commands.json must be a JSON array")

    # extract deduplicated translation units
    files: list[Path] = []
    seen: set[Path] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        source = entry.get("file")
        if not isinstance(source, str) or not source:
            continue
        directory = entry.get("directory")
        src = Path(source)
        if not src.is_absolute() and isinstance(directory, str) and directory:
            src = Path(directory) / src
        src = src.resolve()
        if not src.exists() or src in seen:
            continue
        seen.add(src)
        files.append(src)

    return files
