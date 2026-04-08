//
// Created by:
//      João Russo  ist116543
//      Juan Viteri ist115766
//      Pedro Gomes ist1103468
//

#include "docs-mpi.h"

// --------------- Problem data (fixed after init) ---------------
Documents documents;
Cabinets cabinets;
size_t subject_count;

// --------------- MPI process identity ---------------
int rank;
int n_procs;

// --------------- 2D Cartesian grid topology ---------------
int row;                    // this process's row in the grid
int col;                    // this process's column in the grid
int grid_rows;              // total rows in grid (sqrt(P)-ish)
int grid_cols;              // total columns in grid
MPI_Comm row_comm;          // communicator: same docs, different cabs
MPI_Comm col_comm;          // communicator: same cabs, different docs

// --------------- Local partition bounds ---------------
size_t doc_s;               // first doc index this process handles
size_t doc_e;               // one past last doc index
size_t local_doc_count;     // doc_e - doc_s

size_t cab_s;               // first cab index this process handles
size_t cab_e;               // one past last cab index
size_t local_cab_count;     // cab_e - cab_s

// --------------- Per-iteration working data ---------------
Change *local_changes;      // best local cab for each local doc
size_t *local_count;        // partial doc counts per local cab (this process)
double *local_sum;          // partial score sums per local cab (this process)
int local_swaps;            // did any local doc move?

Change *global_changes;     // all row processes' candidates (after Allgather)
size_t *global_count;       // reduced doc counts per local cab (after Allreduce)
double *global_sum;         // reduced score sums per local cab (after Allreduce)
int global_swaps;           // did any doc move globally?

// --------------- OpenMP thread-local accumulators ---------------
int nthreads;               // omp_get_max_threads()
size_t *thr_count;          // per-thread doc counts [nthreads x local_cab_count]
double *thr_sum;            // per-thread score sums [nthreads x local_cab_count x S]

// ===================================================================
// I/O & PARSING
// ===================================================================

// Reads C, D, S from file header, generates random scores (same seed on all processes)
bool parse_problem(char *filename, Problem *p) {
    bool status = true;
    FILE *file = fopen(filename, "r");
    if (!file) {
        status = false;
        goto cleanup;
    }

    char line[1024];
    if (!fgets(line, 1024, file) ||
        !sscanf(line, "%zu %zu %zu", &p->cabinet_count, &p->document_count, &p->subject_count)) {
        status = false;
        goto cleanup;
    }

    p->document_scores = (double *)calloc(p->document_count * p->subject_count, sizeof(double));
    if (!p->document_scores) goto cleanup;

    srand(SEED);
    for (size_t i = 0; i < p->document_count; i++)
        for (size_t j = 0; j < p->subject_count; j++)
            p->document_scores[p->subject_count * i + j] = UNIF01 * RAND_RANGE;

cleanup:
    if (file) fclose(file);
    if (!status) free_problem(p);

    return status;
}

void free_problem(Problem *p) {
    if (p->document_scores) free(p->document_scores);
}

// One cabinet id per line, ordered by document index (only rank 0 calls this)
void print_result() {
    for (size_t i = 0; i < documents.count; i++) {
        printf("%zu\n", documents.parent_ids[i]);
    }
}

// ===================================================================
// INITIALIZATION
// ===================================================================

// Populates global documents struct, shares score pointer with problem
bool init_documents(Problem *problem) {
    documents.count = problem->document_count;
    documents.scores = problem->document_scores;
    documents.parent_ids = calloc(documents.count, sizeof(size_t));

    return documents.parent_ids;
}

// Allocates zeroed score array for all cabinets [C x S]
bool init_cabinets(Problem *problem) {
    cabinets.count = problem->cabinet_count;
    cabinets.scores = (double *)calloc(problem->cabinet_count * problem->subject_count, sizeof(double));

    return cabinets.scores;
}

// Allocates local (this process) and global (after Allgather) change buffers
bool init_changes() {
    local_changes = (Change *)calloc(local_doc_count, sizeof(Change));
    global_changes = (Change *)calloc(local_doc_count * grid_cols, sizeof(Change));

    return local_changes && global_changes;
}

