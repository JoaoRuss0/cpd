#include <math.h>
#include <mpi.h>
#include <omp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEED 1234
#define RAND_RANGE 10.0
#define UNIF01 ((double) rand() / RAND_MAX)

typedef struct Cabinets Cabinets;
typedef struct Documents Documents;
typedef struct Problem Problem;
typedef struct Change Change;

struct Documents {
    size_t count;
    size_t *parent_ids;
    double *scores;
};

struct Cabinets {
    size_t count;
    size_t *doc_count;
    double *scores;
    size_t *doc_ids;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    double *document_scores;
};

struct Change {
    size_t cab_idx;
    double distance;
};

size_t get_closest_local_cabinet_index(size_t parent_id, double *document_scores, double* distance);
void assign_to_cabinets();
double calculate_distance(double *score_1, double *score_2);
void compute_best_local_cabinet_for_documents();
void increment_cabinet_scores(size_t cab_idx, size_t doc_idx);
void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx);
void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add);
void free_problem(Problem *p);
bool parse_problem(char *filename, Problem *p);
bool init_cabinets(Problem *problem);
bool init_documents(Problem *problem);
void print_result();
void compute_comms();
void gather_parent_ids();
bool init_changes();
bool init_sums_counts();
void compute_partition(size_t total, int num_parts, int part_id, size_t *start, size_t *end, size_t *count);
int assign_best_cabinet();
void recompute_scores();

Documents documents;
Cabinets cabinets;
size_t subject_count;

int rank;
int n_procs;

int row;
int col;
int grid_rows;
int grid_cols;
MPI_Comm row_comm;
MPI_Comm col_comm;

size_t doc_s;
size_t doc_e;
size_t local_doc_count;

size_t cab_s;
size_t cab_e;
size_t local_cab_count;

Change* local_changes;
Change* global_changes;

size_t *local_count;
double *local_sum;
int local_swaps;

size_t *global_count;
double *global_sum;
int global_swaps;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &n_procs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    double exec_time;

    Problem problem;
    problem.document_scores = NULL;

    if (!parse_problem(argv[1], &problem) || !init_documents(&problem) ||
        !init_cabinets(&problem))
        goto cleanup;
    subject_count = problem.subject_count;

    compute_comms();
    compute_partition(documents.count, grid_rows, row, &doc_s, &doc_e, &local_doc_count);
    compute_partition(cabinets.count, grid_cols, col, &cab_s, &cab_e, &local_cab_count);

    if (!init_changes() || !init_sums_counts()) goto cleanup;
    global_swaps = 0;

    exec_time = -omp_get_wtime();

    assign_to_cabinets();
    do {
        compute_best_local_cabinet_for_documents();
        MPI_Allgather(local_changes, (int)(local_doc_count * sizeof(Change)), MPI_BYTE,
              global_changes, (int)(local_doc_count * sizeof(Change)), MPI_BYTE,
              row_comm);
        local_swaps = assign_best_cabinet();

        MPI_Request request;
        MPI_Iallreduce(&local_swaps, &global_swaps, 1, MPI_INT, MPI_LOR, col_comm, &request);
        recompute_scores();
        MPI_Waitall(1, &request, MPI_STATUS_IGNORE);

    } while (global_swaps);

    exec_time += omp_get_wtime();
    gather_parent_ids();

    if (rank == 0) {
        fprintf(stderr, "%.1fs\n", exec_time);
        print_result();
    }

cleanup:
    free_problem(&problem);
    if (cabinets.doc_count) free(cabinets.doc_count);
    if (cabinets.scores) free(cabinets.scores);
    if (documents.parent_ids) free(documents.parent_ids);
    if (global_changes) free(global_changes);
    if (local_changes) free(local_changes);
    if (local_count) free(local_count);
    if (local_sum) free(local_sum);
    if (global_count) free(global_count);
    if (global_sum) free(global_sum);
    if (row_comm != MPI_COMM_NULL) MPI_Comm_free(&row_comm);
    if (col_comm != MPI_COMM_NULL) MPI_Comm_free(&col_comm);

    MPI_Finalize();
    return 0;
}

