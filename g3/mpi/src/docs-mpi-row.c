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
#define UNIF01 ((double)rand() / RAND_MAX)

typedef struct Cabinets Cabinets;
typedef struct Documents Documents;
typedef struct Problem Problem;

struct Documents {
    size_t count;
    size_t *parent_ids;
    double *scores;
};

struct Cabinets {
    size_t count;
    size_t *doc_count;
    double *scores;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    double *document_scores;
};

size_t get_closest_cabinet_index(size_t parent_id, double *document_scores);
void assign_to_cabinets();
double calculate_distance(double *score_1, double *score_2);
int reassign_documents();
void increment_cabinet_scores(size_t cab_idx, size_t doc_idx);
void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx);
void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add);
void free_problem(Problem *p);
bool parse_problem(char *filename, Problem *p);
bool init_cabinets(Problem *problem);
bool init_documents(Problem *problem);
void print_result();
void split_documents_by_rank();
void gather_parent_ids();
void recompute_scores();
bool init_sums_and_counts();

Documents documents;
Cabinets cabinets;
size_t subject_count;

int rank;
int nprocs;
size_t doc_s;
size_t doc_e;

size_t *local_count;
double_t *local_sum;
size_t *global_count;
double *global_sum;

int nthreads;
size_t *thr_count;
double *thr_sum;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    double exec_time;
    int swaps = 0;

    Problem problem;
    problem.document_scores = NULL;

    if (!parse_problem(argv[1], &problem) || !init_documents(&problem) ||
        !init_cabinets(&problem))
        goto cleanup;

    subject_count = problem.subject_count;
    split_documents_by_rank();

    if (!init_sums_and_counts()) goto cleanup;

    int global_swaps = 0;

    exec_time = -omp_get_wtime();

    assign_to_cabinets();
    do {
        swaps = reassign_documents();

        MPI_Request request;
        MPI_Iallreduce(&swaps, &global_swaps, 1, MPI_INT, MPI_LOR,
                       MPI_COMM_WORLD, &request);
        recompute_scores();
        MPI_Wait(&request, MPI_STATUS_IGNORE);
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
    if (local_count) free(local_count);
    if (local_sum) free(local_sum);
    if (global_count) free(global_count);
    if (global_sum) free(global_sum);
    if (thr_count) free(thr_count);
    if (thr_sum) free(thr_sum);

    MPI_Finalize();
    return 0;
}