// Allocates local/global count+sum arrays and per-thread OMP accumulators
bool init_sums_counts() {
    local_count = calloc(local_cab_count, sizeof(size_t));
    local_sum = calloc(local_cab_count * subject_count, sizeof(double));

    global_count = calloc(local_cab_count, sizeof(size_t));
    global_sum = calloc(local_cab_count * subject_count, sizeof(double));

    nthreads = omp_get_max_threads();

    thr_count = calloc(nthreads * local_cab_count, sizeof(size_t));
    thr_sum = calloc(nthreads * local_cab_count * subject_count, sizeof(double));

    return local_sum && local_count && global_count && global_sum && thr_count && thr_sum;
}

// ===================================================================
// MPI TOPOLOGY & PARTITIONING
// ===================================================================

// Creates 2D Cartesian grid and row/col sub-communicators
void compute_comms() {
    row_comm = MPI_COMM_NULL;
    col_comm = MPI_COMM_NULL;

    // Factor P into 2D: e.g. 16 -> {4, 4}, 8 -> {4, 2}
    int dims[2] = {0, 0};
    MPI_Dims_create(n_procs, 2, dims);
    grid_rows = dims[0];
    grid_cols = dims[1];

    // Label all processes on a grid (no wraparound)
    int periods[2] = {0, 0};
    MPI_Comm cart_comm;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart_comm);

    // "What's my (row, col)?" e.g. rank 7 -> row=1, col=3
    int coords[2];
    MPI_Cart_coords(cart_comm, rank, 2, coords);
    row = coords[0];
    col = coords[1];

    // Slice grid into sub-communicators
    int remain_row[2] = {0, 1};  // {0,1} = group by row -> processes sharing same docs
    int remain_col[2] = {1, 0};  // {1,0} = group by col -> processes sharing same cabs
    MPI_Cart_sub(cart_comm, remain_row, &row_comm);
    MPI_Cart_sub(cart_comm, remain_col, &col_comm);

    // Only need row/col comms from here on
    MPI_Comm_free(&cart_comm);
}

// Splits total items evenly across num_parts, handles remainder
void compute_partition(size_t total, int num_parts, int part_id, size_t *start, size_t *end, size_t *count) {
    size_t base = total / (size_t)num_parts;
    size_t remainder = total % (size_t)num_parts;
    size_t s = (size_t)part_id * base + ((size_t)part_id < remainder ? (size_t)part_id : remainder);
    size_t c = base + ((size_t)part_id < remainder ? 1 : 0);
    *start = s;
    *end = s + c;
    *count = c;
}

// Collects all parent_ids to rank 0 for final output
void gather_parent_ids() {
    // Only col-0 processes have final parent_ids (row Allgather resolved best cabs)
    if (col != 0) return;

    int *recv_counts = NULL;
    int *displs = NULL;

    // Only rank 0 needs to know how many docs each row-process sends and where to place them
    if (rank == 0) {
        recv_counts = malloc(grid_rows * sizeof(int));
        displs = malloc(grid_rows * sizeof(int));

        for (int r = 0; r < grid_rows; r++) {
            size_t rs, re, rc;
            compute_partition(documents.count, grid_rows, r, &rs, &re, &rc);
            recv_counts[r] = (int)rc;   // how many docs row r sends
            displs[r] = (int)rs;        // offset in output array for row r
        }
    }

    // Each col-0 process sends its local parent_ids, rank 0 assembles the full array
    MPI_Gatherv(&documents.parent_ids[doc_s], (int)local_doc_count,
                MPI_UNSIGNED_LONG, documents.parent_ids, recv_counts, displs,
                MPI_UNSIGNED_LONG, 0, col_comm);

    if (recv_counts) free(recv_counts);
    if (displs) free(displs);
}

// ===================================================================
// DISTANCE COMPUTATION
// ===================================================================

// Euclidean distance squared (no sqrt — monotonic, same min)
double calculate_distance(double *score_1, double *score_2) {
    double sum = 0;

    for (size_t i = 0; i < subject_count; i++) {
        double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }

    return sum;
}

// Finds closest cabinet among LOCAL cabs only (cab_s to cab_e)
size_t get_closest_local_cabinet_index(size_t parent_id, double *document_scores, double *distance) {
    *distance = INFINITY;

    for (size_t j = cab_s; j < cab_e; j++) {
        double calculated_distance = calculate_distance(&cabinets.scores[j * subject_count], document_scores);

        if (calculated_distance < *distance) {
            *distance = calculated_distance;
            parent_id = j;
        }
    }

    return parent_id;
}

// ===================================================================
// CORE ALGORITHM
// ===================================================================

