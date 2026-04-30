"""Download the NYC Taxi Trip Duration Kaggle dataset into the project tree."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

import kagglehub


DATASET_SLUG = "yasserh/nyc-taxi-trip-duration"
PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = (
    PROJECT_ROOT / "data" / "datasets" / "nyc-taxi-trip-duration" / "raw"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download Kaggle taxi data and copy it into data/datasets."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Local raw dataset directory. Defaults to project data/datasets.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite files that already exist in the local raw directory.",
    )
    return parser.parse_args()


def copy_dataset(cache_path: Path, output_dir: Path, force: bool) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    copied: list[Path] = []

    for source in sorted(cache_path.rglob("*")):
        if not source.is_file():
            continue

        relative_path = source.relative_to(cache_path)
        destination = output_dir / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)

        if destination.exists() and not force:
            continue

        shutil.copy2(source, destination)
        copied.append(destination)

    return copied


def list_output_files(output_dir: Path) -> list[Path]:
    if not output_dir.exists():
        return []
    return sorted(path for path in output_dir.rglob("*") if path.is_file())


def main() -> int:
    args = parse_args()
    output_dir = args.output_dir.resolve()

    cache_path = Path(kagglehub.dataset_download(DATASET_SLUG)).resolve()
    print(f"Kaggle dataset: {DATASET_SLUG}")
    print(f"Kaggle cache path: {cache_path}")
    print(f"Local raw data path: {output_dir}")

    copied = copy_dataset(cache_path, output_dir, args.force)
    if copied:
        print(f"Copied {len(copied)} file(s).")
    else:
        print("No files copied; existing files were kept. Use --force to overwrite.")

    files = list_output_files(output_dir)
    print("Local raw data files:")
    for file_path in files:
        size = file_path.stat().st_size
        print(f"- {file_path.relative_to(output_dir)} ({size} bytes)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
