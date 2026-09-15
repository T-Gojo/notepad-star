"""Archive the actual repository source, excluding ignored SDK/build outputs."""
import argparse
import subprocess
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(
    ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root
).decode("utf-8").split("\0")
deleted = set(subprocess.check_output(["git", "ls-files", "--deleted", "-z"], cwd=root).decode("utf-8").split("\0"))
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(output, "x", compression=ZIP_DEFLATED, compresslevel=6) as archive:
    for name in sorted(set(paths) - {"", *deleted}):
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"Source snapshot requires a regular file: {name}")
        if path.resolve() == output:
            raise RuntimeError("Source output must be outside the source file list.")
        archive.write(path, "notepad-star-source/" + name.replace("\\", "/"))
print(f"Corresponding source: {output}")
