# Python Kaggle Download Tool

This tool downloads the first project dataset through `kagglehub` and copies the
files into a stable local project path.

## Dataset

- Kaggle slug: `yasserh/nyc-taxi-trip-duration`
- Local raw data path: `data/datasets/nyc-taxi-trip-duration/raw/`

## Setup

```powershell
pip install -r tools/python_kaggle_download/requirements.txt
```

## Usage

```powershell
python tools/python_kaggle_download/download_dataset.py
```

Use `--force` to overwrite files that already exist in the local raw data
directory:

```powershell
python tools/python_kaggle_download/download_dataset.py --force
```

The raw CSV files are intentionally ignored by Git. Keep only scripts and README
files under version control.
