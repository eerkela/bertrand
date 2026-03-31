"""TODO"""
from __future__ import annotations

import json
import os
import re
from pathlib import PosixPath
from typing import Any, Annotated, Literal

from .core import (
    Config,
    NoCRLF,
    NonEmpty,
    NoWhiteSpace,
    Resource,
    URL,
    resource
)
from ..run import CONTAINER_TMP_MOUNT, Scalar, atomic_write_text, run
from ..version import VERSION

from conan.api.model.list import ListPattern, VersionRange
from conan.api.model.refs import RecipeReference
from conan.errors import ConanException
from conan.internal.model.conf import ConfDefinition
from pydantic import AfterValidator, BaseModel, ConfigDict, Field, StringConstraints


CONAN_REF_TOKEN_RE = re.compile(r"^[a-z0-9_][a-z0-9_+.-]{1,100}\Z")
CONAN_CACHE: PosixPath = PosixPath("/opt/conan")
CONAN_HOME: PosixPath = PosixPath("/opt/conan")


def _check_conan_requirement(requirement: str) -> str:
    # Use conan's own parser to validate the requirement string and extract/verify its
    # components
    try:
        ref = RecipeReference.loads(requirement)
    except ConanException as err:
        raise ValueError(
            f"invalid conan requirement '{requirement}' "
            "(expected name/version[@user/channel][#revision])"
        ) from err
    if ref.timestamp is not None:
        raise ValueError(
            f"invalid conan requirement '{requirement}' (timestamp suffixes are not "
            "supported)"
        )
    if not CONAN_REF_TOKEN_RE.fullmatch(ref.name):
        raise ValueError(
            f"invalid conan package name '{ref.name}' in requirement '{requirement}'"
        )
    if ref.user and not CONAN_REF_TOKEN_RE.fullmatch(ref.user):
        raise ValueError(
            f"invalid conan package user '{ref.user}' in requirement '{requirement}'"
        )
    if ref.channel and not CONAN_REF_TOKEN_RE.fullmatch(ref.channel):
        raise ValueError(
            f"invalid conan package channel '{ref.channel}' in requirement '{requirement}'"
        )

    # validate version ranges
    version = repr(ref.version)
    if version.startswith("(") and version.endswith(")"):
        raise ValueError(
            f"invalid conan requirement '{requirement}' (alias references are not "
            "supported)"
        )
    if version.startswith("[") and version.endswith("]"):
        expression = version[1:-1].strip()
        if not expression:
            raise ValueError(
                f"invalid conan requirement '{requirement}' (empty version range)"
            )
        try:
            VersionRange(expression)
        except ConanException as err:
            raise ValueError(
                f"invalid conan version range in requirement '{requirement}'"
            ) from err
    else:
        try:
            ref.validate_ref()
        except ConanException as err:
            raise ValueError(f"invalid conan requirement '{requirement}'") from err

    # return normalized requirement string and strip any timestamp suffix
    return ref.repr_notime()


def _check_conan_conf(
    conf: dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]]
) -> None:
    for namespace, values in conf.items():
        for key, value in values.items():
            entry = f"{namespace}:{key}"
            conf_def = ConfDefinition()
            try:
                conf_def.update(entry, value, profile=True)
            except ConanException as err:
                raise ValueError(
                    f"invalid conan conf entry '{entry}' with value {value!r}"
                ) from err


def _check_conan_allowed_pattern(pattern: str) -> str:
    if pattern.startswith(("!", "~")) or pattern == "&":
        raise ValueError(
            f"invalid conan allowed-packages pattern '{pattern}' (negation/consumer "
            "patterns are not supported)"
        )

    # Use conan's own parser to validate the pattern string and extract/verify its
    # components.  We require at least a name and version component
    try:
        parsed = ListPattern(pattern, only_recipe=True)
    except ConanException as err:
        raise ValueError(f"invalid conan allowed-packages pattern '{pattern}'") from err
    if not parsed.name or not parsed.version:
        raise ValueError(
            f"invalid conan allowed-packages pattern '{pattern}' (expected a recipe "
            "pattern with name/version)"
        )
    return pattern


