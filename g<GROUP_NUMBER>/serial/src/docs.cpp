#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <list>
#include <omp.h>

typedef struct Cabinet Cabinet;

typedef struct {
    size_t id;
    double* scores;
    Cabinet* parent;
} Document;

struct Cabinet {
    size_t id;
    std::list<Document*> documents;
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

void calculate_cabinet_averages(Cabinets cabinets, size_t subject_count);
double calculate_distance(size_t subject_count, double* score_1, double* score_2);
bool reassign_documents(Cabinets cabinets, size_t subject_count);
void move_document_to_cabinet(Document* document, Cabinet* to_cabinet);

void print_result(Document** documents, size_t count);

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

    //print_problem(problem);

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.inner = static_cast<Cabinet**>(std::malloc(problem.cabinet_count * sizeof(Cabinet*)));
    if (cabinets.inner == NULL) goto cleanup;

    for (size_t i = 0; i < problem.cabinet_count; i++) {
        cabinets.inner[i] = new Cabinet;
        cabinets.inner[i]->id = i;
        if (cabinets.inner[i] == NULL) goto cleanup;
        cabinets.inner[i]->scores = static_cast<double *>(malloc(problem.subject_count * sizeof(double)));
        if (cabinets.inner[i]->scores == NULL) goto cleanup;
    }

    double exec_time;
    exec_time = -omp_get_wtime();

    assign_to_cabinets(cabinets, problem.documents, problem.document_count);
    do {
        calculate_cabinet_averages(cabinets, problem.subject_count);
    }while (reassign_documents(cabinets, problem.subject_count));

    exec_time += omp_get_wtime();
    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(problem.documents, problem.document_count);

    cleanup:
    free_problem(problem);
    free_cabinets(cabinets);

    return 0;
}

void print_result(Document** documents, size_t count) {

    for (size_t i = 0; i < count; i++) {
        printf("%zu\n", documents[i]->parent->id);
    }
}

bool reassign_documents(Cabinets cabinets, size_t subject_count) {
    size_t occupied = 0;
    size_t amount = 0;
    Cabinet** to_cabinet = NULL;
    Document** to_reassign = NULL;

    for (size_t i = 0; i < cabinets.count; i++) {

        for (auto document : cabinets.inner[i]->documents) {
            double min_distance = INFINITY;
            size_t new_cabinet_index = 0;

            for (size_t k = 0; k < cabinets.count; k++) {
                double distance = calculate_distance(subject_count, cabinets.inner[k]->scores, document->scores);
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
                to_reassign = static_cast<Document **>(realloc(to_reassign, new_size * sizeof(Document*)));
                to_cabinet = static_cast<Cabinet **>(realloc(to_cabinet, new_size * sizeof(Document*)));
                amount = new_size;

                if (to_reassign == NULL || to_cabinet == NULL) {
                    fprintf(stderr, "Failed to allocate memory for reassigning documents.");
                    goto cleanup;
                }

            }

            to_reassign[occupied] = document;
            to_cabinet[occupied] = cabinets.inner[new_cabinet_index];
            //printf("moving document %zu to cabinet %zu\n", document->id, new_cabinet_index);
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
    to_cabinet->documents.push_back(document);
    document->parent->documents.remove(document);
    document->parent = to_cabinet;
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
        double* score_sum = static_cast<double *>(calloc(subject_count, sizeof(double)));
        for (auto document : cabinets.inner[i]->documents) {
            for (size_t k = 0; k < subject_count; k++) {
                score_sum[k] += document->scores[k];
            }
        }

        for (size_t k = 0; k < subject_count; k++) {
            cabinets.inner[i]->scores[k] = score_sum[k]/(double) cabinets.inner[i]->documents.size();
        }

        free(score_sum);
    }
}

void free_cabinets(Cabinets cabinets) {
    for (size_t i = 0; i < cabinets.count; i++) {
        if (cabinets.inner[i] == NULL) {
            continue;
        }
        free(cabinets.inner[i]->scores);
        delete cabinets.inner[i];
    }
    free(cabinets.inner);
}

void print_cabinets(Cabinets cabinets, size_t subject_count) {
    printf("CABINETS:\n");

    for (size_t i = 0; i < cabinets.count; i++) {
        printf("\t%zu ->", i);
        for (auto document : cabinets.inner[i]->documents) {
            printf(" %zu", document->id);
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
        auto cabinet = cabinets.inner[i % cabinets.count];
        cabinet->documents.push_back(documents[i]);
        documents[i]->parent = cabinet;
    }
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

    p->documents = static_cast<Document **>(malloc(p->document_count * sizeof(Document*)));
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

        Document* d = static_cast<Document *>(malloc(sizeof(Document)));
        if (d == NULL) goto cleanup;
        d->id = atoi(token);
        d->scores = static_cast<double *>(malloc(p->subject_count * sizeof(double)));

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
        //printf("Closing file ...\n");
        fclose(file);
    }

    if (!status) free_problem(*p);

    return status;
}

void free_problem(Problem p) {
    //printf("Closing problem ...\n");

    if (!p.documents) return;

    for (size_t i = 0; i < p.document_count; i++) {
        if (p.documents[i] == NULL) {
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
    printf("\n");
}
