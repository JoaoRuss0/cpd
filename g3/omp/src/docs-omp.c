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

struct Documents{
    size_t count;
    Document* inner;
    double* scores;
};

struct Cabinets{
    size_t count;
    double* scores;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    Document* documents;
    double* document_scores;
};

void assign_to_cabinets(Cabinets* cabinets, Documents* documents, const size_t subject_count);
double calculate_distance(size_t subject_count, const double* score_1, const double* score_2);
bool reassign_documents(Cabinets* cabinets, Documents* documents, size_t subject_count);

void free_problem(const Problem &p);

bool parse_problem(const char* filename, Problem* p);
void print_problem(const Problem &p);
void print_cabinets(const Cabinets &cabinets, size_t subject_count);
void print_result(const Documents &documents);

int main(const int argc, char *argv[]) {

    if (argc != 2) {
        return 1;
    }

    bool reassigned = false;
    double exec_time;

    Problem problem;
    if (!parse_problem(argv[1], &problem)) goto cleanup;

    Documents documents;
    documents.count = problem.document_count;
    documents.inner = problem.documents;
    documents.scores = problem.document_scores;

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.scores = static_cast<double*>(calloc(problem.cabinet_count * problem.subject_count, sizeof(double)));
    if(!cabinets.scores) goto cleanup;

    exec_time = -omp_get_wtime();

    assign_to_cabinets(&cabinets, &documents, problem.subject_count);
    do {
        reassigned = reassign_documents(&cabinets, &documents, problem.subject_count);
    } while (reassigned);

    exec_time += omp_get_wtime();
    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(documents);

    cleanup:
    free_problem(problem);
    free(cabinets.scores);

    return 0;
}

bool reassign_documents(Cabinets* cabinets, Documents* documents, size_t subject_count) {
    double* new_cabinet_subject_score_sum = (double*) calloc(cabinets->count * subject_count, sizeof(double));
    if (!new_cabinet_subject_score_sum) return false;

    size_t new_cabinet_document_count[cabinets->count];
    memset(new_cabinet_document_count, 0, sizeof(size_t) * cabinets->count);

    bool swaps = false;

# pragma omp parallel for reduction(|:swaps)

    for (size_t i = 0; i < documents->count; i++) {
        double min_distance = INFINITY;
        size_t new_cabinet_index = documents->inner[i].parent_id;

        for (size_t j = 0; j < cabinets->count; j++) {
            const double distance = calculate_distance(subject_count, &cabinets->scores[j * subject_count],
                                                 &documents->scores[i * subject_count]);
            if (distance < min_distance) {
                min_distance = distance;
                new_cabinet_index = j;
            }
        }

        swaps |= new_cabinet_index != documents->inner[i].parent_id;
        documents->inner[i].parent_id = new_cabinet_index;

#pragma omp critical
        {
            new_cabinet_document_count[new_cabinet_index] += 1;

#pragma omp simd
            for (size_t j = 0; j < subject_count; j++) {
                new_cabinet_subject_score_sum[new_cabinet_index * subject_count + j] += documents->scores[i * subject_count + j];
            }
        }
    }

# pragma omp parallel for
    for (size_t i = 0; i < cabinets->count; i++) {
        for (size_t j = 0; j < subject_count; j++) {
            if (new_cabinet_document_count[i] == 0) continue;
            cabinets->scores[i * subject_count + j] = new_cabinet_subject_score_sum[i * subject_count + j] / (double) new_cabinet_document_count[i];
        }
    }

    free(new_cabinet_subject_score_sum);
    return swaps;
}

double calculate_distance(size_t subject_count, const double* score_1, const double* score_2) {

    double sum = 0;
#pragma omp simd reduction(+:sum)
    for (size_t i = 0; i < subject_count; i++) {
        const double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }
    return sum;
}

void assign_to_cabinets(Cabinets* cabinets, Documents* documents, const size_t subject_count) {

    double* new_cabinet_subject_score_sum = (double*) calloc(cabinets->count * subject_count, sizeof(double));
    size_t new_cabinet_document_count[cabinets->count];
    memset(new_cabinet_document_count, 0, sizeof(size_t) * cabinets->count);

#pragma omp parallel for
        for (size_t i = 0; i < documents->count; i++) {
            const size_t index = i % cabinets->count;
            documents->inner[i].parent_id = index;

#pragma omp critical
            {
                new_cabinet_document_count[index] += 1;

#pragma omp simd
                for (size_t j = 0; j < subject_count; j++) {
                    new_cabinet_subject_score_sum[index * subject_count + j] += documents->scores[i * subject_count + j];
                }
            }
        }

    for (size_t i = 0; i < cabinets->count; i++) {
        for (size_t j = 0; j < subject_count; j++) {
            if (new_cabinet_document_count[i] == 0) continue;
            cabinets->scores[i * subject_count + j] = new_cabinet_subject_score_sum[i * subject_count + j] / (double) new_cabinet_document_count[i];
        }
    }
}

void print_result(const Documents &documents) {

    for (size_t i = 0; i < documents.count; i++) {
        printf("%zu\n", documents.inner[i].parent_id);
    }
}

bool parse_problem(const char* filename, Problem* p) {

    bool status = true;
    FILE* file = fopen(filename, "r");
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

    p->documents = static_cast<Document *>(calloc(p->document_count, sizeof(Document)));
    if (!p->documents) goto cleanup;
    p->document_scores = static_cast<double *>(calloc(p->document_count * p->subject_count, sizeof(double)));
    if (!p->document_scores) goto cleanup;

    for(size_t i = 0; i < p->document_count; i++) {

        if (!fgets(line, 1024, file)) {
            status = false;
            goto cleanup;
        }

        char* token = strtok(line, " ");
        if (token == NULL) {
            status = false;
            goto cleanup;
        }

        p->documents[i].id = i;

        for(size_t j = 0; j < p->subject_count ; j++) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                status = false;
                goto cleanup;
            }
            p->document_scores[p->subject_count * i + j] = strtod(token, NULL);
        }
    }

    cleanup:
    if (file) {
        fclose(file);
    }

    if (!status) free_problem(*p);

    return status;
}

void free_problem(const Problem &p) {
    if (p.document_scores) free(p.document_scores);
    if (p.documents) free(p.documents);
}

void print_cabinets(const Cabinets &cabinets, size_t subject_count) {
    printf("CABINETS:\n");

    for (size_t i = 0; i < cabinets.count; i++) {
        printf("\t%zu ->", i);

        printf("\n\tScore ->");
        for (size_t j = 0; j < subject_count; j++) {
            printf(" %f", cabinets.scores[j]);
        }
        printf("\n");
    }
}

void print_problem(const Problem &p) {
    printf("Cabinets: %zu\n", p.cabinet_count);
    printf("Documents: %zu\n", p.document_count);
    printf("Subjects: %zu\n", p.subject_count);

    printf("Documents:\n");
    for (size_t i = 0; i < p.document_count; i++) {
        printf("\tId: %zu \tScores:", i);
        for (size_t j = 0; j < p.subject_count; j++) {
            printf(" %lf", p.document_scores[i * p.subject_count + j]);
        }
        printf("\n");
    }
    printf("\n");
}