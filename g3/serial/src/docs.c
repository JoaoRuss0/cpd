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
    size_t *document_count;
    double *sub_score_sum;
    double *sub_scores;
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
bool reassign_documents();
void increment_cabinet_scores(size_t cab_idx, size_t doc_idx);
void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx);
void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add);
void recalculate_centroids();
void free_problem(Problem *p);
bool parse_problem(char *filename, Problem *p);
bool init_cabinets(Problem *problem);
bool init_documents(Problem *problem);
void print_result();

Documents documents;
Cabinets cabinets;
size_t subject_count;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    double exec_time;
    bool swaps = false;

    Problem problem;
    if (!parse_problem(argv[1], &problem) || !init_documents(&problem) ||
        !init_cabinets(&problem))
        goto cleanup;

    subject_count = problem.subject_count;

    exec_time = -omp_get_wtime();

    assign_to_cabinets();
    do {
        swaps = reassign_documents();
    } while (swaps);

    exec_time += omp_get_wtime();

    fprintf(stderr, "%.1fs\n", exec_time);
    print_result();

cleanup:
    free_problem(&problem);
    if (cabinets.document_count) free(cabinets.document_count);
    if (cabinets.sub_score_sum) free(cabinets.sub_score_sum);
    if (cabinets.sub_scores) free(cabinets.sub_scores);
    if (documents.parent_ids) free(documents.parent_ids);

    return 0;
}

void assign_to_cabinets() {
    for (size_t i = 0; i < documents.count; i++) {
        size_t cab_idx = i % cabinets.count;

        documents.parent_ids[i] = cab_idx;
        increment_cabinet_scores(cab_idx, i);
    }
    recalculate_centroids();
}

bool reassign_documents() {
    bool swaps = false;
    for (size_t i = 0; i < documents.count; i++) {
        size_t old_cab_idx = documents.parent_ids[i];
        size_t new_cab_idx = get_closest_cabinet_index(
            documents.parent_ids[i], &documents.scores[i * subject_count]);

        if (new_cab_idx == old_cab_idx) continue;
        swaps |= true;
        documents.parent_ids[i] = new_cab_idx;
        increment_cabinet_scores(new_cab_idx, i);
        decrement_cabinet_scores(old_cab_idx, i);
    }
    recalculate_centroids();

    return swaps;
}

void increment_cabinet_scores(size_t cab_idx, size_t doc_idx) {
    update_cabinets(cab_idx, doc_idx, 1);
}

void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx) {
    update_cabinets(cab_idx, doc_idx, -1);
}

void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add) {
    cabinets.document_count[cab_idx] += to_add;
    for (size_t j = 0; j < subject_count; j++) {
        cabinets.sub_score_sum[cab_idx * subject_count + j] +=
            to_add * documents.scores[doc_idx * subject_count + j];
    }
}

void recalculate_centroids() {
    for (size_t i = 0; i < cabinets.count; i++) {
        for (size_t j = 0; j < subject_count; j++) {
            if (cabinets.document_count[i] == 0) {
                cabinets.sub_scores[i * subject_count + j] = 0.0;
                continue;
            }
            cabinets.sub_scores[i * subject_count + j] =
                cabinets.sub_score_sum[i * subject_count + j] /
                cabinets.document_count[i];
        }
    }
}

size_t get_closest_cabinet_index(size_t parent_id, double *document_scores) {
    double min_distance = INFINITY;

    for (size_t j = 0; j < cabinets.count; j++) {
        double distance = calculate_distance(
            &cabinets.sub_scores[j * subject_count], document_scores);

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
    cabinets.sub_scores = (double *)calloc(
        problem->cabinet_count * problem->subject_count, sizeof(double));
    cabinets.document_count = (size_t *)calloc(cabinets.count, sizeof(size_t));
    cabinets.sub_score_sum =
        (double *)calloc(cabinets.count * problem->subject_count, sizeof(double));

    return cabinets.sub_scores && cabinets.document_count;
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