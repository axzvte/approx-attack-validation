"""Pure scheduling bounds and content-verified task artifact reuse.

These helpers do not run a solver. The caller validates solver reports before
recording them and again after restoring them. A reused report is evidence for
the identical task signature only, never a substitute for held-out validation.
"""
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import tempfile


def _integer(value):
    if isinstance(value, bool):
        raise ValueError("Boolean is not an image/count")
    result = int(value)
    if float(value) != result:
        raise ValueError("Nonintegral image/count")
    return result


def elimination_reason(hardware, rows, incumbent, total):
    """Return a safe strict ranking bound, or ``""`` when no proof exists.

Rows must contain completed per-image outcomes for this hardware only. A
per-image outcome may itself be budget limited, but must be the final outcome
of the same calibration protocol used for the incumbent. Probes are not final
outcomes. No bound is inferred on the eventual mean when worst scores tie.
    """
    try:
        total = _integer(total)
        if total < 1 or not rows or len(rows) >= total or not incumbent:
            return ""
        hardware_id = hardware["hardware_id"]
        if not hardware_id or incumbent.get("hardware_id") == hardware_id:
            return ""
        if incumbent["complete"] not in (True, 1, "1") or _integer(incumbent["evaluated_images"]) != total or _integer(incumbent["screen_images"]) != total:
            return ""
        incumbent_successes = _integer(incumbent["numeric_successes"])
        incumbent_worst = float(incumbent["worst_roi_drop_score"])
        if not 0 <= incumbent_successes <= total or not math.isfinite(incumbent_worst) or incumbent_worst < 0:
            return ""
        if incumbent_successes < total and incumbent_worst != 0:
            return ""  # Complete rankings assign a zero to every failed image.
        images = set()
        scores = []
        successes = 0
        for row in rows:
            if row.get("hardware_id") != hardware_id:
                return ""
            image = _integer(row["image"])
            if image < 1 or image in images:
                return ""
            images.add(image)
            status = row["status"]
            if status not in ("numeric_eligible", "quality_not_met", "no_legal_interval_state"):
                return ""
            score = float(row["roi_drop_score"])
            if not math.isfinite(score) or score < 0:
                return ""
            successes += status == "numeric_eligible"
            scores.append(score if status == "numeric_eligible" else 0.0)
        upper = successes + total - len(rows)
        if upper < incumbent_successes:
            return f"success_upper_bound:{upper}<{incumbent_successes}"
        if upper == incumbent_successes and min(scores) < incumbent_worst:
            return f"worst_score_upper_bound:{min(scores):.17g}<{incumbent_worst:.17g}"
    except (KeyError, TypeError, ValueError, OverflowError):
        # Malformed or incomplete evidence cannot justify dropping work.
        return ""
    return ""


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_link(path):
    return path.is_symlink() or (getattr(path, "is_junction", lambda: False)())


def _reject_link_ancestors(path):
    for item in (path, *path.parents):
        if _is_link(item):
            raise ValueError(f"Task reuse does not accept symlinks or junctions: {item}")


