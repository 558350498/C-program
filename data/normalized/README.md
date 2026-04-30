# Normalized Replay Data

Go preprocessing tools should write normalized files here:

```text
data/normalized/requests.csv
data/normalized/drivers.csv
```

Generated normalized CSV files are ignored by Git. The C++ replay layer should
read from this directory after preprocessing.
