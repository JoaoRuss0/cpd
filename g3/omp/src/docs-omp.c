#include <math.h>
#include <omp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_LINE_SIZE_IN_BYTES 64
#define SIZE_T_PER_CACHE_LINE (CACHE_LINE_SIZE_IN_BYTES / sizeof(size_t))
#define DOUBLE_PER_CACHE_LINE (CACHE_LINE_SIZE_IN_BYTES / sizeof(double))

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
    double *sub_scores;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    double *document_scores;
};

size_t get_closest_cabinet_index(const Cabinets *cabinets, size_t parent_id,
                                 double *document_scores, size_t subject_count);

void recalculate_scores(const Cabinets *cabinets, size_t subject_count);

void assign_to_cabinets(const Cabinets *cabinets, const Documents *documents,
                        size_t subject_count);

double calculate_distance(size_t subject_count, const double *score_1,
                          const double *score_2);

void reassign_documents(const Cabinets *cabinets, const Documents *documents,
                        size_t subject_count);

void free_problem(const Problem *p);

bool parse_problem(const char *filename, Problem *p);

void *get_cache_aligned_pointer(size_t size);

inline size_t get_allocation_size(size_t single_count, size_t block_size);

void print_result(const Documents *documents);

int init_documents(const Problem *problem, Documents *documents);

int init_cabinets(Problem *problem, Cabinets *cabinets);

int init_auxiliary_arrays(Problem *problem);

int swaps = 0;
size_t *new_counts = NULL;
double *new_scores = NULL;

size_t padded_count_per_thread;
size_t padded_cabinet_subject_scores_per_thread;
size_t padded_subject_scores;

int n_threads = 0;
int thread_number = 0;
#pragma omp threadprivate(thread_number)

int main(const int argc, char *argv[]) {
    if (argc != 2) return 1;

    double exec_time;

    Problem problem;
    if (!parse_problem(argv[1], &problem)) goto cleanup;

    n_threads = omp_get_max_threads();

    Documents documents;
    Cabinets cabinets;
    if (init_documents(&problem, &documents)) goto cleanup;
    if (init_cabinets(&problem, &cabinets)) goto cleanup;
    if (init_auxiliary_arrays(&problem)) goto cleanup;

    exec_time = -omp_get_wtime();

#pragma omp parallel
    {
        thread_number = omp_get_thread_num();

        assign_to_cabinets(&cabinets, &documents, problem.subject_count);
        do {
            recalculate_scores(&cabinets, problem.subject_count);
            reassign_documents(&cabinets, &documents, problem.subject_count);
        } while (swaps > 0);
    }

    exec_time += omp_get_wtime();

    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(&documents);

cleanup:
    free_problem(&problem);
    if (cabinets.sub_scores) free(cabinets.sub_scores);
    if (new_counts) free(new_counts);
    if (new_scores) free(new_scores);
    if (documents.parent_ids) free(documents.parent_ids);

    return 0;
}

void assign_to_cabinets(const Cabinets *cabinets, const Documents *documents,
                        const size_t subject_count) {
#pragma omp for schedule(static)
    for (size_t i = 0; i < documents->count; i++) {
        const size_t index = i % cabinets->count;
        documents->parent_ids[i] = index;

        new_counts[thread_number * padded_count_per_thread + index] += 1;
        for (size_t j = 0; j < subject_count; j++) {
            new_scores[thread_number *
                           padded_cabinet_subject_scores_per_thread +
                       index * subject_count + j] +=
                documents->scores[i * subject_count + j];
        }
    }
}

void recalculate_scores(const Cabinets *cabinets, const size_t subject_count) {
#pragma omp for schedule(static)
    for (size_t c = 0; c < cabinets->count; c++) {
        size_t count = 0;
        for (size_t t = 0; t < n_threads; t++) {
            count += new_counts[t * padded_count_per_thread + c];
        }

        double *score = &cabinets->sub_scores[c * padded_subject_scores];
        if (count == 0) {
            memset(score, 0, subject_count * sizeof(double));
            continue;
        }

        for (size_t s = 0; s < subject_count; s++) {
            double sum = 0.0;
            for (size_t t = 0; t < n_threads; t++) {
                sum += new_scores[t * padded_cabinet_subject_scores_per_thread +
                                  c * subject_count + s];
            }
            score[s] = sum / (double)count;
        }
    }
}

