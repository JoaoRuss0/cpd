#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>

typedef struct Cabinet Cabinet;

typedef struct {
    size_t id;
    double* scores;
    Cabinet* parent;
} Document;

struct Cabinet {
    Document** documents;
    size_t occupied;
    size_t amount;
    double* scores;
};

typedef struct {
    size_t count;
    Cabinet** inner;
} Cabinets;

typedef struct {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    Document** documents;
} Problem;

void free_cabinets(Cabinets cabinets);
void print_cabinets(Cabinets cabinets, size_t subject_count);
void assign_to_cabinets(Cabinets cabinets, Document** documents, size_t document_count);
void assign_to_cabinet(Cabinet* cabinet, Document* document);

void calculate_cabinet_averages(Cabinets cabinets, size_t subject_count);
double calculate_distance(size_t subject_count, double* score_1, double* score_2);
bool reassign_documents(Cabinets cabinets, size_t subject_count);
void move_document_to_cabinet(Document* document, Cabinet* to_cabinet);

bool parse_problem(char* filename, Problem* p);
void free_problem(Problem p);
void print_problem(Problem p);

int main(const int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments.");
        return 1;
    }

    Problem problem;
    bool status = parse_problem(argv[1], &problem);
    if (!status) {
        return 1;
    }

    print_problem(problem);

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.inner = malloc(problem.cabinet_count * sizeof(Cabinet*));
    if (cabinets.inner == NULL) goto cleanup;

    for (size_t i = 0; i < problem.cabinet_count; i++) {
        cabinets.inner[i] = malloc(sizeof(Cabinet));
        if (cabinets.inner[i] == NULL) goto cleanup;
        cabinets.inner[i]->scores = malloc(problem.subject_count * sizeof(double));
        if (cabinets.inner[i]->scores == NULL) goto cleanup;
    }

    assign_to_cabinets(cabinets, problem.documents, problem.document_count);
    calculate_cabinet_averages(cabinets, problem.subject_count);
    print_cabinets(cabinets, problem.subject_count);

    reassign_documents(cabinets, problem.subject_count);

    cleanup:
    free_problem(problem);
    free_cabinets(cabinets);

    return 0;
}

bool reassign_documents(Cabinets cabinets, size_t subject_count) {
    size_t occupied = 0;
    size_t amount = 0;
    Cabinet** to_cabinet = NULL;
    Document** to_reassign = NULL;

    for (size_t i = 0; i < cabinets.count; i++) {
        for (size_t j = 0; j < cabinets.inner[i]->occupied; j++) {
            double min_distance = INFINITY;
            size_t new_cabinet_index = 0;

            for (size_t k = 0; k < cabinets.count; k++) {
                double distance = calculate_distance(subject_count, cabinets.inner[k]->scores, cabinets.inner[i]->documents[j]->scores);
                if (distance < min_distance) {
                    min_distance = distance;
                    new_cabinet_index = k;
                }
            }

            if (new_cabinet_index == i) {
                continue;
            }

            if (occupied == amount) {
                size_t new_size;
                if (amount == 0) {new_size = 1;} else {new_size = 2 * amount;}
                to_reassign = realloc(to_reassign, new_size * sizeof(Document*));
                to_cabinet = realloc(to_cabinet, new_size * sizeof(Document*));
                amount = new_size;

                if (to_reassign == NULL || to_cabinet == NULL) {
                    fprintf(stderr, "Failed to allocate memory for reassigning documents.");
                    goto cleanup;
                }

            }

            to_reassign[occupied] = cabinets.inner[i]->documents[j];
            to_cabinet[occupied] = cabinets.inner[new_cabinet_index];
            printf("moving document %zu to cabinet %zu\n", cabinets.inner[i]->documents[j]->id, new_cabinet_index);
            occupied++;
        }
    }

    for (size_t i = 0; i < occupied; i++) {
        move_document_to_cabinet(to_reassign[i], to_cabinet[i]);
    }

    cleanup:
    free(to_reassign);
    free(to_cabinet);

    return occupied > 0;
}

void move_document_to_cabinet(Document* document, Cabinet* to_cabinet) {

}

double calculate_distance(size_t subject_count, double* score_1, double* score_2) {

    double sum = 0;
    for (size_t i = 0; i < subject_count; i++) {
        double diff = score_1[i] - score_2[i];
        sum += pow(diff, 2);
    }
    return sqrt(sum);
}

