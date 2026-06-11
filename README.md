# cpd

Parallel document clustering (k-means-style) — IST CPD project.

## Structure

- `serial/` — single-threaded baseline (`docs`)
- `omp/` — OpenMP version (`docs-omp`)
- `mpi/` — MPI + OpenMP versions (`docs-mpi-checker`, `docs-mpi-row`)
- `tests/` — input/expected-output pairs (`T01`–`T08`) and timings (`times.md`)
- `statement.pdf`, `report_omp.pdf`, `report_mpi.pdf` — problem and reports

## Build

```sh
make -C serial/src
make -C omp/src
make -C mpi/src
```

## Run

```sh
./serial/src/docs tests/T01/ex5-1d.in
./omp/src/docs-omp tests/T01/ex5-1d.in
mpirun -n 4 ./mpi/src/docs-mpi-row tests/T01/ex5-1d.in
```

## Test

```sh
./test.sh                              # builds omp and runs all tests
./test.sh ./serial/src/docs            # test a specific binary
./test.sh mpirun -n 4 ./mpi/src/docs-mpi-row 1 2 3   # selected tests
```

## Authors

João Russo, Juan Viteri, Pedro Gomes.