void reassign_documents(const Cabinets *cabinets, const Documents *documents,
                        const size_t subject_count) {
#pragma omp single
    swaps = 0;

    memset(
        &new_scores[thread_number * padded_cabinet_subject_scores_per_thread],
        0, padded_cabinet_subject_scores_per_thread * sizeof(double));
    memset(&new_counts[thread_number * padded_count_per_thread], 0,
           padded_count_per_thread * sizeof(size_t));

#pragma omp for schedule(static) reduction(+ : swaps)
    for (size_t i = 0; i < documents->count; i++) {
        const size_t old_cabinet_index = documents->parent_ids[i];
        const size_t new_cabinet_index = get_closest_cabinet_index(
            cabinets, documents->parent_ids[i],
            &documents->scores[i * subject_count], subject_count);

        if (new_cabinet_index != old_cabinet_index) {
            swaps += 1;
        }

        documents->parent_ids[i] = new_cabinet_index;
        new_counts[thread_number * padded_count_per_thread +
                   new_cabinet_index] += 1;

        for (size_t j = 0; j < subject_count; j++) {
            new_scores[thread_number *
                           padded_cabinet_subject_scores_per_thread +
                       new_cabinet_index * subject_count + j] +=
                documents->scores[i * subject_count + j];
        }
    }
}

size_t get_closest_cabinet_index(const Cabinets *cabinets, size_t parent_id,
                                 double *document_scores,
                                 size_t subject_count) {
    double min_distance = INFINITY;

    for (size_t j = 0; j < cabinets->count; j++) {
        const double distance = calculate_distance(
            subject_count, &cabinets->sub_scores[j * padded_subject_scores],
            document_scores);

        if (distance < min_distance) {
            min_distance = distance;
            parent_id = j;
        }
    }

    return parent_id;
}

double calculate_distance(const size_t subject_count, const double *score_1,
                          const double *score_2) {
    double sum = 0;

    for (size_t i = 0; i < subject_count; i++) {
        const double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }

    return sum;
}

void print_result(const Documents *documents) {
    for (size_t i = 0; i < documents->count; i++) {
        printf("%zu\n", documents->parent_ids[i]);
    }
}

bool parse_problem(const char *filename, Problem *p) {
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

void free_problem(const Problem *p) {
    if (p->document_scores) free(p->document_scores);
}
void *get_cache_aligned_pointer(size_t size) {
    void *pointer = NULL;

    if (posix_memalign(&pointer, CACHE_LINE_SIZE_IN_BYTES, size) != 0) {
        return NULL;
    }

    memset(pointer, 0, size);
    return pointer;
}

inline size_t get_allocation_size(size_t single_count, size_t block_size) {
    return ((block_size + single_count - 1) / block_size) * block_size;
}

int init_documents(const Problem *problem, Documents *documents) {
    documents->count = problem->document_count;
    documents->scores = problem->document_scores;

    documents->parent_ids = calloc(documents->count, sizeof(size_t));
    if (!documents->parent_ids) return 1;
    return 0;
}

int init_cabinets(Problem *problem, Cabinets *cabinets) {
    cabinets->count = problem->cabinet_count;

    padded_subject_scores =
        get_allocation_size(problem->subject_count, DOUBLE_PER_CACHE_LINE);
    cabinets->sub_scores = (double *)get_cache_aligned_pointer(
        cabinets->count * padded_subject_scores * sizeof(double));
    if (!cabinets->sub_scores) return 1;
    return 0;
}

int init_auxiliary_arrays(Problem *problem) {
    padded_count_per_thread =
        get_allocation_size(problem->cabinet_count, SIZE_T_PER_CACHE_LINE);
    new_counts = (size_t *)get_cache_aligned_pointer(
        n_threads * padded_count_per_thread * sizeof(size_t));
    if (!new_counts) return 1;

    padded_cabinet_subject_scores_per_thread = get_allocation_size(
        problem->cabinet_count * problem->subject_count, DOUBLE_PER_CACHE_LINE);
    new_scores = (double *)get_cache_aligned_pointer(
        n_threads * padded_cabinet_subject_scores_per_thread * sizeof(double));
    if (!new_scores) return 1;
    return 0;
}