"""General-purpose installation framework for Bertrand's host dependencies."""
from __future__ import annotations

import json
import hashlib
import inspect
import re
import shutil
import uuid

from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from types import TracebackType
from typing import (
    Any,
    Callable,
    Iterable,
    Iterator,
    Literal,
    Protocol,
    TypedDict,
    cast,
    overload
)

from .run import (
    LockDir,
    UserInfo,
    atomic_write_text,
    atomic_write_bytes,
    mkdir_private
)

#pylint: disable=broad-except


SANITIZE: re.Pattern[str] = re.compile(r"[^a-zA-Z0-9_.-]")
ATOMIC_UNDO: dict[str, Callable[[Pipeline, dict[str, Any]], None]] = {}
STATE: Path = UserInfo().home / ".local" / "state" / "bertrand"


class HasQualName(Protocol):
    """A type hint for any object with a `__module__` and `__qualname__` attribute."""
    __module__: str
    __qualname__: str


class Atomic(Protocol):
    """A type hint for a reversible operation performed as part of a step in the
    installation/uninstallation registry.  Operations can be registered using the
    `@atomic` class decorator on any class implementing this protocol.

    Methods
    -------
    apply(ctx: Pipeline.InProgress) -> dict[str, Any] | None
        Apply the operation to the host system within an in-progress `Pipeline` step,
        and return an optional payload describing any state needed to undo the
        operation.  Use the `ctx.do()` method to record an operation in the journal.
    undo(ctx: Pipeline, payload: dict[str, Any]) -> None
        Undo the operation on the host system within the given `Pipeline`, using the
        payload returned by the original `apply()` call.
    """
    # pylint: disable=missing-function-docstring
    def apply(self, ctx: Pipeline.InProgress) -> dict[str, Any] | None: ...

    @staticmethod
    def undo(ctx: Pipeline, payload: dict[str, Any]) -> None: ...


def _qualname(x: HasQualName) -> str:
    return f"{x.__module__}.{x.__qualname__}"


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def atomic(t: type[Atomic]) -> type[Atomic]:
    """Register an operator for use given operation kind, so that it can be
    looked up during rollback.

    Parameters
    ----------
    t : type[Atomic]
        The operation class to register.

    Returns
    -------
    type[Atomic]
        The same operation class that was passed in, after registering its undo
        method.

    Raises
    ------
    TypeError
        If an operation with the same class name has already been registered.
    """
    kind = _qualname(t)
    if kind in ATOMIC_UNDO:
        raise TypeError(f"Atomic operation must have a unique class name: {kind}")
    ATOMIC_UNDO[kind] = t.undo
    return t


# TODO: I should implement some level of journal compaction over time, to avoid
# endless growth of the journal file.
# -> always keep the N most recent failed runs for each step, and discard the rest.


# TODO: also, since I can now pass in arguments from the `run()` method, then the door
# is open to converting all CLI commands into pipelines, which is super clean.  I just
# need some notion of an ephemeral step that doesn't get recorded in the journal,
# since some CLI steps are one-offs that don't need to be cached between runs.
# -> Persistent steps can never depend on ephemeral ones, but ephemeral steps can
# depend on persistent ones.  This could be enforced in the decorator.


