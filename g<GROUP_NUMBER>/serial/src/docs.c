#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>

typedef struct {
    uint16_t id;
    double* scores;
} Document;

typedef struct {
    uint16_t cabinet_count;
    uint16_t document_count;
    uint16_t subject_count;
    Document** documents;
} Problem;

bool parse_problem(char* filename, Problem* p);
void free_problem(Problem* p);
void print_problem(Problem* p);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments.");
        return 1;
    }

    Problem p;
    bool status = parse_problem(argv[1], &p);
    if (!status) {
        return 1;
    }

    free_problem(&p);
    return 0;
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
        !sscanf(line, "%" SCNu16 " %" SCNu16 " %" SCNu16, &p->cabinet_count,
            &p->document_count, &p->subject_count)) {

        status = false;
        fprintf(stderr, "Error reading first line of file\n");
        goto cleanup;
    }

    p->documents = malloc(p->document_count * sizeof(Document));

    for(uint16_t i = 0; i < p->document_count; i++) {

        if (!fgets(line, 1024, file)) {
            status = false;
            fprintf(stderr, "Error %d-th document line\n", i);
            goto cleanup;
        }

        char* token = strtok(line, " ");
        if (token == NULL) {
            status = false;
            fprintf(stderr, "Error extracting id token from %d-th document line\n", i);
            goto cleanup;
        }

        Document* d = malloc(sizeof(Document));
        d->id = atoi(token);
        d->scores = malloc(p->subject_count * sizeof(double));

        for(uint16_t j = 0; j < p->subject_count ; j++) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                status = false;
                fprintf(stderr, "Error extracting %d-th score token from %d-th"
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

    return p;
}

void free_problem(Problem* p) {
    printf("Closing problem ...\n");

    if (!p) return;
    if (p->documents) return;

    for(int i = 0; i < p->document_count; i++) {
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
    printf("Cabinets: %"PRIu16"\n", p->cabinet_count);
    printf("Documents: %"PRIu16"\n", p->document_count);
    printf("Subjects: %"PRIu16"\n", p->subject_count);

    printf("Documents:\n");
    for(int i = 0; i < p->document_count; i++) {
        printf("\tId:%" PRIu16 "\tScores:", p->documents[i]->id);
        for(int j = 0; j < p->subject_count; j++) {
            printf(" %lf", p->documents[i]->scores[j]);
        }
        printf("\n");
    }
}
