#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>

typedef struct Document Document;
typedef struct Cabinets Cabinets;
typedef struct Documents Documents;
typedef struct Problem Problem;

struct Document {
    size_t id;
    size_t parent_id;
};

struct Documents {
    size_t count;
    Document *inner;
    double *scores;
};

struct Cabinets {
    size_t count;
    double *scores;
    double *scores_sum;
    size_t *document_counts;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    Document *documents;
    double *document_scores;
};

size_t get_closest_cabinet_index(const Cabinets *cabinets, Document *document, double *document_scores,
                                 size_t subject_count);

void recalculate_scores(const Cabinets *cabinets, size_t subject_count);

void assign_to_cabinets(const Cabinets *cabinets, const Documents *documents, size_t subject_count);

double calculate_distance(size_t subject_count, const double *score_1, const double *score_2);

void reassign_documents(const Cabinets *cabinets, const Documents *documents, size_t subject_count);

void free_problem(const Problem *p);

bool parse_problem(const char *filename, Problem *p);

void print_result(const Documents *documents);

int swaps = 0;

int main(const int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    double exec_time;

    Problem problem;
    if (!parse_problem(argv[1], &problem)) goto cleanup;

    Documents documents;
    documents.count = problem.document_count;
    documents.inner = problem.documents;
    documents.scores = problem.document_scores;

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.scores = (double *) calloc(problem.cabinet_count * problem.subject_count, sizeof(double));
    if (!cabinets.scores) goto cleanup;
    cabinets.scores_sum = (double *) calloc(problem.cabinet_count * problem.subject_count, sizeof(double));
    if (!cabinets.scores_sum) goto cleanup;
    cabinets.document_counts = (size_t *) calloc(problem.cabinet_count, sizeof(size_t));
    if (!cabinets.document_counts) goto cleanup;

    exec_time = -omp_get_wtime();

#pragma omp parallel
    {
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
    if (cabinets.scores) free(cabinets.scores);
    if (cabinets.scores_sum) free(cabinets.scores_sum);
    if (cabinets.document_counts) free(cabinets.document_counts);

    return 0;
}

void assign_to_cabinets(const Cabinets *cabinets, const Documents *documents, const size_t subject_count) {
    // We can not use "nowait" since we need to know all the documents that belong to a cabinet before calculating its scores
#pragma omp for
    for (size_t i = 0; i < documents->count; i++) {
        const size_t index = i % cabinets->count;
        documents->inner[i].parent_id = index;

#pragma omp atomic
        cabinets->document_counts[index] += 1;

        for (size_t j = 0; j < subject_count; j++) {
#pragma omp atomic
            cabinets->scores_sum[index * subject_count + j] += documents->scores[i * subject_count + j];
        }
    }
}

void recalculate_scores(const Cabinets *cabinets, const size_t subject_count) {
    // We can not use "nowait" since we need to know the cabinet scores before calculating if a document is to be moved

#pragma omp for
    for (size_t i = 0; i < cabinets->count; i++) {
        for (size_t j = 0; j < subject_count; j++) {
            if (cabinets->document_counts[i] == 0) {
                memset(&cabinets->scores[i*subject_count], 0, subject_count * sizeof(double));
                break;
            }
            cabinets->scores[i * subject_count + j] =
                    cabinets->scores_sum[i * subject_count + j] / (double) cabinets->document_counts[i];
        }
    }
}

void reassign_documents(const Cabinets *cabinets, const Documents *documents, const size_t subject_count) {
#pragma omp barrier
#pragma omp single
    swaps = 0;

    // We have to wait for all swaps to be summed up, since a single thread's documents might not be moved (hence swaps = 0)
    // but another thread's assigned documents might, and we would be quitting the while cycle if we used "nowait"
#pragma omp for reduction(+:swaps)
    for (size_t i = 0; i < documents->count; i++) {
        const size_t old_cabinet_index = documents->inner[i].parent_id;
        const size_t new_cabinet_index = get_closest_cabinet_index(cabinets, &documents->inner[i],
                                                                   &documents->scores[i * subject_count],
                                                                   subject_count);
        if (new_cabinet_index == old_cabinet_index) {
            continue;
        }

        swaps += 1;
        documents->inner[i].parent_id = new_cabinet_index;

#pragma omp atomic
        cabinets->document_counts[new_cabinet_index] += 1;
#pragma omp atomic
        cabinets->document_counts[old_cabinet_index] -= 1;

        for (size_t j = 0; j < subject_count; j++) {
#pragma omp atomic
            cabinets->scores_sum[new_cabinet_index * subject_count + j] += documents->scores[
                i * subject_count + j];
#pragma omp atomic
            cabinets->scores_sum[old_cabinet_index * subject_count + j] -= documents->scores[
                i * subject_count + j];
        }
    }
}

size_t get_closest_cabinet_index(const Cabinets *cabinets, Document *document, double *document_scores,
                                 size_t subject_count) {
    double min_distance = INFINITY;
    size_t new_cabinet_index = document->parent_id;

    for (size_t j = 0; j < cabinets->count; j++) {
        const double distance =
                calculate_distance(subject_count, &cabinets->scores[j * subject_count], document_scores);

        if (distance < min_distance) {
            min_distance = distance;
            new_cabinet_index = j;
        }
    }

    return new_cabinet_index;
}

double calculate_distance(const size_t subject_count, const double *score_1, const double *score_2) {
    double sum = 0;

    for (size_t i = 0; i < subject_count; i++) {
        const double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }

    return sum;
}

void print_result(const Documents *documents) {
    for (size_t i = 0; i < documents->count; i++) {
        printf("%zu\n", documents->inner[i].parent_id);
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
        !sscanf(line, "%zu %zu %zu", &p->cabinet_count, &p->document_count, &p->subject_count)) {
        status = false;
        goto cleanup;
    }

    p->documents = (Document *) calloc(p->document_count, sizeof(Document));
    if (!p->documents) goto cleanup;
    p->document_scores = (double *) calloc(p->document_count * p->subject_count, sizeof(double));
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

        p->documents[i].id = i;

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
    if (file)fclose(file);
    if (!status) free_problem(p);

    return status;
}

void free_problem(const Problem *p) {
    if (p->document_scores) free(p->document_scores);
    if (p->documents) free(p->documents);
}
