#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>

typedef struct {
    size_t id;
    double* scores;
} Document;

typedef struct {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    Document** documents;
} Problem;

typedef struct {
    Document** documents;
    size_t occupied;
    size_t amount;
} Cabinet;

void free_cabinets(Cabinet** cabinets, size_t count);
void print_cabinets(Cabinet** cabinets, size_t count);
void assign_to_cabinets(Cabinet** cabinets, Problem* problem);
void assign_to_cabinet(Cabinet* cabinet, Document* document);

bool parse_problem(char* filename, Problem* p);
void free_problem(Problem* p);
void print_problem(Problem* p);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments.");
        return 1;
    }

    Problem problem;
    bool status = parse_problem(argv[1], &problem);
    if (!status) {
        return 1;
    }

    Cabinet** cabinets = malloc(problem.cabinet_count * sizeof(Cabinet*));
    if (cabinets == NULL) goto cleanup;
    for (size_t i = 0; i < problem.cabinet_count; i++) {
        cabinets[i] = malloc(sizeof(Cabinet));
        if (cabinets[i] == NULL) goto cleanup;
    }

    assign_to_cabinets(cabinets, &problem);
    //do {
    //    calculate_cabinet_averages();
    //} while(reassign_documents());

    cleanup:
    free_problem(&problem);
    free_cabinets(cabinets, problem.cabinet_count);

    return 0;
}

void free_cabinets(Cabinet** cabinets, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (cabinets[i] == NULL) {
            continue;
        }

        if (cabinets[i]->documents != NULL) {
            free(cabinets[i]->documents);
        }
        free(cabinets[i]);
    }
    free(cabinets);
}

void print_cabinets(Cabinet** cabinets, size_t count) {
    printf("CABINETS:\n");

    for (size_t i = 0; i < count; i++) {
        printf("\t%zu ->", i);

        for (size_t j = 0; j < cabinets[i]->occupied; j++) {
            printf(" %zu", cabinets[i]->documents[j]->id);
        }
        printf("\n");
    }
}

void assign_to_cabinets(Cabinet** cabinets, Problem* problem) {

    for (size_t i = 0; i < problem->document_count; i++) {
        assign_to_cabinet(cabinets[i % problem->cabinet_count], problem->documents[i]);
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

    if (!status) free_problem(p);

    return status;
}

void free_problem(Problem* p) {
    printf("Closing problem ...\n");

    if (!p) return;
    if (!p->documents) return;

    for (size_t i = 0; i < p->document_count; i++) {
        if (p->documents[i] != NULL) {
            if (p->documents[i]->scores != NULL) {
                free(p->documents[i]->scores);
            }
            free(p->documents[i]);
        }
    }

    free(p->documents);
}

void print_problem(Problem* p) {
    printf("Cabinets: %zu\n", p->cabinet_count);
    printf("Documents: %zu\n", p->document_count);
    printf("Subjects: %zu\n", p->subject_count);

    printf("Documents:\n");
    for (size_t i = 0; i < p->document_count; i++) {
        printf("\tId: %zu \tScores:", p->documents[i]->id);
        for (size_t j = 0; j < p->subject_count; j++) {
            printf(" %lf", p->documents[i]->scores[j]);
        }
        printf("\n");
    }
}