void recompute_scores() {

    MPI_Request requests[2];
    MPI_Iallreduce(local_count, global_count, (int)cabinets.count,
                   MPI_UNSIGNED_LONG, MPI_SUM, col_comm, &requests[0]);
    MPI_Iallreduce(local_sum, global_sum, (int)(cabinets.count * subject_count),
                  MPI_DOUBLE, MPI_SUM, col_comm, &requests[1]);
    MPI_Waitall(2, requests, MPI_STATUS_IGNORE);

    for (size_t c = 0; c < cabinets.count; c++)
        for (size_t s = 0; s < subject_count; s++)
            cabinets.scores[c * subject_count + s] = global_count[c] ? global_sum[c * subject_count + s] / (double) global_count[c] : 0.0;
}

void gather_parent_ids() {
    if (col != 0) return;

    int *recv_counts = NULL;
    int *displs = NULL;

    if (rank == 0) {
        recv_counts = malloc(grid_rows * sizeof(int));
        displs = malloc(grid_rows * sizeof(int));

        for (int r = 0; r < grid_rows; r++) {
            size_t rs, re, rc;
            compute_partition(documents.count, grid_rows, r, &rs, &re, &rc);
            recv_counts[r] = (int)rc;
            displs[r] = (int)rs;
        }
    }

    MPI_Gatherv(&documents.parent_ids[doc_s], (int)local_doc_count,
                MPI_UNSIGNED_LONG, documents.parent_ids, recv_counts, displs,
                MPI_UNSIGNED_LONG, 0, col_comm);

    if (recv_counts) free(recv_counts);
    if (displs) free(displs);
}

void assign_to_cabinets() {
    for (size_t i = 0; i < documents.count; i++) {
        size_t cab_idx = i % cabinets.count;

        size_t doc_count = cabinets.doc_count[cab_idx];
        size_t new_doc_count = doc_count + 1;

        for (size_t j = 0; j < subject_count; j++) {
            size_t idx = cab_idx * subject_count + j;

            if (new_doc_count == 0) {
                cabinets.scores[idx] = 0;
                continue;
            }

            cabinets.scores[idx] = (cabinets.scores[idx] * (double)doc_count +
                                    documents.scores[i * subject_count + j]) /
                                   (double)new_doc_count;
        }
        cabinets.doc_count[cab_idx] = new_doc_count;
    }
}

void compute_best_local_cabinet_for_documents() {

    for (size_t d = doc_s; d < doc_e; d++) {
        double distance = 0;
        size_t cab_idx = get_closest_local_cabinet_index(documents.parent_ids[d], &documents.scores[d * subject_count], &distance);
        local_changes[d - doc_s] = (Change){cab_idx, distance};
    }
}

size_t get_closest_local_cabinet_index(size_t parent_id, double *document_scores, double* distance) {
    *distance = INFINITY;

    for (size_t j = cab_s; j < cab_e; j++) {
        double calculated_distance = calculate_distance(
            &cabinets.scores[j * subject_count], document_scores);

        if (calculated_distance < *distance) {
            *distance = calculated_distance;
            parent_id = j;
        }
    }

    return parent_id;
}

double calculate_distance(double *score_1, double *score_2) {
    double sum = 0;

    for (size_t i = 0; i < subject_count; i++) {
        double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }

    return sum;
}

bool init_cabinets(Problem *problem) {
    cabinets.count = problem->cabinet_count;
    cabinets.scores = (double *)calloc(
        problem->cabinet_count * problem->subject_count, sizeof(double));
    cabinets.doc_count = (size_t *)calloc(cabinets.count, sizeof(size_t));

    return cabinets.scores && cabinets.doc_count;
}