@dataclass
class Pipeline:
    """A reversible sequence of steps that can be planned and executed in order.

    Attributes
    ----------
    state_dir : Path
        The base directory for storing pipeline state, including the lock, journal,
        and backup files.
    schema : int
        The schema version to expect when loading the journal file.
    user : UserInfo
        The user information (uid/gid/name/home directory) for the user performing the
        installation.
    timeout : int
        The timeout in seconds for acquiring the installation lock.  May also be used
        by operations that need to wait for external resources.
    attempt_id : str
        A UUID for the current installation attempt.
    targets : list[Pipeline.Target]
        The list of registered installation steps in the pipeline, along with their
        dependency information.
    facts : dict[str, Any]
        A scratch space for storing arbitrary facts during installation.  These will
        be loaded from completed steps in previous runs if needed, and are otherwise
        populated idempotently by steps during installation.

    Notes
    -----
    Instances of this class can be used both as function decorators and context
    managers.

    When used as a function decorator, the `__call__()` method will register new
    steps by appending a decorated function to the pipeline's internal list, which will
    then be topologically sorted by their capabilities and executed when the `run()`
    method is invoked.  Decorators cannot be applied while the pipeline context is
    active.

    When used as a context manager, the pipeline will acquire an exclusive lock on a
    user-defined state directory, and load or create a persistent journal to determine
    which steps have already been completed, as well as roll back those that were
    interrupted or failed.  When the `run()` method is invoked, it will begin by
    acquiring this context and executing any incomplete steps with nested `InProgress`
    contexts, which record operations in the journal and complete the step on exit.  If
    the step function raises an error during execution, then the recorded operations
    will be replayed in reverse order to roll the pipeline back to a previous state
    before propagating the error.

    Steps are always ordered according to their `requires` capabilities, with ties
    broken by their registration order.  If a step is added or removed from the
    pipeline or its originating module is changed in any way, then all dependent steps
    will be rolled back and re-executed on the next run.  The `undo()` method also
    allows users to roll back steps by dependency, or all at once, by replaying the
    journal in reverse order.
    """
    class Function(Protocol):
        """A type hint for a function that can be decorated as a pipeline step."""
        __module__: str
        __qualname__: str
        def __call__(self, ctx: Pipeline.InProgress) -> None: ...

    @dataclass(frozen=True)
    class Target:
        """An entry in the installation pipeline, along with its dependency information.

        Attributes
        ----------
        func : Pipeline.Function
            The function to execute for this step, which accepts an in-progress
            context and uses it to record mutating operations in the installation
            journal.  The function's fully-qualified name (dotted path) will be used as
            the step name, and will always be appended to the `provides` list.
        version : int
            A version specifier for the function's enclosing module, to detect changes.
        requires : frozenset[str]
            The unique capability flags required by this step.  Each step will be
            executed in strict topological order based on these dependencies.  If a
            required capability is not provided by any other step in the pipeline, an
            error will be raised before execution.
        provides : frozenset[str]
            The capability flags provided by this step upon successful installation,
            which can satisfy the `requires` of other steps.
        """
        func: Pipeline.Function
        version: str
        requires: frozenset[Pipeline.Target]

        def __str__(self) -> str:
            return _qualname(self.func)

        def __hash__(self) -> int:
            return id(self.func)

        def __eq__(self, other: Any) -> bool:
            if isinstance(other, Pipeline.Target):
                return self.func is other.func
            return NotImplemented

    class OpRecord(TypedDict):
        """Type hint for an operation taken as part of a step."""
        kind: str  # fully-qualified name of the operation class
        payload: dict[str, Any]  # operation-specific payload for undoing the operation

    class StepRecord(TypedDict, total=False):
        """Type hint for an atomic step in the journal."""
        attempt_id: str  # uuid of the installation run that created this step
        name: str  # dotted name of the step function
        version: str  # hash of the step's originating module for change detection
        status: Literal["in_progress", "completed", "failed"]
        started_at: str  # ISO timestamp
        ended_at: str  # ISO timestamp
        requires: list[str]  # dotted names of prerequisite steps
        ops: list[Pipeline.OpRecord]  # ordered operations performed in this step
        args: dict[str, str]  # keyword name and value hash of arguments accessed during this step
        facts: dict[str, Any]  # facts recorded during this step
        backups: list[str]  # backup filenames associated with this step
        error: str  # error message if step failed

    # pipeline-wide config
    state_dir: Path
    schema: int = 1
    user: UserInfo = field(default_factory=UserInfo)
    timeout: int = 30  # seconds
    attempt_id: str = field(default_factory=lambda: uuid.uuid4().hex, repr=False)

    # planning/registration
    targets: list[Pipeline.Target] = field(default_factory=list)
    _ordered: bool = field(default=False, repr=False)  # skip planning
    _lookup: dict[str, Pipeline.Target] = field(default_factory=dict, repr=False)  # name -> target
    _modules: dict[str, str] = field(default_factory=dict, repr=False)  # module path -> hash

    # context
    facts: dict[str, Any] = field(default_factory=dict, repr=False)
    _lock: LockDir = field(init=False, repr=False)
    _data: dict[str, Any] = field(default_factory=dict, repr=False)
    _completed: dict[Pipeline.Target, str] = field(default_factory=dict, repr=False)
    _active: Pipeline.Target | None = field(default=None, repr=False)
    _kwargs: dict[str, tuple[str, Any]] = field(default_factory=dict, repr=False)

    def __post_init__(self) -> None:
        mkdir_private(self.state_dir)
        mkdir_private(self.backup_dir)
        self._lock = LockDir(path=self.state_dir / ".lock", timeout=self.timeout)

    # TODO: if a module hash returns "", then I should always invalidate dependent
    # that step, since I can't be sure it hasn't changed.

    def _module_hash(self, obj: Any) -> str:
        try:
            # find originating source file
            source = inspect.getsourcefile(obj) or inspect.getfile(obj)
            if not source:
                return ""

            # check cache
            cached = self._modules.get(source)
            if cached is not None:
                return cached

            # compute hash of source file
            path = Path(source)
            if not path.exists() or not path.is_file():
                result = ""
            else:
                result = hashlib.sha256(path.read_bytes()).hexdigest()

            # cache result
            self._modules[source] = result
            return result
        except Exception:
            return ""  # best-effort

    # TODO: json serialization for arguments, facts, and payloads is currently too
    # strict.  I should use sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    # and default=str

    def _plan(self) -> None:
        if self._ordered:
            return

        # topological sort
        pending = list(self.targets)
        have: set[Pipeline.Target] = set()
        order: list[Pipeline.Target] = []
        while pending:
            new_pending: list[Pipeline.Target] = []
            for step in pending:
                if step.requires.issubset(have):
                    have.add(step)
                    order.append(step)
                else:
                    new_pending.append(step)

            # no progress made - requirements unsatisfied
            if len(new_pending) == len(pending):
                lines = ["Pipeline stalled; unsatisfied dependencies:"]
                for s in pending:
                    lines.append(f"  - {s}: missing {s.requires - have}")
                raise OSError("\n".join(lines))

            pending = new_pending

        self.targets = order
        self._ordered = True

    def _dump(self) -> None:
        atomic_write_text(self.journal, json.dumps(self._data, indent=2, sort_keys=True) + "\n")

    def _rollback_step(self, step: Pipeline.StepRecord) -> None:
        name = step.get("name")
        step["ended_at"] = _utc_now_iso()
        step["status"] = "failed"

        # TODO: rather than removing facts blindly, I should maybe just rebuild
        # the facts dict from scratch after rollback?  Or maybe I can track the
        # facts that are accessed by a step, similar to what I do for args?

        # remove any facts set by this step
        facts = step.get("facts")
        if isinstance(facts, dict):
            for key in facts:
                self.facts.pop(key, None)

        # remove from completed targets if present
        if isinstance(name, str):
            target = self._lookup.get(name)
            if target is not None:
                self._completed.pop(target, None)

        # undo operations in reverse order (best-effort)
        ok = True
        for op in reversed(step.get("ops", [])):
            undo = ATOMIC_UNDO.get(op["kind"])
            if undo:
                try:
                    undo(self, op["payload"])
                except Exception:
                    ok = False

        # remove backup files associated with this step
        kept: list[str] = []
        for b in step.get("backups", []):
            try:
                p = self.backup_dir / b
                if p.exists() and p.is_file():
                    p.unlink()
            except Exception:
                kept.append(b)  # best-effort

        # keep track of any backups that couldn't be deleted
        step["backups"] = kept
        if not ok:
            error = step.get("error", "")
            if isinstance(error, str) and error:
                step["error"] = f"{error}; partial rollback failure"
            else:
                step["error"] = "partial rollback failure"

    def _rollback_in_progress(self) -> bool:
        changed = False
        for s in reversed(self.records):
            if s.get("status") == "in_progress":
                self._rollback_step(s)
                changed = True
        return changed

    def __enter__(self) -> Pipeline:
        # acquire lock
        self._lock.__enter__()
        if self._lock.depth > 1:
            return self  # re-entrant case

        # load journal state
        changed = False
        if self.journal.exists():
            self._data = json.loads(self.journal.read_text(encoding="utf-8"))
            if self._data.get("schema") != f"pipeline/v{self.schema}":
                raise OSError(
                    f"Registry schema mismatch at {self.journal}. Found schema="
                    f"{self._data.get('schema')} (expected pipeline/v{self.schema})."
                )
        else:  # write new journal
            self._data = {
                "schema": f"pipeline/v{self.schema}",
                "created_at": _utc_now_iso(),
                "uid": self.user.uid,
                "user": self.user.name,
                "steps": [],
            }
            changed = True

        # roll back in-progress steps from previous runs
        if self._rollback_in_progress():
            changed = True

        # write changes to journal if needed
        if changed:
            self._dump()

        # clean up any orphaned backup files
        keep: set[str] = {
            b
            for s in self.records
            for b in (s.get("backups", []) or []) if isinstance(b, str)
        }
        try:
            for p in self.backup_dir.iterdir():
                if p.is_file() and p.name not in keep:
                    try:
                        p.unlink()
                    except OSError:
                        pass
        except OSError:
            pass  # best-effort

        # hydrate in-memory state
        self.facts.clear()
        self._completed.clear()
        for s in self.records:
            if s.get("status") == "completed":
                name = s.get("name")
                version = s.get("version")
                if isinstance(name, str) and isinstance(version, str):
                    target = self._lookup.get(name)
                    if target is not None:
                        self._completed[target] = version

                facts = s.get("facts")
                if isinstance(facts, dict):
                    self.facts.update(facts)

        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._lock.depth > 1:
            self._lock.__exit__(exc_type, exc_value, traceback)
            return  # re-entrant case

        # roll back any in-progress steps
        self._rollback_in_progress()

        # always synchronize journal state on exit
        self._dump()
        self._data.clear()
        self.facts.clear()
        self._completed.clear()
        self._kwargs.clear()

        # release lock
        self._lock.__exit__(exc_type, exc_value, traceback)

    @overload
    def __call__(
        self,
        func: Pipeline.Function,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...
    ) -> Pipeline.Function: ...
    @overload
    def __call__(
        self,
        func: None = ...,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...
    ) -> Callable[[Pipeline.Function], Pipeline.Function]: ...
    def __call__(
        self,
        func: Pipeline.Function | None = None,
        *,
        requires: Iterable[Pipeline.Function] | None = None,
        enable: bool = True
    ) -> Pipeline.Function | Callable[[Pipeline.Function], Pipeline.Function]:
        """Register a step function with the given dependencies.

        Parameters
        ----------
        requires : Iterable[Pipeline.Function] | None, optional
            The prerequisite functions that must be completed before this step can run.
            These should be provided as function objects that have previously been
            decorated with this method.  If None (the default), then all previous steps
            will be required, effectively forcing sequential execution.
        enable : bool, optional
            Whether to enable this step in the pipeline.  If False, the step will not
            be registered, and the pipeline will be unchanged.  Default is True.

        Returns
        -------
        func
            The decorated step function, which will execute the step with the given
            installation context.  The `ctx.do()` and `ctx.backup()` methods should be
            used within the function to record mutating operations in the installation
            journal, so that they can be rolled back if needed.  If an error is raised
            during the function, the installation process will roll back to the
            previous stable state using the journal.

        Raises
        ------
        TypeError
            If any provided capabilities are not unique within the pipeline, or if
            the step name is not unique.
        OSError
            If the pipeline context is currently active.
        """
        if self._lock.depth > 0:
            raise OSError("cannot register pipeline steps while within its context")

        if not enable:  # no-op
            return func if func is not None else lambda func: func

        def _decorator(func: Pipeline.Function) -> Pipeline.Function:
            name = _qualname(func)
            if name in self._lookup:
                raise TypeError(f"Pipeline step function must be unique: '{name}'")

            # gather prerequisites
            r: frozenset[Pipeline.Target]
            if requires is None:
                r = frozenset(self.targets)
            else:
                _r = set()
                for dep in requires:
                    dep_name = _qualname(dep)
                    dep_target = self._lookup.get(dep_name)
                    if not dep_target:
                        raise TypeError(f"Pipeline step dependency not found: '{dep_name}'")
                    _r.add(dep_target)
                r = frozenset(_r)

            # register step
            target = Pipeline.Target(func=func, version=self._module_hash(func), requires=r)
            self.targets.append(target)
            self._lookup[name] = target
            self._ordered = False  # re-plan on next run
            return func

        return _decorator(func) if func is not None else _decorator

    def __len__(self) -> int:
        """The number of functions registered in this pipeline.

        Returns
        -------
        int
            The number of registered functions.
        """
        return len(self.targets)

    def __iter__(self) -> Iterator[Pipeline.Function]:
        """Iterate over the functions registered in this pipeline in topological order.

        Returns
        -------
        Iterator[Pipeline.Function]
            An iterator over the registered functions.
        """
        self._plan()
        for target in self.targets:
            yield target.func

    def __contains__(self, target: Pipeline.Function) -> bool:
        """Check if a function is registered as a target in this pipeline.

        Parameters
        ----------
        target : Pipeline.Function
            The function to check for.

        Returns
        -------
        bool
            True if the function is registered, False otherwise.
        """
        return _qualname(target) in self._lookup

    @property
    def backup_dir(self) -> Path:
        """The directory for storing backup files during installation.

        Returns
        -------
        Path
            The backup directory path.
        """
        return self.state_dir / "backups"

    @property
    def journal(self) -> Path:
        """The path to the installation journal file.

        Returns
        -------
        Path
            The journal file path.
        """
        return self.state_dir / "journal.json"

    @property
    def records(self) -> list[Pipeline.StepRecord]:
        """
        Returns
        -------
        list[Pipeline.StepRecord]
            A list of steps recorded in the journal.
        """
        return cast(list[Pipeline.StepRecord], self._data.setdefault("steps", []))

    @dataclass
    class InProgress:
        """A context manager representing an in-progress installation step."""
        # pylint: disable=protected-access
        pipeline: Pipeline
        target: Pipeline.Target
        record: Pipeline.StepRecord = field(init=False)

        def __enter__(self) -> Pipeline.InProgress:
            if self.pipeline._active is not None:
                raise OSError("nested installation steps are not supported")
            self.record = {
                "attempt_id": self.pipeline.attempt_id,
                "name": str(self.target),
                "version": self.target.version,
                "status": "in_progress",
                "started_at": _utc_now_iso(),
                "requires": sorted(str(p) for p in self.target.requires),
                "ops": [],
                "args": {},
                "facts": {},
                "backups": [],
                "error": "",
            }
            self.pipeline.records.append(self.record)
            self.pipeline._dump()
            self.pipeline._active = self.target
            return self

        def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            traceback: TracebackType | None
        ) -> None:
            if self.pipeline._active != self.target:
                raise OSError("installation step exit does not match active step")
            try:
                if exc_type is None:
                    self.record["status"] = "completed"
                    self.record["ended_at"] = _utc_now_iso()
                    self.pipeline.facts.update(self.record["facts"])
                    self.pipeline._completed[self.target] = self.target.version
                else:
                    self.pipeline._rollback_step(self.record)
                    error = self.record.get("error")
                    if isinstance(error, str) and error:
                        self.record["error"] = f"{error}; {exc_type.__name__}: {exc_value}"
                    else:
                        self.record["error"] = f"{exc_type.__name__}: {exc_value}"
                self.pipeline._dump()
            finally:
                self.pipeline._active = None

        @property
        def user(self) -> str:
            """
            Returns
            -------
            str
                The name of the user performing the installation.
            """
            return self.pipeline.user.name

        @property
        def uid(self) -> int:
            """
            Returns
            -------
            int
                The numeric user ID of the user performing the installation.
            """
            return self.pipeline.user.uid

        @property
        def gid(self) -> int:
            """
            Returns
            -------
            int
                The numeric group ID of the user performing the installation.
            """
            return self.pipeline.user.gid

        @property
        def timeout(self) -> int:
            """
            Returns
            -------
            int
                The timeout in seconds for acquiring the installation lock.
            """
            return self.pipeline.timeout

        @property
        def state_dir(self) -> Path:
            """
            Returns
            -------
            Path
                The base directory for storing installation state.
            """
            return self.pipeline.state_dir

        @property
        def attempt_id(self) -> str:
            """
            Returns
            -------
            str
                The unique identifier for the current installation attempt.
            """
            return self.pipeline.attempt_id

        def __getitem__(self, key: str) -> Any:
            """Look up a fact stored in the pipeline context, preferring arguments
            passed to the pipeline's `run()` method, then the local step context, and
            finally the global context if necessary.

            Parameters
            ----------
            key : str
                The key to look up.  If this corresponds to an argument passed to the
                pipeline's `run()` method, then the argument and its hashed value will
                be recorded in the journal for change detection.

            Returns
            -------
            Any
                The value of the requested fact.

            Raises
            ------
            KeyError
                If the requested fact is not found.
            """
            # kwargs take precedence over facts
            if key in self.pipeline._kwargs:
                arg_hash, value = self.pipeline._kwargs[key]
                self.record.setdefault("args", {})[key] = arg_hash
                return value

            # local facts take precedence over global facts
            f = self.record["facts"]
            if key in f:
                return f[key]

            # fall back to global facts
            return self.pipeline.facts[key]

        def __setitem__(self, key: str, value: Any) -> None:
            """Set a fact in the local step context, which will be written to the
            global context upon step completion.

            Parameters
            ----------
            key : str
                The key to set.
            value : Any
                The value to set for the keyed fact.

            Raises
            ------
            KeyError
                If the fact key conflicts with an argument passed to the pipeline's
                `run()` method or a fact in the global context.
            """
            if key in self.pipeline._kwargs or key in self.pipeline.facts:
                raise KeyError(f"Cannot overwrite '{key}' in installation context")
            json.dumps(value)  # validate serializability
            self.record["facts"][key] = value

        def __delitem__(self, key: str) -> None:
            """Delete a fact from the local step context.  Never modifies the
            arguments passed to the pipeline's `run()` method or the global context.

            Parameters
            ----------
            key : str
                The key to delete.

            Raises
            ------
            KeyError
                If the requested fact is not found in the local step context.
            """
            del self.record["facts"][key]

        def __contains__(self, key: str) -> bool:
            """Check if a fact exists in the local step context or the global context.

            Parameters
            ----------
            key : str
                The fact key to check.

            Returns
            -------
            bool
                True if the fact exists in either context, False otherwise.
            """
            return (
                key in self.pipeline._kwargs or
                key in self.record["facts"] or
                key in self.pipeline.facts
            )

        def get(self, key: str, default: Any = None) -> Any:
            """Look up a fact stored in the installation context, preferring the local
            step context and falling back to the global context.

            Parameters
            ----------
            key : str
                The fact key to look up.
            default : Any, optional
                The default value to return if the fact is not found, by default None.

            Returns
            -------
            Any
                The value of the requested fact, or the default value if not found.
            """
            # kwargs take precedence over facts
            if key in self.pipeline._kwargs:
                arg_hash, value = self.pipeline._kwargs[key]
                self.record.setdefault("args", {})[key] = arg_hash
                return value

            # local facts take precedence over global facts
            f = self.record["facts"]
            if key in f:
                return f[key]

            # fall back to global facts
            return self.pipeline.facts.get(key, default)

        def do(self, op: Atomic) -> None:
            """Apply an operation within this installation step, recording it in the
            journal.

            Parameters
            ----------
            op : Atomic
                The operation to apply.
            """
            payload = op.apply(self)
            if payload is not None:
                json.dumps(payload)  # validate serializability
                self.record.setdefault("ops", []).append({
                    "kind": _qualname(type(op)),
                    "payload": payload
                })
                self.pipeline._dump()

        def backup(self, path: Path) -> str:
            """Write backup data to a uniquely named file in the backup directory.

            Parameters
            ----------
            path : Path
                The path of the file to back up.  The raw bytes of this file will be
                loaded and written to a uniquely named backup file, which can later be
                recovered using `Pipeline.backup()`.

            Returns
            -------
            str
                The filename of the backup file relative to the backup directory.  This
                should be stored in the operation's payload for recovery during
                `undo()`.

            Raises
            ------
            FileNotFoundError
                If the specified file does not exist, or is not a file.
            """
            if not path.exists() or not path.is_file():
                raise FileNotFoundError(f"Cannot back up non-existent file: {path}")
            filename = f"{uuid.uuid4().hex}__{SANITIZE.sub('_', path.name.strip())}"
            atomic_write_bytes(self.pipeline.backup_dir / filename, path.read_bytes())
            self.record.setdefault("backups", []).append(filename)
            self.pipeline._dump()
            return filename

    def backup(self, filename: str) -> bytes | None:
        """Read backup data from a file in the backup directory.

        Parameters
        ----------
        filename : str
            The filename of the backup file relative to the backup directory.

        Returns
        -------
        bytes | None
            The raw bytes read from the backup file, or None if the file does not
            exist.

        Raises
        ------
        OSError
            If the pipeline context is not currently acquired.
        ValueError
            If the filename is not an immediate child of the backup directory.
        """
        if self._lock.depth < 1:
            raise OSError("pipeline context must be acquired before reading backup files")

        p = Path(filename)
        if p.is_absolute() or len(p.parts) != 1:
            raise ValueError(
                "Backup filename must be an immediate child of the backup "
                f"directory: {filename}"
            )
        path = self.backup_dir / filename
        if not path.exists():
            return None
        return path.read_bytes()

    def run(self, **kwargs: Any) -> None:
        """Run the installation pipeline with the given `Pipeline`.

        Parameters
        ----------
        **kwargs : Any
            Keyword arguments that can be accessed by steps during installation via
            the `ctx.arg()` method.  If any argument value's serialized hash changes
            between runs, then any steps that access it or depend on a step that does
            so will be rolled back and re-executed.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies, or if the
            pipeline context is active.
        Exception
            If any step in the installation process fails.  The type of the exception
            will depend on the underlying error.
        """
        if self._lock.depth > 0:
            raise OSError("cannot run a pipeline while within its context")

        # TODO: it may be possible to store the JSON in the `kwargs` dict directly,
        # and then return lists or dicts as tuples or MappingProxies to prevent
        # mutation after hashing.  Or I can just list this in the contract and
        # leave it up to the user (which is generally just me).

        # compute topological order + record kwargs
        self._plan()
        self._kwargs = {
            k: (hashlib.sha256(json.dumps(v, sort_keys=True).encode("utf-8")).hexdigest(), v)
            for k, v in kwargs.items()
        }

        # acquire lock + journal context
        with self:
            invalid: set[Pipeline.Target] = set()

            # look for steps that access changed arguments or whose requirements have changed
            for s in self.records:
                if s.get("status") == "completed":
                    name = s.get("name")
                    target = self._lookup.get(name) if isinstance(name, str) else None
                    if target is not None:
                        args = s.get("args")
                        current_requires = sorted(str(r) for r in target.requires)
                        recorded_requires = s.get("requires")
                        if (
                            not isinstance(args, dict) or
                            not isinstance(recorded_requires, list) or
                            current_requires != recorded_requires or
                            any(
                                arg_name not in self._kwargs or
                                self._kwargs[arg_name][0] != arg_hash
                                for arg_name, arg_hash in args.items()
                            )
                        ):
                            invalid.add(target)

            # extend to cover all changed targets + dependencies
            for t in self.targets:
                # the targets have already have been topologically sorted, so any
                # dependent capabilities can be invalidated by checking earlier targets
                if self._completed.get(t) != t.version or any(r in invalid for r in t.requires):
                    invalid.add(t)

            # roll back any completed steps that are now invalid, or have been removed
            # from the pipeline
            for s in reversed(self.records):
                if s.get("status") == "completed":
                    step_name = s.get("name")
                    if (
                        not isinstance(step_name, str) or
                        step_name not in self._lookup or
                        self._lookup[step_name] in invalid
                    ):
                        self._rollback_step(s)
                        error = s.get("error")
                        if isinstance(error, str) and error:
                            s["error"] = f"{error}; step invalidated due to dependency change"
                        else:
                            s["error"] = "step invalidated due to dependency change"

            # apply incomplete steps in topological order
            for target in self.targets:
                if target in self._completed:
                    continue

                # run step within a new journal entry
                with Pipeline.InProgress(pipeline=self, target=target) as entry:
                    target.func(entry)

    def undo(self, steps: Iterable[Pipeline.Function] | None = None) -> None:
        """Replay the installation journal in reverse order to uninstall changes.

        Parameters
        ----------
        steps : Iterable[Pipeline.Function] | None
            An optional sequence of step functions which should be rolled back, along
            with any steps that depend on them.  If None (the default), all completed
            steps will be rolled back.

        Raises
        ------
        OSError
            If the pipeline context is active.
        KeyError
            If any specified capability is not found in the pipeline.
        """
        if self._lock.depth > 0:
            raise OSError("cannot undo a pipeline while within its context")

        # roll back all completed steps in reverse order
        if steps is None:
            with self:
                for s in reversed(self.records):
                    if s.get("status") == "completed":
                        self._rollback_step(s)
            return

        # convert step functions into dotted names
        invalid: set[str] = set()
        for func in steps:
            func_name = _qualname(func)
            if func_name not in self._lookup:
                raise KeyError(func_name)
            invalid.add(func_name)
        if not invalid:
            return  # nothing to roll back

        # roll back only steps providing or requiring the specified capabilities
        with self:
            # extend invalid steps to include all dependent capabilities
            completed = [s for s in self.records if s.get("status") == "completed"]
            while True:
                n = len(invalid)
                for s in completed:
                    step_name = s.get("name")
                    requires = s.get("requires")
                    if (
                        isinstance(step_name, str) and
                        isinstance(requires, list) and
                        any(r in invalid for r in requires)
                    ):
                        invalid.add(step_name)
                if len(invalid) == n:
                    break  # no changes

            # roll back all steps that provide or require an invalidated capability
            for s in reversed(completed):
                if s.get("name") in invalid:
                    self._rollback_step(s)


