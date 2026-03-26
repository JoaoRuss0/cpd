#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <omp.h>

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

Documents documents;
Cabinets cabinets;
size_t subject_count;

int rank;
int nprocs;
size_t doc_s;
size_t doc_e;

size_t* local_count;
double_t* local_sum;

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

    local_count = calloc(cabinets.count, sizeof(size_t));
    local_sum = calloc(cabinets.count * subject_count, sizeof(double));

    size_t* global_count = calloc(cabinets.count, sizeof(size_t));
    double* global_sum = calloc(cabinets.count * subject_count, sizeof(double));
    int global_swaps = 0;

    exec_time = -omp_get_wtime();

    assign_to_cabinets();
    do {
        swaps = reassign_documents();

        MPI_Allreduce(local_sum, global_sum, cabinets.count * subject_count, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(local_count, global_count, cabinets.count, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&swaps, &global_swaps, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);

        for (size_t c = 0; c < cabinets.count; c++)
            for (size_t s = 0; s < subject_count; s++)
                cabinets.scores[c * subject_count + s] = global_count[c] ? global_sum[c * subject_count + s] / (double) global_count[c] : 0.0;

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

    MPI_Gatherv(&documents.parent_ids[doc_s], (int)(doc_e - doc_s), MPI_UNSIGNED_LONG,
                documents.parent_ids, recv_counts, displs, MPI_UNSIGNED_LONG,
                0, MPI_COMM_WORLD);
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

            cabinets.scores[idx] =
                (cabinets.scores[idx] * (double) doc_count + documents.scores[i * subject_count + j])
                    / (double) new_doc_count;
        }
        cabinets.doc_count[cab_idx] = new_doc_count;
    }
}

int reassign_documents() {

    int swaps = 0;
    memset(local_sum, 0, cabinets.count * subject_count * sizeof(double));
    memset(local_count, 0, cabinets.count * sizeof(size_t));

    for (size_t d = doc_s; d < doc_e; d++) {
        size_t old_cab_idx = documents.parent_ids[d];
        size_t new_cab_idx = get_closest_cabinet_index(
            documents.parent_ids[d], &documents.scores[d * subject_count]);

        if (new_cab_idx != old_cab_idx) swaps = 1;

        documents.parent_ids[d] = new_cab_idx;

        local_count[new_cab_idx]++;
        for (int s = 0; s < subject_count; s++)
            local_sum[new_cab_idx * subject_count + s] +=
                documents.scores[d * subject_count + s];
    }

    return swaps;
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
    cabinets.scores =
        (double *)calloc(problem->cabinet_count * problem->subject_count, sizeof(double));
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

    for (size_t i = 0; i < p->document_count; i++) {
        if (!fgets(line, 1024, file)) {
            status = false;
            goto cleanup;
        }

        char *token = strtok(line, " ");
        if (token == NULL) {
            status = false;
            goto cleanup;
        }

        for (size_t j = 0; j < p->subject_count; j++) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                status = false;
                goto cleanup;
            }
            p->document_scores[p->subject_count * i + j] = strtod(token, NULL);
        }
    }

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
    size_t remainder  = documents.count % nprocs;

    size_t count = base + (rank < remainder ? 1 : 0);
    doc_s = rank * base + (rank < remainder ? rank : remainder);
    doc_e = doc_s + count;
}