void calculate_cabinet_averages(Cabinets cabinets, size_t subject_count) {
    for (size_t i = 0; i < cabinets.count; i++) {
        double* score_sum = malloc(subject_count * sizeof(double));
        for (size_t j = 0; j < cabinets.inner[i]->occupied; j++) {
            for (size_t k = 0; k < subject_count; k++) {
                score_sum[k] += cabinets.inner[i]->documents[j]->scores[k];
            }
        }

        for (size_t k = 0; k < subject_count; k++) {
            cabinets.inner[i]->scores[k] = score_sum[k]/(double) cabinets.inner[i]->occupied;
        }

        free(score_sum);
    }
}

void free_cabinets(Cabinets cabinets) {
    for (size_t i = 0; i < cabinets.count; i++) {
        if (cabinets.inner[i] == NULL) {
            continue;
        }
        free(cabinets.inner[i]->documents);
        free(cabinets.inner[i]->scores);
        free(cabinets.inner[i]);
    }
    free(cabinets.inner);
}

void print_cabinets(Cabinets cabinets, size_t subject_count) {
    printf("CABINETS:\n");

    for (size_t i = 0; i < cabinets.count; i++) {
        printf("\t%zu ->", i);
        for (size_t j = 0; j < cabinets.inner[i]->occupied; j++) {
            printf(" %zu", cabinets.inner[i]->documents[j]->id);
        }

        printf("\n\tScore ->");
        for (size_t j = 0; j < subject_count; j++) {
            printf(" %f", cabinets.inner[i]->scores[j]);
        }
        printf("\n");
    }
}

void assign_to_cabinets(Cabinets cabinets, Document** documents, size_t document_count) {

    for (size_t i = 0; i < document_count; i++) {
        assign_to_cabinet(cabinets.inner[i % cabinets.count], documents[i]);
    }
}

void assign_to_cabinet(Cabinet* cabinet, Document* document) {

    if (cabinet->amount == cabinet->occupied) {
        size_t new_size;
        if (cabinet->occupied == 0) {new_size = 1;} else {new_size = 2 * cabinet->amount;}
        cabinet->documents = realloc(cabinet->documents, new_size * sizeof(Document*));
        cabinet->amount = new_size;
    }
    cabinet->documents[cabinet->occupied] = document;
    document->parent = cabinet;
    cabinet->occupied++;
}

bool parse_problem(char* filename, Problem* p) {

    bool status = true;
    FILE* file = fopen(filename, "r");
    if (!file) {
        status = false;
        fprintf(stderr, "Error opening file '%s' for reading: %s\n", filename,
            strerror(errno));
        goto cleanup;
    }

    char line[1024];
    if (!fgets(line, 1024, file) ||
        !sscanf(line, "%zu %zu %zu", &p->cabinet_count, &p->document_count, &p->subject_count)) {
        status = false;
        fprintf(stderr, "Error reading first line of file\n");
        goto cleanup;
    }

    p->documents = malloc(p->document_count * sizeof(Document*));
    if (p->documents == NULL) goto cleanup;

    for(size_t i = 0; i < p->document_count; i++) {

        if (!fgets(line, 1024, file)) {
            status = false;
            fprintf(stderr, "Error %zu-th document line\n", i);
            goto cleanup;
        }

        char* token = strtok(line, " ");
        if (token == NULL) {
            status = false;
            fprintf(stderr, "Error extracting id token from %zu-th document line\n", i);
            goto cleanup;
        }

        Document* d = malloc(sizeof(Document));
        if (d == NULL) goto cleanup;
        d->id = atoi(token);
        d->scores = malloc(p->subject_count * sizeof(double));

        for(size_t j = 0; j < p->subject_count ; j++) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                status = false;
                fprintf(stderr, "Error extracting %zu-th score token from %zu-th"
                    " document line\n", j, i);
                goto cleanup;
            }
            d->scores[j] = strtod(token, NULL);
        }

        p->documents[i] = d;
    }

    cleanup:
    if (file) {
        fprintf(stderr, "Closing file ...\n");
        fclose(file);
    }

    if (!status) free_problem(*p);

    return status;
}

void free_problem(Problem p) {
    printf("Closing problem ...\n");

    if (!p.documents) return;

    for (size_t i = 0; i < p.document_count; i++) {
        if (p.documents[i] != NULL) {
            continue;
        }
        free(p.documents[i]->scores);
        free(p.documents[i]);
    }

    free(p.documents);
}

void print_problem(Problem p) {
    printf("Cabinets: %zu\n", p.cabinet_count);
    printf("Documents: %zu\n", p.document_count);
    printf("Subjects: %zu\n", p.subject_count);

    printf("Documents:\n");
    for (size_t i = 0; i < p.document_count; i++) {
        printf("\tId: %zu \tScores:", p.documents[i]->id);
        for (size_t j = 0; j < p.subject_count; j++) {
            printf(" %lf", p.documents[i]->scores[j]);
        }
        printf("\n");
    }
}