##########################
####    OPERATIONS    ####
##########################


@atomic
@dataclass(frozen=True)
class WriteTextFileOp:
    """An operation that writes text to a file on the host system and backs up any
    existing file as needed.

    Attributes
    ----------
    path : Path
        The path of the file to write.
    text : str
        The text content to write to the file.
    mode : int | None
        The file mode (permissions) to set on the written file, or None to leave
        unchanged.
    mkdir_parents : bool
        Whether to create parent directories as needed, by default True.
    encoding : str
        The text encoding to use when writing the file, by default "utf-8".
    newline : str | None
        The newline mode to use when writing the file, by default None (universal).
    """
    # pylint: disable=unused-argument
    path: Path = Path()
    text: str = ""
    mode: int | None = None
    mkdir_parents: bool = True
    encoding: str = "utf-8"
    newline: str | None = None

    # TODO: newline currently isn't being used.  It should either be deleted or
    # implemented properly.

    def apply(self, ctx: Pipeline) -> dict[str, Any] | None:
        """Write the specified text to a file on the host system, backing up any
        existing file as needed, and record the action in the installation registry.

        Parameters
        ----------
        ctx : Pipeline
            The in-flight installation context.

        Returns
        -------
        dict[str, Any] | None
            A payload describing the written file for undo purposes, or None if no
            changes were made.

        Raises
        ------
        OSError
            If the installation context is not properly initialized, or if backing up
            the existing file fails.
        """
        if not hasattr(ctx, "journal"):
            raise OSError("WriteTextFileOp requires ctx.journal to be initialized")

        # ensure parent directories exist
        p = self.path
        if self.mkdir_parents:
            p.parent.mkdir(parents=True, exist_ok=True)

        # compute new content hash
        data = self.text.encode(self.encoding)
        new_hash = hashlib.sha256(data).hexdigest()

        # check existing file and do nothing if contents already match
        existed = p.exists()
        backup: str | None = None
        old_mode: int | None = None
        old_bytes: bytes | None = None
        if existed:
            try:
                old_mode = p.stat().st_mode & 0o777
            except OSError:
                old_mode = None
            try:
                old_bytes = p.read_bytes()
            except OSError:
                old_bytes = None
            if old_bytes is not None:
                # if contents already match, only apply mode changes if needed
                if old_bytes == data:
                    mode_changed = False
                    if self.mode is not None and old_mode is not None and old_mode != self.mode:
                        try:
                            p.chmod(self.mode)
                            mode_changed = True
                        except OSError:
                            pass
                    if mode_changed:
                        return {
                            "path": str(p),
                            "existed": True,
                            "backup": None,
                            "restore_mode": old_mode,
                            "expected_size": len(data),
                            "expected_hash": new_hash,
                            "wrote": False,
                            "mode_changed": True,
                        }
                    return None

                # if contents differ, back up existing mode
                backup = ctx.write_backup(old_bytes, p.name)

        # write new contents
        atomic_write_bytes(p, data)

        # apply mode if requested (best-effort)
        if self.mode is not None:
            try:
                p.chmod(self.mode)
            except OSError:
                pass
        return {
            "path": str(p),
            "existed": existed,
            "backup": backup,  # filename in backup dir
            "restore_mode": old_mode,
            "expected_size": len(data),
            "expected_hash": new_hash,
            "wrote": True,
            "mode_changed": self.mode is not None and (old_mode is None or old_mode != self.mode),
        }

    @staticmethod
    def undo(ctx: Pipeline, payload: dict[str, Any]) -> None:
        """Restore the backed-up file on the host system, or remove the file if it did
        not exist before.

        Parameters
        ----------
        ctx : Docker
            The in-flight Docker installation state.
        payload : dict[str, Any]
            The payload returned by the original `apply()` call.
        """
        p = Path(str(payload.get("path", "")))
        existed = bool(payload.get("existed", False))
        backup = payload.get("backup")
        restore_mode = payload.get("restore_mode")
        expected_size = payload.get("expected_size")
        expected_hash = payload.get("expected_hash")
        mode_changed = bool(payload.get("mode_changed", False))

        # helper to see if the current file still looks like the one we wrote
        def _matches_expected() -> bool:
            if not expected_hash:
                return True  # no reference; be permissive
            try:
                cur = p.read_bytes()
            except OSError:
                return False
            return (
                (expected_size is None or len(cur) == expected_size) and
                hashlib.sha256(cur).hexdigest() == expected_hash
            )

        # if we overwrote an existing file, restore it, but only if safe
        if existed:
            if backup is not None:
                backup_data = ctx.read_backup(backup)
                if backup_data is None or (p.exists() and not _matches_expected()):
                    return  # backup is empty or user has changed original file - don't clobber
                try:
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_bytes(backup_data)
                    if restore_mode is not None:
                        try:
                            p.chmod(int(restore_mode))
                        except OSError:
                            pass
                    # TODO: remove backup file?
                    # -> This requires some level of garbage collection to avoid deleting
                    # backups that may still be needed due to rollback or similar
                except OSError:
                    pass
                return

            #iIf we didn't create a backup but we did change mode only, restore mode
            if mode_changed and restore_mode is not None and p.exists():
                try:
                    p.chmod(int(restore_mode))
                except OSError:
                    pass
            return

        # file did not exist before - delete only if it still matches what we wrote
        if p.exists() and _matches_expected():
            try:
                p.unlink()
            except OSError:
                pass


@atomic
@dataclass(frozen=True)
class RemoveTreeOp:
    """An operation that removes a directory tree from the host system."""
    # pylint: disable=unused-argument
    path: Path = Path()
    ignore_errors: bool = True

    def apply(self, ctx: Pipeline.InProgress) -> dict[str, Any] | None:
        """Remove the specified directory tree from the host system and record the
        action in the installation registry.

        Parameters
        ----------
        ctx : Pipeline.InProgress
            The current installation step.

        Returns
        -------
        dict[str, Any] | None
            A payload describing the removed path for undo purposes, or None if the
            path did not exist.
        """
        p = self.path
        if not p.exists():
            return None
        shutil.rmtree(p, ignore_errors=self.ignore_errors)
        return {"path": str(p)}

    @staticmethod
    def undo(ctx: Pipeline, payload: dict[str, Any]) -> None:
        """Undo the removal of a directory tree on the host system.  This operation is
        irreversible, so this method is intentionally a no-op.

        Parameters
        ----------
        ctx : Pipeline
            The in-flight installation context.
        payload : dict[str, Any]
            The payload returned by the original `apply()` call.
        """
        return
