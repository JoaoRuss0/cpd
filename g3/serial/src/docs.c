#include <math.h>
#include <omp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    double *scores;
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

void print_result(const Documents *documents);

int swaps = 0;
size_t *new_counts = NULL;
double *new_scores = NULL;

int main(const int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    double exec_time;

    Problem problem;
    if (!parse_problem(argv[1], &problem)) goto cleanup;

    Documents documents;
    documents.count = problem.document_count;
    documents.scores = problem.document_scores;
    documents.parent_ids = calloc(documents.count, sizeof(size_t));
    if (!documents.parent_ids) goto cleanup;

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.scores = (double *)calloc(
        problem.cabinet_count * problem.subject_count, sizeof(double));
    if (!cabinets.scores) goto cleanup;

    new_counts = (size_t *)calloc(problem.cabinet_count, sizeof(size_t));
    if (!new_counts) goto cleanup;
    new_scores = (double *)calloc(problem.cabinet_count * problem.subject_count,
                                  sizeof(double));
    if (!new_scores) goto cleanup;

    exec_time = -omp_get_wtime();

    assign_to_cabinets(&cabinets, &documents, problem.subject_count);
    do {
        recalculate_scores(&cabinets, problem.subject_count);
        reassign_documents(&cabinets, &documents, problem.subject_count);
    } while (swaps > 0);

    exec_time += omp_get_wtime();

    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(&documents);

cleanup:
    free_problem(&problem);
    if (cabinets.scores) free(cabinets.scores);
    if (new_counts) free(new_counts);
    if (new_scores) free(new_scores);
    if (documents.parent_ids) free(documents.parent_ids);

    return 0;
}

void assign_to_cabinets(const Cabinets *cabinets, const Documents *documents,
                        const size_t subject_count) {
    for (size_t i = 0; i < documents->count; i++) {
        const size_t index = i % cabinets->count;
        documents->parent_ids[i] = index;

        new_counts[index] += 1;
        for (size_t j = 0; j < subject_count; j++) {
            new_scores[index * subject_count + j] +=
                documents->scores[i * subject_count + j];
        }
    }
}

void recalculate_scores(const Cabinets *cabinets, const size_t subject_count) {
    memset(cabinets->scores, 0,
           cabinets->count * subject_count * sizeof(double));
    for (size_t c = 0; c < cabinets->count; c++) {
        double *score = &cabinets->scores[c * subject_count];
        if (new_counts[c] == 0) {
            memset(score, 0, subject_count * sizeof(double));
            continue;
        }

        for (size_t s = 0; s < subject_count; s++) {
            score[s] =
                new_scores[c * subject_count + s] / (double)new_counts[c];
        }
    }
}

void reassign_documents(const Cabinets *cabinets, const Documents *documents,
                        const size_t subject_count) {
    swaps = 0;
    memset(new_scores, 0, cabinets->count * subject_count * sizeof(double));
    memset(new_counts, 0, cabinets->count * sizeof(size_t));

    for (size_t i = 0; i < documents->count; i++) {
        const size_t old_cabinet_index = documents->parent_ids[i];
        const size_t new_cabinet_index = get_closest_cabinet_index(
            cabinets, documents->parent_ids[i],
            &documents->scores[i * subject_count], subject_count);

        if (new_cabinet_index != old_cabinet_index) {
            swaps += 1;
        }

        documents->parent_ids[i] = new_cabinet_index;
        new_counts[new_cabinet_index] += 1;

        for (size_t j = 0; j < subject_count; j++) {
            new_scores[new_cabinet_index * subject_count + j] +=
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
            subject_count, &cabinets->scores[j * subject_count],
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