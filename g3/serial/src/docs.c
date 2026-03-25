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
    size_t *doc_count;
    double *temp_scores;
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
bool reassign_documents();
void increment_cabinet_scores(size_t cab_idx, size_t doc_idx);
void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx);
void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add);
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
    if (cabinets.doc_count) free(cabinets.doc_count);
    if (cabinets.temp_scores) free(cabinets.temp_scores);
    if (cabinets.scores) free(cabinets.scores);
    if (documents.parent_ids) free(documents.parent_ids);

    return 0;
}

void assign_to_cabinets() {
    for (size_t i = 0; i < documents.count; i++) {
        increment_cabinet_scores(i % cabinets.count, i);
    }
    memcpy(cabinets.scores, cabinets.temp_scores, cabinets.count * subject_count * sizeof(double));
}

bool reassign_documents() {
    bool swaps = false;
    for (size_t i = 0; i < documents.count; i++) {
        size_t old_cab_idx = documents.parent_ids[i];
        size_t new_cab_idx = get_closest_cabinet_index(
            documents.parent_ids[i], &documents.scores[i * subject_count]);

        if (new_cab_idx == old_cab_idx) continue;
        swaps |= true;
        increment_cabinet_scores(new_cab_idx, i);
        decrement_cabinet_scores(old_cab_idx, i);
    }
    memcpy(cabinets.scores, cabinets.temp_scores, cabinets.count * subject_count * sizeof(double));

    return swaps;
}

void increment_cabinet_scores(size_t cab_idx, size_t doc_idx) {
    update_cabinets(cab_idx, doc_idx, 1);
}

void decrement_cabinet_scores(size_t cab_idx, size_t doc_idx) {
    update_cabinets(cab_idx, doc_idx, -1);
}

void update_cabinets(size_t cab_idx, size_t doc_idx, int to_add) {
    size_t doc_count = cabinets.doc_count[cab_idx];
    size_t new_doc_count = (to_add > 0) ? doc_count + 1 : doc_count - 1;

    for (size_t j = 0; j < subject_count; j++) {
        size_t idx = cab_idx * subject_count + j;

        if (new_doc_count == 0) {
            cabinets.temp_scores[idx] = 0;
            continue;
        }

        cabinets.temp_scores[idx] =
            (cabinets.temp_scores[idx] * (double) doc_count + (double) to_add * documents.scores[doc_idx * subject_count + j])
                / (double) new_doc_count;
    }
    cabinets.doc_count[cab_idx] = new_doc_count;
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
    cabinets.temp_scores =
        (double *)calloc(problem->cabinet_count * problem->subject_count, sizeof(double));
    cabinets.doc_count = (size_t *)calloc(cabinets.count, sizeof(size_t));

    return cabinets.scores && cabinets.doc_count && cabinets.temp_scores;
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