type ConanRequirement = Annotated[
    NonEmpty[NoCRLF],
    AfterValidator(_check_conan_requirement)
]
type ConanOptionName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z_][A-Za-z0-9_]*$"
)]
type ConanConfNamespace = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^:\s]+$"
)]
type ConanConfName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[^:\s]+$"
)]
type ConanConf = Annotated[
    dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]],
    AfterValidator(_check_conan_conf)
]
type ConanConfValue = Scalar | list[ConanConfValue] | dict[str, ConanConfValue]
type ConanRemoteName = Annotated[str, StringConstraints(
    strip_whitespace=True,
    min_length=1,
    pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]*$"
)]
type ConanAllowedPattern = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_conan_allowed_pattern)
]
type ConanOptionPattern = Annotated[
    NonEmpty[NoWhiteSpace],
    AfterValidator(_check_conan_allowed_pattern)
]
type ConanOptions = dict[ConanOptionPattern, dict[ConanOptionName, Scalar]]


@resource("conan")
class ConanConfig(Resource):
    """A resource that validates Conan configuration data sourced from project config
    files (e.g. `[tool.conan]` in `pyproject.toml`) and renders derived `conanfile.py`,
    `remotes.json`, and default profile artifacts.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    GENERATORS: tuple[str, ...] = (
        "CMakeDeps",
        "CMakeToolchain",
        "VirtualBuildEnv",
        "VirtualRunEnv",
    )

    ARCH_MAP: dict[str, str] = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "aarch64": "armv8",
        "arm64": "armv8",
    }

    class Model(BaseModel):
        """Validate the global `[tool.conan]` table."""
        model_config = ConfigDict(extra="forbid")
        build_type: Annotated[Literal["Release", "Debug"], Field(
            default="Release",
            alias="build-type",
            examples=["Release", "Debug"],
            description=
                "Global default build type for Conan builds, which can be overridden "
                "by individual tags.",
        )]
        conf: Annotated[ConanConf, Field(
            default_factory=dict,
            description=
                "Global mapping of namespace tables to conf entries, which get "
                "converted into '<namespace>:<name>=<value>' format in the generated "
                "Conan profile.  Individual tags can specify additional entries that "
                "merge with these global defaults.",
        )]
        options: Annotated[ConanOptions, Field(
            default_factory=dict,
            description=
                "Global mapping of namespace tables to package options, which get "
                "converted into '<package-pattern>:<option>=<value>' format in the "
                "generated Conan profile.  Individual tags can specify additional "
                "options that merge with these global defaults.",
        )]

        class Require(BaseModel):
            """Validate entries in `[[tool.conan.requires]]`."""
            model_config = ConfigDict(extra="forbid")
            package: Annotated[ConanRequirement, Field(
                description="A Conan package specifier."
            )]
            kind: Annotated[Literal["host", "tool"], Field(
                default="host",
                examples=["host", "tool"],
                description=
                    "The kind of requirement, which controls how the requirement gets "
                    "injected into the generated Conan profile.  'tool' requirements "
                    "apply only to the build context, whereas 'host' requirements "
                    "specify runtime dependencies."
            )]
            options: Annotated[dict[ConanOptionName, Scalar], Field(
                default_factory=dict,
                description=
                    "Mapping of option names to their values for this requirement, "
                    "which get converted into '<package>:<option>=<value>' format in "
                    "the generated Conan profile.  These options merge with any global "
                    "options specified in the `options` field, and take precedence "
                    "over any conflicting values."
            )]

        class Remote(BaseModel):
            """Validate entries in `[[tool.conan.remotes]]`."""
            @staticmethod
            def _check_allowed_packages(
                value: list[ConanAllowedPattern]
            ) -> list[ConanAllowedPattern]:
                seen: set[ConanAllowedPattern] = set()
                for pattern in value:
                    if pattern in seen:
                        raise ValueError(
                            f"duplicate conan allowed-packages pattern: '{pattern}'"
                        )
                    seen.add(pattern)
                return value

            model_config = ConfigDict(extra="forbid")
            name: Annotated[ConanRemoteName, Field(
                description="Unique name for this Conan remote.",
            )]
            url: Annotated[URL, Field(
                description="Public URL for this Conan remote.",
            )]
            verify_ssl: Annotated[bool, Field(
                default=True,
                alias="verify-ssl",
                description=
                    "Whether to verify SSL certificates when communicating with this "
                    "remote.  Should generally be left enabled unless the remote is "
                    "known to have an invalid certificate, or is only accessible over "
                    "insecure HTTP.",
            )]
            enabled: Annotated[bool, Field(
                default=True,
                description=
                    "Whether to include this remote in Conan operations.  This can be "
                    "used to temporarily disable a remote without removing it from "
                    "config.",
            )]
            recipes_only: Annotated[bool, Field(
                default=False,
                alias="recipes-only",
                description=
                    "If true, only recipes will be loaded from this remote, and no "
                    "binaries will be downloaded.",
            )]
            allowed_packages: Annotated[
                list[ConanAllowedPattern],
                AfterValidator(_check_allowed_packages),
                Field(
                    default_factory=list,
                    alias="allowed-packages",
                    description=
                        "List of recipes that are allowed to be downloaded from this "
                        "remote. If the list is empty or not present, all packages "
                        "are allowed. Uses fnmatch rules.",
                )
            ]

        @staticmethod
        def _check_requires(value: list[Require]) -> list[Require]:
            seen: set[tuple[str, str]] = set()
            for req in value:
                identity = (req.kind, req.package)
                if identity in seen:
                    raise ValueError(
                        f"duplicate conan requirement identity for kind='{req.kind}', "
                        f"package='{req.package}'"
                    )
                seen.add(identity)
            return value

        @staticmethod
        def _check_remotes(value: list[Remote]) -> list[Remote]:
            seen: set[ConanRemoteName] = set()
            for remote in value:
                if remote.name in seen:
                    raise ValueError(
                        f"duplicate conan remote name in [tool.conan]: '{remote.name}'"
                    )
                seen.add(remote.name)
            return value

        requires: Annotated[list[Require], AfterValidator(_check_requires), Field(
            default_factory=list,
            description=
                "Global list of Conan dependencies to install for the project, which "
                "can be extended for individual tags.",
        )]
        remotes: Annotated[list[Remote], AfterValidator(_check_remotes), Field(
            default_factory=list,
            description=
                "List of Conan remotes to use when resolving Conan dependencies for "
                "this project.  NOTE: Conan remotes are resolved in declaration "
                "order.  Prefer private remotes first, then optional public fallback "
                "remotes last.  Credentials are host-local and must never be stored "
                "here; resolve through env/secret channels (e.g. "
                "CONAN_LOGIN_USERNAME_<REMOTE>, CONAN_PASSWORD_<REMOTE>, with remote "
                "names normalized to SCREAMING_SNAKE_CASE)",
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    @staticmethod
    def _merge_options(out: dict[str, Scalar], options: ConanOptions) -> None:
        for pattern, pattern_options in sorted(options.items(), key=lambda i: i[0]):
            for option, value in sorted(pattern_options.items()):
                out[f"{pattern}:{option}"] = value

    async def _render_conanfile(self, config: Config, tag: str) -> None:
        from .bertrand import Bertrand
        from .python import PyProject
        python = config.get(PyProject)
        bertrand = config.get(Bertrand)
        conan = config.get(ConanConfig)
        assert conan is not None

        # start with global requirements, then merge tag-specific additions if
        # applicable
        active = None
        requires = list(conan.requires)
        if bertrand is not None:
            active = next((t for t in bertrand.tags if t.tag == tag), None)
            if active is not None:
                requires.extend(active.conan.requires)

        # check merged requirement identities and sort into host/tool requirements
        _requires: list[ConanConfig.Model.Require] = []
        tool_requires: list[ConanConfig.Model.Require] = []
        seen: set[tuple[str, str]] = set()
        for req in requires:
            identity = (req.kind, req.package)
            if identity in seen:
                raise OSError(
                    f"duplicate effective conan requirement identity for tag '{tag}': "
                    f"kind='{req.kind}', package='{req.package}'"
                )
            if req.kind == "host":
                _requires.append(req)
            elif req.kind == "tool":
                tool_requires.append(req)
            seen.add(identity)
        requires = sorted(_requires, key=lambda r: r.package)
        tool_requires.sort(key=lambda r: r.package)

        # merge global + tag-level Conan options mapping for global/tag tables, then
        # merge any per-require options
        default_options: dict[str, Scalar] = {}
        self._merge_options(default_options, conan.options)
        if active is not None:
            self._merge_options(default_options, active.conan.options)
        for req in requires + tool_requires:
            try:
                package = RecipeReference.loads(req.package).name
            except ConanException:
                package = req.package.split("/", maxsplit=1)[0]
            for option, value in sorted(req.options.items()):
                key = f"{package}/*:{option}"
                inserted = default_options.setdefault(key, value)
                if inserted != value:
                    raise OSError(
                        f"conflicting conan option values for '{key}' across "
                        f"requirements: {inserted!r} vs {value!r}"
                    )

        # render lines for Conanfile.py
        lines: list[str] = [
            "from conan import ConanFile",
            "",
            "class BertrandConanFile(ConanFile):",
        ]
        if python is not None:
            project = python.project
            lines.append(f"    name = {project.name!r}")
            lines.append(f"    version = {project.version!r}")
            if project.license is not None:
                lines.append(f"    license = {project.license!r}")
            if project.description is not None:
                lines.append(f"    description = {project.description!r}")
            url = next((str(project.urls[key]) for key in (
                "homepage",
                "repository",
                "documentation"
            ) if key in project.urls), None)
            if url is not None:
                lines.append(f"    url = {url!r}")
            topics: list[str] = []
            seen_topics: set[str] = set()
            for keyword in project.keywords:
                value = keyword.strip()
                if not value or value in seen_topics:
                    continue
                seen_topics.add(value)
                topics.append(value)
            if topics:
                lines.append(f"    topics = {tuple(topics)!r}")
        lines.append("    settings = \"os\", \"arch\", \"compiler\", \"build_type\"")
        lines.append(f"    generators = {self.GENERATORS!r}")
        lines.append(f"    requires = {tuple(req.package for req in requires)!r}")
        lines.append(f"    tool_requires = {tuple(req.package for req in tool_requires)!r}")
        if default_options:
            lines.append("    default_options = {")
            for key, value in default_options.items():
                lines.append(f"        {key!r}: {value!r},")
            lines.append("    }")
        lines.append("")

        atomic_write_text(
            CONTAINER_TMP_MOUNT / "conanfile.py",
            "\n".join(lines),
            encoding="utf-8"
        )

    @staticmethod
    def _merge_conf(
        base: ConanConf,
        override: ConanConf,
    ) -> dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]]:
        merged: dict[ConanConfNamespace, dict[ConanConfName, ConanConfValue]] = {
            namespace: values.copy() for namespace, values in base.items()
        }
        for namespace, values in override.items():
            merged.setdefault(namespace, {}).update(values)
        return merged

    def _arch(self) -> str:
        raw_arch = os.uname().machine.strip().lower()
        arch = self.ARCH_MAP.get(raw_arch)
        if arch is None:
            raise OSError(
                f"unsupported Conan architecture for current runtime machine: '{raw_arch}'"
            )
        return arch

    @staticmethod
    async def _clang_major() -> str:
        result = await run(["/opt/llvm/bin/clang", "-dumpversion"], capture_output=True)
        version = result.stdout.strip()
        major = version.split(".", maxsplit=1)[0]
        if not major or not major.isdigit():
            raise OSError(
                "failed to parse clang major version during conan profile generation: "
                f"{version!r}"
            )
        return major

    @staticmethod
    def _cppstd() -> str:
        value = "" if VERSION.cxx_std is None else str(VERSION.cxx_std).strip()
        if not value or not value.isdigit():
            raise OSError(
                "invalid VERSION.cxx_std during conan profile generation: "
                f"{VERSION.cxx_std!r}"
            )
        return value

    async def _render_conanprofile(self, config: Config, tag: str) -> None:
        from .bertrand import Bertrand
        bertrand = config.get(Bertrand)
        conan = config.get(ConanConfig)
        assert conan is not None

        # merge global and tag-specific build_type + conf settings
        build_type = conan.build_type
        conf = self._merge_conf(
            {
                "tools.cmake.cmaketoolchain": {"generator": "Ninja"},
                "tools.build": {
                    "compiler_executables": {
                        "c": "/opt/llvm/bin/clang",
                        "cpp": "/opt/llvm/bin/clang++",
                    }
                },
            },
            conan.conf,
        )
        if bertrand is not None:
            active = next((t for t in bertrand.tags if t.tag == tag), None)
            if active is not None:
                if active.conan.build_type:
                    build_type = active.conan.build_type
                conf = self._merge_conf(conf, active.conan.conf)
        conf_def = ConfDefinition()
        for namespace in sorted(conf):
            for key, value in sorted(conf[namespace].items()):
                conf_def.update(f"{namespace}:{key}", value, profile=True)
        conf_text = conf_def.dumps()

        # render lines
        clang_major = await self._clang_major()
        arch = self._arch()
        cppstd = self._cppstd()
        lines = [
            "[settings]",
            "os=Linux",
            f"arch={arch}",
            "compiler=clang",
            f"compiler.version={clang_major}",
            "compiler.libcxx=libc++",
            f"compiler.cppstd={cppstd}",
            f"build_type={build_type}",
            "",
            "[conf]",
        ]
        if conf_text:
            lines.extend(conf_text.splitlines())
        lines.extend([
            "",
            "[buildenv]",
            "CC=ccache /opt/llvm/bin/clang",
            "CXX=ccache /opt/llvm/bin/clang++",
            "",
        ])

        atomic_write_text(
            CONAN_HOME / "profiles" / "default",
            "\n".join(lines),
            encoding="utf-8"
        )

    async def _render_conanremotes(self, config: Config, tag: str) -> None:
        conan = config.get(ConanConfig)
        assert conan is not None

        payload: dict[str, list[dict[str, Any]]] = {"remotes": []}
        for remote in conan.remotes:
            entry: dict[str, Any] = {
                "name": remote.name,
                "url": str(remote.url),
                "verify_ssl": remote.verify_ssl,
                "disabled": not remote.enabled,
                "recipes_only": remote.recipes_only,
            }
            if remote.allowed_packages:
                entry["allowed_packages"] = list(remote.allowed_packages)
            payload["remotes"].append(entry)

        try:
            text = json.dumps(payload, indent=4, ensure_ascii=False)
        except (TypeError, ValueError) as err:
            raise OSError(
                f"failed to serialize JSON payload for resource '{self.name}': {err}"
            ) from err
        if not text.endswith("\n"):
            text += "\n"

        atomic_write_text(
            CONAN_HOME / "remotes.json",
            text,
            encoding="utf-8"
        )

    async def render(self, config: Config, tag: str | None) -> None:
        if tag is None or config.get(ConanConfig) is None:
            return
        await self._render_conanfile(config, tag)
        await self._render_conanprofile(config, tag)
        await self._render_conanremotes(config, tag)