void gather_parent_ids() {
    int *recv_counts = NULL;
    int *displs = NULL;

    if (rank == 0) {
        recv_counts = malloc(nprocs * sizeof(int));
        displs = malloc(nprocs * sizeof(int));

        size_t base = documents.count / nprocs;
        size_t remainder = documents.count % nprocs;

        for (int i = 0; i < nprocs; i++) {
            recv_counts[i] = (int)(base + (i < remainder ? 1 : 0));
            displs[i] = (int)(i * base + (i < remainder ? i : remainder));
        }
    }

    MPI_Gatherv(&documents.parent_ids[doc_s], (int)(doc_e - doc_s),
                MPI_UNSIGNED_LONG, documents.parent_ids, recv_counts, displs,
                MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
}

void assign_to_cabinets() {
    size_t *cabinet_document_counts = calloc(cabinets.count, sizeof(size_t));
    if (!cabinet_document_counts) return;

#pragma omp parallel for schedule(static)
    for (size_t i = 0; i < documents.count; i++) {
        size_t cab_idx = i % cabinets.count;
        documents.parent_ids[i] = cab_idx;

#pragma omp atomic
        cabinet_document_counts[cab_idx]++;

        for (size_t j = 0; j < subject_count; j++) {
#pragma omp atomic
            cabinets.scores[cab_idx * subject_count + j] +=
                documents.scores[i * subject_count + j];
        }
    }

#pragma omp parallel for schedule(static)
    for (size_t cab_idx = 0; cab_idx < cabinets.count; cab_idx++) {
        size_t doc_count = cabinet_document_counts[cab_idx];
        for (size_t sub_index = 0; sub_index < subject_count; sub_index++) {
            if (doc_count != 0) {
                cabinets.scores[cab_idx * subject_count + sub_index] /=
                    (double)doc_count;
            } else {
                cabinets.scores[cab_idx * subject_count + sub_index] = 0.0;
            }
        }
    }

    free(cabinet_document_counts);
}

int reassign_documents() {
    int swaps = 0;
    memset(local_sum, 0, cabinets.count * subject_count * sizeof(double));
    memset(local_count, 0, cabinets.count * sizeof(size_t));

    memset(thr_count, 0, nthreads * cabinets.count * sizeof(size_t));
    memset(thr_sum, 0, nthreads * cabinets.count * subject_count * sizeof(double));

#pragma omp parallel for schedule(static) reduction(| : swaps)
    for (size_t d = doc_s; d < doc_e; d++) {
        int tid = omp_get_thread_num();
        size_t *my_count = &thr_count[tid * cabinets.count];
        double *my_sum = &thr_sum[tid * cabinets.count * subject_count];

        size_t old_cab_idx = documents.parent_ids[d];
        size_t new_cab_idx = get_closest_cabinet_index(
            documents.parent_ids[d], &documents.scores[d * subject_count]);

        if (new_cab_idx != old_cab_idx) swaps = 1;
        documents.parent_ids[d] = new_cab_idx;

        my_count[new_cab_idx]++;
        for (size_t s = 0; s < subject_count; s++)
            my_sum[new_cab_idx * subject_count + s] +=
                documents.scores[d * subject_count + s];
    }

#pragma omp parallel for schedule(static)
    for (size_t c = 0; c < cabinets.count; c++) {
        for (int t = 0; t < nthreads; t++) {
            local_count[c] += thr_count[t * cabinets.count + c];
            for (size_t s = 0; s < subject_count; s++)
                local_sum[c * subject_count + s] +=
                    thr_sum[t * cabinets.count * subject_count + c * subject_count + s];
        }
    }

    return swaps;
}

void recompute_scores() {
    MPI_Request request[2];
    MPI_Iallreduce(local_sum, global_sum, cabinets.count * subject_count,
                   MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD, &request[0]);
    MPI_Iallreduce(local_count, global_count, cabinets.count, MPI_UNSIGNED_LONG,
                   MPI_SUM, MPI_COMM_WORLD, &request[1]);
    MPI_Waitall(2, request, MPI_STATUS_IGNORE);

#pragma omp parallel for schedule(static)
    for (size_t c = 0; c < cabinets.count; c++)
        for (size_t s = 0; s < subject_count; s++)
            cabinets.scores[c * subject_count + s] =
                global_count[c] ? global_sum[c * subject_count + s] /
                                      (double)global_count[c]
                                : 0.0;
}

size_t get_closest_cabinet_index(size_t parent_id, double *document_scores) {
    double min_distance = INFINITY;

    for (size_t j = 0; j < cabinets.count; j++) {
        double distance = calculate_distance(
            &cabinets.scores[j * subject_count], document_scores);

        if (distance < min_distance) {
            min_distance = distance;
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

void split_documents_by_rank() {
    size_t base = documents.count / nprocs;
    size_t remainder = documents.count % nprocs;

    size_t count = base + (rank < remainder ? 1 : 0);
    doc_s = rank * base + (rank < remainder ? rank : remainder);
    doc_e = doc_s + count;
}

bool init_sums_and_counts() {
    local_count = calloc(cabinets.count, sizeof(size_t));
    local_sum = calloc(cabinets.count * subject_count, sizeof(double));

    global_count = calloc(cabinets.count, sizeof(size_t));
    global_sum = calloc(cabinets.count * subject_count, sizeof(double));

    nthreads = omp_get_max_threads();

    thr_count = calloc(nthreads * cabinets.count, sizeof(size_t));
    thr_sum = calloc(nthreads * cabinets.count * subject_count, sizeof(double));

    return local_sum && local_count && global_count && global_sum && thr_count && thr_sum;
}