//
// Created by:
//      João Russo  ist116543
//      Juan Viteri ist115766
//      Pedro Gomes ist1113468
//

#include <math.h>
#include <omp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
double calculate_distance(size_t subject_count, const double *score_1,
                          const double *score_2);
void free_problem(const Problem *p);
bool parse_problem(const char *filename, Problem *p);
void print_result(const Documents *documents);
int init_documents(const Problem *problem, Documents *documents);
int init_cabinets(Problem *problem, Cabinets *cabinets);

int swaps = 0;
size_t cab_count = 0;
size_t sub_count = 0;
size_t score_size = 0;

int main(const int argc, char *argv[]) {
    if (argc != 2) return 1;

    double exec_time;

    Problem problem;
    if (!parse_problem(argv[1], &problem)) return 1;

    cab_count = problem.cabinet_count;
    sub_count = problem.subject_count;
    score_size = cab_count * sub_count;

    Documents documents;
    Cabinets cabinets;
    if (init_documents(&problem, &documents)) {
        free_problem(&problem);
        return 1;
    }
    if (init_cabinets(&problem, &cabinets)) {
        free_problem(&problem);
        free(documents.parent_ids);
        return 1;
    }

    size_t counts[cab_count];
    double scores[score_size];
    memset(counts, 0, cab_count * sizeof(size_t));
    memset(scores, 0, score_size * sizeof(double));

    exec_time = -omp_get_wtime();

#pragma omp parallel for schedule(static) \
    reduction(+:counts[:cab_count]) \
    reduction(+:scores[:score_size])
    for (size_t i = 0; i < documents.count; i++) {
        const size_t cab = i % cab_count;
        documents.parent_ids[i] = cab;

        counts[cab] += 1;
        for (size_t j = 0; j < sub_count; j++)
            scores[cab * sub_count + j] +=
                documents.scores[i * sub_count + j];
    }

    do {
#pragma omp parallel for schedule(static)
        for (size_t c = 0; c < cab_count; c++) {
            double *score = &cabinets.scores[c * sub_count];
            if (counts[c] == 0) {
                memset(score, 0, sub_count * sizeof(double));
                continue;
            }
            for (size_t s = 0; s < sub_count; s++)
                score[s] = scores[c * sub_count + s] / (double)counts[c];
        }

        memset(counts, 0, cab_count * sizeof(size_t));
        memset(scores, 0, score_size * sizeof(double));
        swaps = 0;

#pragma omp parallel for schedule(static) \
    reduction(|:swaps) \
    reduction(+:counts[:cab_count]) \
    reduction(+:scores[:score_size])
        for (size_t i = 0; i < documents.count; i++) {
            const size_t old_cab = documents.parent_ids[i];
            const size_t new_cab = get_closest_cabinet_index(
                &cabinets, old_cab,
                &documents.scores[i * sub_count], sub_count);

            if (new_cab != old_cab)
                swaps = 1;

            documents.parent_ids[i] = new_cab;
            counts[new_cab] += 1;
            for (size_t j = 0; j < sub_count; j++)
                scores[new_cab * sub_count + j] +=
                    documents.scores[i * sub_count + j];
        }
    } while (swaps);

    exec_time += omp_get_wtime();

    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(&documents);

    free_problem(&problem);
    free(cabinets.scores);
    free(documents.parent_ids);
    return 0;
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
    for (size_t i = 0; i < documents->count; i++)
        printf("%zu\n", documents->parent_ids[i]);
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

int init_documents(const Problem *problem, Documents *documents) {
    documents->count = problem->document_count;
    documents->scores = problem->document_scores;
    documents->parent_ids = calloc(documents->count, sizeof(size_t));
    if (!documents->parent_ids) return 1;
    return 0;
}

int init_cabinets(Problem *problem, Cabinets *cabinets) {
    cabinets->count = problem->cabinet_count;
    cabinets->scores = (double *)calloc(
        cabinets->count * problem->subject_count, sizeof(double));
    if (!cabinets->scores) return 1;
    return 0;
}