def _relative(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or "\x00" in value:
        raise ValueError("Task paths must be nonempty forward-slash relative paths")
    if PurePosixPath(value).is_absolute() or PureWindowsPath(value).drive:
        raise ValueError("Absolute task paths are not allowed")
    if any(part in ("", ".", "..") for part in value.split("/")):
        raise ValueError("Task paths cannot contain empty, dot or parent components")
    return Path(*value.split("/"))


def _inside(root, relative):
    candidate = root / _relative(relative)
    _reject_link_ancestors(candidate)
    resolved = candidate.resolve()
    if root not in resolved.parents:
        raise ValueError("Task path escapes its root")
    return candidate


def _artifact_hashes(folder):
    """Hash all regular files, rejecting links before following file contents."""
    _reject_link_ancestors(folder)
    if not folder.is_dir():
        raise ValueError("Task artifact directory does not exist")
    artifacts = {}
    for path in sorted(folder.rglob("*")):
        if _is_link(path):
            raise ValueError(f"Task artifact is a symlink or junction: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise ValueError(f"Task artifact is not a regular file: {path}")
        relative = path.relative_to(folder).as_posix()
        if relative != "TASK.json":
            artifacts[relative] = _sha256(path)
    return artifacts


class TaskCache:
    """Read verified artifacts from a prior run into a fresh output tree.

``source`` is optional, read only and disjoint from ``root``. Neither a
signature mismatch nor a corrupt/incomplete task is a cache hit. Existing
destination tasks are never overwritten. Unsafe paths raise ``ValueError``.
    """
    def __init__(self, root, source=None):
        root = Path(root).absolute()
        _reject_link_ancestors(root)
        self.root = root.resolve()
        self.source = None
        if source is not None:
            source = Path(source).absolute()
            _reject_link_ancestors(source)
            source = source.resolve()
            if not source.is_dir():
                raise ValueError("Task reuse source must be an existing directory")
            if self.root == source or self.root in source.parents or source in self.root.parents:
                raise ValueError("Task reuse source and destination must not overlap")
            self.source = source

    def restore(self, relative, signature):
        destination = _inside(self.root, relative)
        expected = _canonical(signature)
        if destination.exists() or self.source is None:
            return False
        origin = _inside(self.source, relative)
        if not origin.is_dir():
            return False
        manifest_path = origin / "TASK.json"
        if _is_link(manifest_path):
            raise ValueError("Task manifest cannot be a symlink or junction")
        # Check the tree for unsafe links even when a corrupt manifest would
        # otherwise cause an early miss. Never silently follow those links.
        try:
            artifacts = _artifact_hashes(origin)
        except OSError:
            return False
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if (not isinstance(manifest, dict) or manifest.get("version") != 1 or
                    manifest.get("completed") is not True or
                    _canonical(manifest.get("signature")) != expected or
                    not isinstance(manifest.get("artifacts"), dict) or
                    not manifest["artifacts"] or manifest["artifacts"] != artifacts):
                return False
        except (OSError, ValueError, TypeError):
            return False
        destination.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix=".task-restore-", dir=destination.parent))
        try:
            # Copy into a temporary sibling first. A source changed during the
            # copy is not accepted, and no partial report becomes a task hit.
            shutil.copytree(origin, staging, dirs_exist_ok=True)
            if _artifact_hashes(staging) != artifacts:
                return False
            copied_manifest = json.loads((staging / "TASK.json").read_text(encoding="utf-8"))
            if copied_manifest != manifest:
                return False
            if destination.exists():
                return False
            staging.rename(destination)
            return True
        except (OSError, ValueError, TypeError):
            return False
        finally:
            if staging.exists():
                shutil.rmtree(staging)

    def record(self, relative, signature):
        folder = _inside(self.root, relative)
        # Validate JSON before writing anything, including rejecting NaNs.
        signature = json.loads(_canonical(signature))
        artifacts = _artifact_hashes(folder)
        if not artifacts:
            raise ValueError("An empty task cannot be recorded as complete")
        manifest = dict(version=1, completed=True, signature=signature, artifacts=artifacts)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                             prefix=".TASK-", suffix=".tmp", dir=folder.parent,
                                             delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(_canonical(manifest) + "\n")
            os.replace(temporary, folder / "TASK.json")
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()


def source_fingerprint(root):
    """Fingerprint solver/orchestration code, excluding data, tests and outputs."""
    root = Path(root).absolute()
    _reject_link_ancestors(root)
    root = root.resolve()
    extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".py", ".sh", ".cmake"}
    paths = [root / "CMakeLists.txt"]
    for name in ("app", "include", "src", "scripts"):
        folder = root / name
        if not folder.exists():
            continue
        _reject_link_ancestors(folder)
        for path in folder.rglob("*"):
            if _is_link(path):
                raise ValueError(f"Source fingerprint does not accept links: {path}")
            if path.is_file() and path.suffix.lower() in extensions and "__pycache__" not in path.parts:
                paths.append(path)
    hashes = {}
    for path in sorted(paths):
        _reject_link_ancestors(path)
        hashes[path.relative_to(root).as_posix()] = _sha256(path)
    return hashlib.sha256(_canonical(hashes).encode("utf-8")).hexdigest()