bool init_documents(Problem *problem) {
    documents.count = problem->document_count;
    documents.scores = problem->document_scores;
    documents.parent_ids = calloc(documents.count, sizeof(size_t));

    return documents.parent_ids;
}

void print_result() {
    for (size_t i = 0; i < documents.count; i++) {
        printf("%zu\n", documents.parent_ids[i]);
    }
}

bool parse_problem(char *filename, Problem *p) {
    bool status = true;
    FILE *file = fopen(filename, "r");
    if (!file) {
        status = false;
        goto cleanup;
    }

    char line[1024];
    if (!fgets(line, 1024, file) ||
        !sscanf(line, "%zu %zu %zu", &p->cabinet_count, &p->document_count,
                &p->subject_count)) {
        status = false;
        goto cleanup;
    }

    p->document_scores =
        (double *)calloc(p->document_count * p->subject_count, sizeof(double));
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

void compute_comms() {
    row_comm = MPI_COMM_NULL;
    col_comm = MPI_COMM_NULL;

    int dims[2] = {0, 0};
    MPI_Dims_create(n_procs, 2, dims);
    grid_rows = dims[0];
    grid_cols = dims[1];

    int periods[2] = {0, 0};
    MPI_Comm cart_comm;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &cart_comm);

    int coords[2];
    MPI_Cart_coords(cart_comm, rank, 2, coords);
    row = coords[0];
    col = coords[1];

    int remain_row[2] = {0, 1};
    int remain_col[2] = {1, 0};
    MPI_Cart_sub(cart_comm, remain_row, &row_comm);
    MPI_Cart_sub(cart_comm, remain_col, &col_comm);

    MPI_Comm_free(&cart_comm);
}

void compute_partition(size_t total, int num_parts, int part_id, size_t *start, size_t *end, size_t *count) {
    size_t base      = total / (size_t)num_parts;
    size_t remainder = total % (size_t)num_parts;
    size_t s = (size_t)part_id * base +
               ((size_t)part_id < remainder ? (size_t)part_id : remainder);
    size_t c = base + ((size_t)part_id < remainder ? 1 : 0);
    *start = s;
    *end   = s + c;
    *count = c;
}

bool init_changes() {
    local_changes = (Change *)calloc(local_doc_count, sizeof(Change));
    global_changes = (Change *)calloc(local_doc_count * grid_cols, sizeof(Change));
    return local_changes && global_changes;
}

int assign_best_cabinet() {

    int swaps = 0;
    memset(local_sum, 0, cabinets.count * subject_count * sizeof(double));
    memset(local_count, 0, cabinets.count * sizeof(size_t));

    for (size_t d = 0; d < local_doc_count; d++) {
        size_t old_cab_idx = documents.parent_ids[doc_s + d];
        size_t new_cab_idx = old_cab_idx;
        double min_distance = INFINITY;

        for (int i = 0; i < grid_cols; i++) {
            Change *entry = &global_changes[i * local_doc_count + d];
            if (entry->distance < min_distance) {
                min_distance = entry->distance;
                new_cab_idx  = entry->cab_idx;
            }
        }

        if (new_cab_idx != old_cab_idx) swaps = 1;
        documents.parent_ids[doc_s + d] = new_cab_idx;

        local_count[new_cab_idx]++;
        for (int s = 0; s < subject_count; s++)
            local_sum[new_cab_idx * subject_count + s] +=
                documents.scores[(doc_s + d) * subject_count + s];
    }

    return swaps;
}

bool init_sums_counts() {

    local_count = calloc(cabinets.count, sizeof(size_t));
    local_sum = calloc(cabinets.count * subject_count, sizeof(double));

    global_count = calloc(cabinets.count, sizeof(size_t));
    global_sum = calloc(cabinets.count * subject_count, sizeof(double));

    return  local_count && local_sum && global_count && global_sum;
}