// Round-robin initial assignment, only accumulates into local cabinets
void assign_to_cabinets() {
    // Only track counts for cabinets this process owns
    size_t *cabinet_document_counts = calloc(local_cab_count, sizeof(size_t));
    if (!cabinet_document_counts) return;

    // Every process assigns ALL docs (so parent_ids is consistent everywhere)
#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < documents.count; i++) {
        size_t cab_idx = i % cabinets.count;  // round-robin: doc i -> cab i%C

        documents.parent_ids[i] = cab_idx;

        // Not my cabinet? skip accumulation, only accumulate for local cabs
        if (cab_idx < cab_s || cab_idx >= cab_e) continue;
        size_t array_cab_idx = cab_idx - cab_s;  // convert global to local index

#pragma omp atomic
        cabinet_document_counts[array_cab_idx]++;

        // Accumulate doc scores into cabinet (atomics because multiple OMP threads)
        for (size_t j = 0; j < subject_count; j++) {
#pragma omp atomic
            cabinets.scores[cab_idx * subject_count + j] += documents.scores[i * subject_count + j];
        }
    }

    // Divide accumulated sums by count to get initial centroids (local cabs only)
#pragma omp parallel for schedule(static)
    for (size_t cab_idx = 0; cab_idx < local_cab_count; cab_idx++) {
        size_t doc_count = cabinet_document_counts[cab_idx];
        for (size_t sub_index = 0; sub_index < subject_count; sub_index++) {
            if (doc_count != 0) {
                cabinets.scores[(cab_s + cab_idx) * subject_count + sub_index] /= (double)doc_count;
                continue;
            }
            // Empty cabinet -> centroid at origin
            cabinets.scores[(cab_s + cab_idx) * subject_count + sub_index] = 0.0;
        }
    }

    free(cabinet_document_counts);
}

// Each process finds best cab among its LOCAL cabs for each local doc
void compute_best_local_cabinet_for_documents() {
#pragma omp parallel for schedule(static)
    for (size_t d = doc_s; d < doc_e; d++) {
        double distance = 0;
        size_t cab_idx = get_closest_local_cabinet_index(documents.parent_ids[d], &documents.scores[d * subject_count], &distance);
        local_changes[d - doc_s] = (Change){cab_idx, distance};
    }
}

// Pick global best cab from all row processes' candidates, accumulate partial sums into thread-local arrays, then merge
int assign_best_cabinet() {
    int swaps = 0;
    memset(local_count, 0, local_cab_count                                   * sizeof(size_t));
    memset(local_sum,   0, local_cab_count * subject_count                   * sizeof(double));
    memset(thr_count,   0, nthreads        * local_cab_count                 * sizeof(size_t));
    memset(thr_sum,     0, nthreads        * local_cab_count * subject_count * sizeof(double));

    // For each local doc, pick the global best cab from all col processes' proposals
#pragma omp parallel for schedule(static) reduction(| : swaps)
    for (size_t d = 0; d < local_doc_count; d++) {
        // Each OMP thread gets its own count/sum slice to avoid atomics
        int tid = omp_get_thread_num();
        size_t *my_count = &thr_count[tid * local_cab_count];
        double *my_sum = &thr_sum[tid * local_cab_count * subject_count];

        size_t old_cab_idx = documents.parent_ids[doc_s + d];
        size_t new_cab_idx = old_cab_idx;
        double min_distance = INFINITY;

        // Scan all col processes' proposals for this doc, pick min distance
        for (int i = 0; i < grid_cols; i++) {
            Change *entry = &global_changes[i * local_doc_count + d];
            if (entry->distance < min_distance) {
                min_distance = entry->distance;
                new_cab_idx = entry->cab_idx;
            }
        }

        // Commit the winning cabinet
        documents.parent_ids[doc_s + d] = new_cab_idx;

        // Track if anything moved (convergence flag)
        if (new_cab_idx != old_cab_idx) swaps = 1;
        // Only accumulate scores if winning cab is local to this process
        if (new_cab_idx < cab_s || new_cab_idx >= cab_e) continue;

        // Accumulate into thread-local arrays (no contention)
        size_t arr_cab_index = new_cab_idx - cab_s;
        my_count[arr_cab_index]++;
        for (size_t s = 0; s < subject_count; s++)
            my_sum[arr_cab_index * subject_count + s] += documents.scores[(doc_s + d) * subject_count + s];
    }

    // Merge all threads' partial counts/sums into process-level local_count/local_sum
#pragma omp parallel for schedule(static)
    for (size_t c = 0; c < local_cab_count; c++) {
        for (int t = 0; t < nthreads; t++) {
            local_count[c] += thr_count[t * local_cab_count + c];
            for (size_t s = 0; s < subject_count; s++)
                local_sum[c * subject_count + s] += thr_sum[t * local_cab_count * subject_count + c * subject_count + s];
        }
    }

    return swaps;
}

