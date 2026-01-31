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
from types import MappingProxyType, TracebackType
from typing import (
    Any,
    Callable,
    Iterable,
    Iterator,
    Literal,
    Protocol,
    TypedDict,
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
MISSING: str = "<missing>"  # not a valid SHA-256 hexdigest


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


@dataclass
class Pipeline:
    """A reversible sequence of steps that can be planned and executed in order.

    Attributes
    ----------
    state_dir : Path
        The base directory for storing pipeline state, including the lock, journal,
        and backup files.
    schema : int, optional
        The schema version to expect when loading the journal file.  Defaults to the
        highest version supported by this codebase.
    timeout : int, optional
        The timeout in seconds for acquiring the installation lock.  May also be used
        by operations that need to wait for external resources.  Defaults to 30
        seconds.
    keep : int, optional
        The number of previous attempts to keep in the journal for each step.  Defaults
        to 3.

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
            journal.  The function's fully-qualified (dotted) name will be used as the
            step name, and must be unique within the pipeline.
        version : int
            A current version specifier for the function's enclosing module, which
            will be cross-checked against the journal to detect changes.
        cache : bool
            Whether to cache the results of this step in the journal.  If False, the
            step will always be re-executed on each run, regardless of its previous
            state or dependencies.  Cached steps must never depend on uncached steps.
        requires : frozenset[Pipeline.Target]
            The set of previous targets which must be completed before this step can
            run.  Each step will be executed in strict topological order based on these
            dependencies.  If a requirement is not present in the pipeline, then an
            error will be raised during registration.
        """
        func: Pipeline.Function
        version: str
        cache: bool
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
        """JSON representation for an atomic operation taken as part of a step.

        Attributes
        ----------
        kind : str
            The fully-qualified (dotted) name of the operation class.
        payload : dict[str, Any]
            The operation-specific payload needed to undo the operation.  Must be
            JSON-serializable.
        """
        kind: str
        payload: dict[str, Any]

    class StepRecord(TypedDict):
        """JSON representation for a full step in the journal.

        Attributes
        ----------
        attempt_id : str
            The UUID of the `run()` that created this step.
        name : str
            The fully-qualified (dotted) name of the step function.  Guaranteed to be
            unique within the pipeline.
        status : Literal["in_progress", "completed", "failed"]
            The current status of the step.
        started_at : str
            The ISO timestamp when the step began execution.
        ended_at : str
            The ISO timestamp when the step completed execution or failed.
        version : str
            The version specifier (hash) of the step's originating module for basic
            change detection.  Note that this will not cover calls to functions
            imported from other modules, so steps should either be self-contained
            within a module or be manually invalidated when their dependencies change.
            This is a best-effort mechanism, not a guarantee.
        accesses : dict[str, str]
            A mapping that tracks the arguments and global facts that were accessed
            during this step, where each key is an argument/fact name and each value is
            a hash of its JSON-serialized value.  This is used for change detection in
            addition to `version`; if any argument or fact is removed or changes value
            between runs, then all steps that access it will be invalidated.
        requires : list[str]
            The list of dotted names of prerequisite steps that must be completed
            before this step can run.  If any of these steps are invalidated, or
            require an invalidated step in turn (recursively), then this step will also
            be invalidated.
        facts : dict[str, Any]
            A mapping of the facts that were generated during this step.  The keys will
            never conflict with `accesses`, and the values must be JSON-serializable.
        ops : list[Pipeline.OpRecord]
            An ordered list of atomic operations that were performed during this step,
            which will be replayed in reverse order if the step needs to be rolled
            back.
        backups : list[str]
            A list of backup filenames that were generated during this step, which will
            be deleted after the step is rolled back.
        error : str
            An informative error message if the step failed.
        """
        attempt_id: str
        name: str
        status: Literal["in_progress", "completed", "failed"]
        started_at: str
        ended_at: str
        version: str
        accesses: dict[str, str]
        requires: list[str]
        facts: dict[str, Any]
        ops: list[Pipeline.OpRecord]
        backups: list[str]
        error: str

    class Journal(TypedDict):
        """JSON representation for the installation journal header.

        Attributes
        ----------
        schema : str
            The schema version string (e.g. "pipeline/v1").  This is used to verify
            compatibility when loading the journal.
        created_at : str
            The ISO timestamp when the journal was created.
        steps : list[Pipeline.StepRecord]
            The list of recorded steps in the journal.  In order to prevent endless
            growth, only the last `pipeline.keep` attempts will be retained for each
            step.
        """
        schema: str  # schema version string (e.g. "pipeline/v1")
        created_at: str  # ISO timestamp
        steps: list[Pipeline.StepRecord]  # recorded steps

    @dataclass(frozen=True)
    class Fact:
        """An entry in the global fact context for the pipeline, representing a value
        that can be shared between steps.

        Attributes
        ----------
        origin : str | None
            The name of the step that created this fact.  If another step accesses this
            fact without declaring the origin step as a prerequisite, then an error
            will be raised.  If None, then the fact is considered to be ephemeral, and
            may have originated from kwargs passed to `pipeline.run()`.
        hash : str
            The SHA-256 hash of the JSON-serialized value of this fact.
        value : Any
            The deserialized value of this fact.  If the value is a mutable type (e.g.
            list or dict), then it will be wrapped in a read-only proxy to prevent
            accidental modification by dependent steps.
        """
        origin: str | None
        hash: str
        value: Any

    # pipeline-wide config
    state_dir: Path
    schema: int = 1
    timeout: int = 30  # seconds
    keep: int = 3

    # registration
    _targets: list[Pipeline.Target] = field(default_factory=list, repr=False)
    _ordered: bool = field(default=False, repr=False)  # skip planning
    _lookup: dict[str, Pipeline.Target] = field(default_factory=dict, repr=False)  # name -> target
    _modules: dict[str, str] = field(default_factory=dict, repr=False)  # module path -> hash

    # TODO: maybe preventing _data from being None would be better.  I can probably
    # write a staticmethod that produces a "default" empty journal, and then always
    # reset it to that value on exit.  That would avoid all the Optional checks and
    # related confusion.

    # TODO: it's also currently impossible to roll back ephemerals if they fully
    # complete without error, since they are not recorded in the journal.  Maybe I
    # should record them anyway, and just mark them as ephemeral so they automatically
    # get cleaned up on entrance to the pipeline context, which forces a re-run in the
    # manner I'm expecting?  That doesn't alter the behavior too much, and increases
    # symmetry with regular steps.

    # context
    _lock: LockDir = field(init=False, repr=False)
    _data: Journal | None = field(default=None, repr=False)
    _facts: dict[str, Pipeline.Fact] = field(default_factory=dict, repr=False)
    _completed: dict[Pipeline.Target, str] = field(default_factory=dict, repr=False)
    _ephemeral: list[Pipeline.StepRecord] = field(default_factory=list, repr=False)
    _active: Pipeline.Target | None = field(default=None, repr=False)
    _attempt_id: str = field(init=False, repr=False)

    def __post_init__(self) -> None:
        if self.keep < 1:
            raise ValueError("pipeline must keep at least one attempt per step")
        mkdir_private(self.state_dir)
        mkdir_private(self.backup_dir)
        self._lock = LockDir(path=self.state_dir / ".lock", timeout=self.timeout)

    # TODO: anywhere I use `.get()` assumes the object is a dict, and I should
    # try to confirm that before using it.

    @staticmethod
    def _json(obj: Any) -> str:
        # NOTE: all kwargs and facts must be JSON-serializable, and need to produce
        # stable hashes for change detection.  We also store in the same format, so that
        # we can round-trip arbitrary data.
        return json.dumps(
            obj,
            sort_keys=True,
            indent=2,
            ensure_ascii=True,
            allow_nan=False
        )

    @staticmethod
    def _readonly(obj: Any) -> Any:
        if isinstance(obj, list):
            return tuple(Pipeline._readonly(v) for v in obj)
        if isinstance(obj, dict):
            return MappingProxyType({k: Pipeline._readonly(v) for k, v in obj.items()})
        return obj

    def _dump(self) -> None:
        atomic_write_text(self.journal, self._json(self._data) + "\n")

    def _module_hash(self, obj: Any) -> str:
        # find originating source file
        source = inspect.getsourcefile(obj) or inspect.getfile(obj)
        if not source:
            return MISSING

        # check cache
        cached = self._modules.get(source)
        if cached is not None:
            return cached

        # compute hash of source file
        path = Path(source)
        if not path.exists() or not path.is_file():
            result = MISSING
        else:
            result = hashlib.sha256(path.read_bytes()).hexdigest()

        # cache result
        self._modules[source] = result
        return result

    def _plan(self) -> None:
        if self._ordered:
            return

        # topological sort
        pending = list(self._targets)
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

        self._targets = order
        self._ordered = True

    def _rollback_step(self, step: Pipeline.StepRecord) -> None:
        name = step.get("name")
        step["ended_at"] = _utc_now_iso()
        step["status"] = "failed"

        # remove any facts set by this step
        facts = step.get("facts")
        if isinstance(facts, dict):
            for key in facts:
                self._facts.pop(key, None)

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

        try:
            # generate new attempt ID
            self._attempt_id = uuid.uuid4().hex

            # load journal state
            changed = False
            if self.journal.exists():
                self._data = json.loads(self.journal.read_text(encoding="utf-8"))
                if not isinstance(self._data, dict):
                    raise OSError("journal must be a valid JSON mapping object")
                if self._data.get("schema") != f"pipeline/v{self.schema}":
                    raise OSError(
                        f"Registry schema mismatch at {self.journal}. Found schema="
                        f"{self._data.get('schema')} (expected pipeline/v{self.schema})."
                    )
            else:  # write new journal
                self._data = {
                    "schema": f"pipeline/v{self.schema}",
                    "created_at": _utc_now_iso(),
                    "steps": [],
                }
                changed = True

            # roll back in-progress steps from previous runs
            if self._rollback_in_progress():
                changed = True

            # write changes to journal if needed
            if changed:
                self._dump()

            # hydrate in-memory state
            self._facts.clear()
            self._completed.clear()
            self._ephemeral.clear()
            for s in self.records:
                if s.get("status") != "completed":
                    continue

                name = s.get("name")
                version = s.get("version")
                if not isinstance(name, str) or not isinstance(version, str):
                    continue

                target = self._lookup.get(name)
                if target is None:
                    continue
                self._completed[target] = version

                # hydrate facts with hashes + readonly views
                facts = s.get("facts")
                if isinstance(facts, dict):
                    for k, v in facts.items():
                        v_norm = v
                        v_json = self._json(v_norm)
                        v_hash = hashlib.sha256(v_json.encode("utf-8")).hexdigest()
                        v_readonly = self._readonly(v_norm)
                        self._facts[k] = Pipeline.Fact(
                            origin=name,
                            hash=v_hash,
                            value=v_readonly
                        )

            return self

        # ensure lock is properly released on error
        except Exception as err:
            self._lock.__exit__(type(err), err, getattr(err, "__traceback__", None))
            raise

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None
    ) -> None:
        if self._lock.depth > 1:
            self._lock.__exit__(exc_type, exc_value, traceback)
            return  # re-entrant case

        try:
            # TODO: maybe _rollback_in_progress() should be extended to cover ephemeral
            # steps as well?  Also, if an exception originates from the rest of `__exit__`,
            # then ephemeral steps will need to be rolled back too.

            # roll back any in-progress steps, including ephemeral steps if there is an error
            self._rollback_in_progress()
            if exc_type is not None:
                for r in reversed(self._ephemeral):
                    self._rollback_step(r)

            # compact the journal to keep only the last N attempts per step
            if self._data is not None:
                steps = self._data.get("steps")
                if isinstance(steps, list):
                    compacted: list[Pipeline.StepRecord] = []
                    seen: dict[str, int] = {}
                    for step in reversed(steps):
                        name = step.get("name")
                        if not isinstance(name, str):
                            continue
                        count = seen.get(name, 0)
                        if count < self.keep:
                            compacted.append(step)
                            seen[name] = count + 1
                    self._data["steps"] = list(reversed(compacted))

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

            # always synchronize journal state on exit
            self._dump()
            self._data = None
            self._facts.clear()
            self._completed.clear()
            self._ephemeral.clear()

        # always release lock
        finally:
            self._lock.__exit__(exc_type, exc_value, traceback)

    @overload
    def __call__(
        self,
        func: Pipeline.Function,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...,
        cache: bool = ...,
    ) -> Pipeline.Function: ...
    @overload
    def __call__(
        self,
        func: None = ...,
        *,
        requires: Iterable[Pipeline.Function] | None = ...,
        enable: bool = ...,
        cache: bool = ...,
    ) -> Callable[[Pipeline.Function], Pipeline.Function]: ...
    def __call__(
        self,
        func: Pipeline.Function | None = None,
        *,
        requires: Iterable[Pipeline.Function] | None = None,
        enable: bool = True,
        cache: bool = False,
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
        cache : bool, optional
            Whether to cache the results of this step in the journal.  If False (the
            default), the step will always be re-executed on each run, regardless of
            its previous state or dependencies.  If any cached step depends on an
            uncached step, an error will be raised during registration.

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
            the step name is not unique, or if a cached step depends on an uncached
            step.
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
                r = frozenset(self._targets)
            else:
                _r = set()
                for dep in requires:
                    dep_name = _qualname(dep)
                    dep_target = self._lookup.get(dep_name)
                    if not dep_target:
                        raise TypeError(f"Pipeline step dependency not found: '{dep_name}'")
                    _r.add(dep_target)
                r = frozenset(_r)

            # validate cache dependencies
            if cache:
                for t in r:
                    if not t.cache:
                        raise TypeError(
                            f"Cached pipeline step '{name}' cannot depend on uncached "
                            f"step '{t}'."
                        )

            # hash originating module
            version = self._module_hash(func)
            if version == MISSING:
                raise TypeError(
                    f"Pipeline step hash could not be determined: '{name}'.  The "
                    "originating module must be available on disk and its content "
                    "hashable for the function to be a valid step."
                )

            # register step
            target = Pipeline.Target(func=func, version=version, cache=cache, requires=r)
            self._targets.append(target)
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
        return len(self._targets)

    def __iter__(self) -> Iterator[Pipeline.Function]:
        """Iterate over the functions registered in this pipeline in topological order.

        Returns
        -------
        Iterator[Pipeline.Function]
            An iterator over the registered functions.
        """
        self._plan()
        for target in self._targets:
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
        """
        Returns
        -------
        Path
            The path to the directory for storing backup files referenced by the
            pipeline.
        """
        return self.state_dir / "backups"

    @property
    def lock_dir(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the pipeline's atomic lock directory, which is created on
            entering the pipeline context and removed on exit.
        """
        return self.state_dir / ".lock"

    @property
    def journal(self) -> Path:
        """
        Returns
        -------
        Path
            The path to the pipeline's persistent journal, in which steps are recorded.
        """
        return self.state_dir / "journal.json"

    @property
    def records(self) -> list[Pipeline.StepRecord]:
        """
        Returns
        -------
        list[Pipeline.StepRecord]
            A list of steps recorded in the journal.

        Raises
        ------
        OSError
            If the pipeline context is not active.
        """
        if self._data is None:
            raise OSError("pipeline context is not active")
        return self._data.setdefault("steps", [])

    @dataclass
    class InProgress:
        """A context manager representing an in-progress installation step."""
        # pylint: disable=protected-access
        target: Pipeline.Target
        record: Pipeline.StepRecord = field(init=False)
        pipeline: Pipeline = field(repr=False)
        facts: dict[str, Pipeline.Fact] = field(default_factory=dict, repr=False)

        def __enter__(self) -> Pipeline.InProgress:
            if self.pipeline._active is not None:
                raise OSError("nested installation steps are not supported")
            self.record = {
                "attempt_id": self.pipeline._attempt_id,
                "name": str(self.target),
                "version": self.target.version,
                "status": "in_progress",
                "started_at": _utc_now_iso(),
                "ended_at": "",
                "requires": sorted(str(p) for p in self.target.requires),
                "accesses": {},
                "ops": [],
                "facts": {},
                "backups": [],
                "error": "",
            }
            if self.target.cache:
                self.pipeline.records.append(self.record)
                self.pipeline._dump()
            self.pipeline._active = self.target
            return self

        def __exit__(
            self,
            exc_type: type[BaseException] | None,
            exc_value: BaseException | None,
            exc_traceback: TracebackType | None
        ) -> None:
            if self.pipeline._active != self.target:
                raise OSError("installation step exit does not match active step")
            try:
                if exc_type is None:
                    # mark step as completed
                    try:
                        self.record["status"] = "completed"
                        self.record["ended_at"] = _utc_now_iso()
                        self.pipeline._facts.update(self.facts)
                        if self.target.cache:
                            self.pipeline._completed[self.target] = self.target.version
                            self.pipeline._dump()
                        else:
                            self.pipeline._ephemeral.append(self.record)
                        return

                    # fall through to rollback
                    except Exception as err:
                        exc_type = type(err)
                        exc_value = err
                        exc_traceback = getattr(err, "__traceback__", None)

                # roll back step on error
                self.pipeline._rollback_step(self.record)
                error = self.record.get("error")
                if isinstance(error, str) and error:
                    self.record["error"] = f"{error}; {exc_type.__name__}: {exc_value}"
                else:
                    self.record["error"] = f"{exc_type.__name__}: {exc_value}"
                if self.target.cache:
                    self.pipeline._dump()
            finally:
                self.pipeline._active = None

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
            return self.pipeline._attempt_id

        def _origin_is_required(self, origin: str) -> bool:
            target = self.pipeline._lookup.get(origin)
            if target is None:
                return False
            requires = self.record.get("requires")
            if not isinstance(requires, list):
                return False
            seen: set[Pipeline.Target] = set()
            stack = [self.pipeline._lookup[r] for r in requires if r in self.pipeline._lookup]
            while stack:
                t = stack.pop()
                if t == target:
                    return True
                seen.add(t)
                stack.extend(t.requires - seen)
            return False

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
                The value of the requested fact.  If the fact comes from the global
                context or corresponds to an argument passed to the pipeline's `run()`
                method, then it will be returned as a read-only, JSON-deserialized
                object, where lists are converted to tuples and dictionaries to
                `MappingProxyType` instances to discourage modification.

            Raises
            ------
            KeyError
                If the requested fact is not found, or if it originates from the global
                context but the corresponding step is not a prerequisite of this step.
            """
            # check local facts first
            if key in self.facts:
                return self.facts[key].value

            # check global facts
            accesses = self.record.setdefault("accesses", {})
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in accesses and
                    not self._origin_is_required(fact.origin)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is not "
                        "a prerequisite of the current step "
                        f"'{self.record.get('name')}'.  Add it to the 'requires' list "
                        "to ensure proper ordering."
                    )
                accesses[key] = fact.hash
                return fact.value

            # # record negative global access and surface key error
            accesses[key] = MISSING
            raise KeyError(f"Fact '{key}' not found in local or global context")

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
            if key in self.pipeline._facts:
                raise KeyError(f"Cannot overwrite '{key}' in global context")

            # serialize value, hash, and then deserialize for storage
            value_json = self.pipeline._json(value)
            value_hash = hashlib.sha256(value_json.encode("utf-8")).hexdigest()
            value_norm = json.loads(value_json)
            self.record.setdefault("facts", {})[key] = value_norm

            # store read-only view in local context, for future reference
            value_readonly = self.pipeline._readonly(value_norm)
            self.facts[key] = Pipeline.Fact(
                origin=self.record.get("name"),
                hash=value_hash,
                value=value_readonly
            )
            if self.target.cache:
                self.pipeline._dump()

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
            if key not in self.facts:
                raise KeyError(f"Fact '{key}' not found in local context")

            self.facts.pop(key, None)
            self.record.get("facts", {}).pop(key, None)
            if self.target.cache:
                self.pipeline._dump()

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
            # check local facts first
            if key in self.facts:
                return True

            # record positive global access
            accesses = self.record.setdefault("accesses", {})
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in accesses and
                    not self._origin_is_required(fact.origin)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is "
                        "not a prerequisite of the current step "
                        f"'{self.record.get('name')}'.  Add it to the 'requires' list "
                        "to ensure proper ordering."
                    )
                accesses[key] = fact.hash
                return True

            # record negative global access
            accesses[key] = MISSING
            return False

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

            Raises
            ------
            KeyError
                If the requested fact originates from the global context but the
                corresponding step is not a prerequisite of this step.
            """
            # check local facts first
            if key in self.facts:
                return self.facts[key].value

            # record positive global access
            accesses = self.record.setdefault("accesses", {})
            if key in self.pipeline._facts:
                fact = self.pipeline._facts[key]
                if (
                    fact.origin is not None and
                    key not in accesses and
                    not self._origin_is_required(fact.origin)
                ):
                    raise KeyError(
                        f"Fact '{key}' originates from step '{fact.origin}', which is not "
                        "a prerequisite of the current step "
                        f"'{self.record.get('name')}'.  Add it to the 'requires' list "
                        "to ensure proper ordering."
                    )
                accesses[key] = fact.hash
                return fact.value

            # record negative global access
            accesses[key] = MISSING
            return default

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
                self.pipeline._json(payload)  # validate serializability
                self.record.setdefault("ops", []).append({
                    "kind": _qualname(type(op)),
                    "payload": payload
                })
                if self.target.cache:
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
            if self.target.cache:
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
            the subscript interface.  Each value must be JSON-serializable, and a hash
            + read-only view of its serialized value will be stored for change
            detection and lookup.  If the value changes between runs, then any steps
            that access it will be invalidated and re-executed.

        Raises
        ------
        OSError
            If the pipeline cannot proceed due to unsatisfied dependencies, or if the
            pipeline context is active.
        KeyError
            If any provided keyword argument conflicts with a fact in the pipeline
            context.
        Exception
            If any step in the installation process fails.  The type of the exception
            will depend on the underlying error.
        """
        if self._lock.depth > 0:
            raise OSError("cannot run a pipeline while within its context")

        # compute topological order
        self._plan()

        # acquire lock + journal context
        with self:
            # serialize kwargs, compute hashes, and store read-only views for lookup
            conflicts: list[str] = [k for k in kwargs if k in self._facts]
            if conflicts:
                raise KeyError(f"kwargs cannot alias facts in pipeline context: {conflicts}")
            for k, v in kwargs.items():
                v_json = self._json(v)
                v_hash = hashlib.sha256(v_json.encode("utf-8")).hexdigest()
                v_readonly = self._readonly(json.loads(v_json))
                self._facts[k] = Pipeline.Fact(
                    origin=None,
                    hash=v_hash,
                    value=v_readonly
                )

            # look for steps that access changed facts or whose requirements have changed
            invalid: set[Pipeline.Target] = set()
            for s in self.records:
                if s.get("status") != "completed":
                    continue

                name = s.get("name")
                target = self._lookup.get(name) if isinstance(name, str) else None
                if target is None:
                    continue

                accesses = s.get("accesses")
                current_requires = sorted(str(r) for r in target.requires)
                recorded_requires = s.get("requires")
                facts = s.get("facts")  # facts produced by this step
                if (
                    not isinstance(accesses, dict) or  # malformed accesses
                    not isinstance(recorded_requires, list) or  # malformed requires
                    current_requires != recorded_requires or  # requirements changed
                    not all(
                        (
                            access_name in self._facts and  # positive access
                            self._facts[access_name].hash == access_hash
                        ) or (
                            access_hash == MISSING and  # negative access
                            access_name not in self._facts
                        ) or (
                            isinstance(facts, dict) and  # fact produced by this step
                            access_name in facts
                        )
                        for access_name, access_hash in accesses.items()
                    )
                ):
                    invalid.add(target)

            # extend to cover all changed targets + dependencies
            for t in self._targets:
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
            for target in self._targets:
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
            if not self._lookup[func_name].cache:
                raise KeyError(f"Cannot undo uncached pipeline step: '{func_name}'")
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
class WriteTextFile:
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
class RmTree:
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