// Allreduce partial counts/sums across column, then compute centroids
void recompute_scores() {
    // Sum all row processes' partial counts and sums for our local cabs (non-blocking)
    MPI_Request requests[2];
    MPI_Iallreduce(local_count, global_count, (int)local_cab_count, MPI_UNSIGNED_LONG, MPI_SUM, col_comm, &requests[0]);
    MPI_Iallreduce(local_sum, global_sum, (int)(local_cab_count * subject_count), MPI_DOUBLE, MPI_SUM, col_comm, &requests[1]);
    // Wait for both reductions to finish before using results
    MPI_Waitall(2, requests, MPI_STATUS_IGNORE);

    // Compute centroids: score = total_sum / total_count (0 if empty cabinet)
#pragma omp parallel for schedule(static)
    for (size_t c = 0; c < local_cab_count; c++)
        for (size_t s = 0; s < subject_count; s++)
            cabinets.scores[(cab_s + c) * subject_count + s] = global_count[c] ? global_sum[c * subject_count + s] / (double)global_count[c] : 0.0;
}

// ===================================================================
// MAIN
// ===================================================================

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    // MPI bootstrap: every process gets rank (id) and n_procs (total)
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &n_procs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Time measurement variable
    double exec_time;

    // Problem data structure, shared by all processes (same after parsing)
    Problem problem;
    problem.document_scores = NULL;

    // Every process parses independently (same seed -> same data)
    if (!parse_problem(argv[1], &problem) || !init_documents(&problem) || !init_cabinets(&problem))
        goto cleanup;
    subject_count = problem.subject_count;

    // Build 2D grid, assign each process its doc/cab partition
    compute_comms();
    compute_partition(documents.count, grid_rows, row, &doc_s, &doc_e, &local_doc_count);
    compute_partition(cabinets.count, grid_cols, col, &cab_s, &cab_e, &local_cab_count);

    // Allocate working buffers (changes, counts, sums, thread-local)
    if (!init_changes() || !init_sums_counts()) goto cleanup;
    global_swaps = 0;

    // Start timer after setup, before initial assignment
    exec_time = -omp_get_wtime();

    // Round-robin initial assignment, each process scores its local cabs
    assign_to_cabinets();   // Also does centroid calculation for local cabs based on initial assignment
    do {
        // Each process: best cab for its docs among LOCAL cabs only
        compute_best_local_cabinet_for_documents();

        // Share candidates across row (same docs, different cabs)
        MPI_Allgather(local_changes, (int)(local_doc_count * sizeof(Change)), MPI_BYTE, 
                     global_changes, (int)(local_doc_count * sizeof(Change)), MPI_BYTE, row_comm);

        // Pick global best from all candidates, accumulate local sums
        local_swaps = assign_best_cabinet();

        // Non-blocking convergence check (overlaps with recompute)
        MPI_Request request;
        MPI_Iallreduce(&local_swaps, &global_swaps, 1, MPI_INT, MPI_LOR, col_comm, &request);

        // Allreduce counts/sums on col_comm, then centroids = sum/count
        recompute_scores();

        // Wait for convergence result
        MPI_Waitall(1, &request, MPI_STATUS_IGNORE);

    } while (global_swaps);

    // Stop timer after convergence, before output gathering
    exec_time += omp_get_wtime();

    // Collect all assignments to rank 0 for output and print result
    gather_parent_ids();
    if (rank == 0) {
        fprintf(stderr, "%.1fs\n", exec_time);
        print_result();
    }

cleanup:
    free_problem(&problem);
    if (cabinets.scores) free(cabinets.scores);
    if (documents.parent_ids) free(documents.parent_ids);
    if (global_changes) free(global_changes);
    if (local_changes) free(local_changes);
    if (local_count) free(local_count);
    if (local_sum) free(local_sum);
    if (global_count) free(global_count);
    if (global_sum) free(global_sum);
    if (thr_count) free(thr_count);
    if (thr_sum) free(thr_sum);
    if (row_comm != MPI_COMM_NULL) MPI_Comm_free(&row_comm);
    if (col_comm != MPI_COMM_NULL) MPI_Comm_free(&col_comm);

    MPI_Finalize();
    return 0;
}
