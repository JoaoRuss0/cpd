using namespace std;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <omp.h>

typedef struct Cabinet Cabinet;
typedef struct Document Document;
typedef struct Cabinets Cabinets;
typedef struct Documents Documents;
typedef struct Problem Problem;

struct Document {
    size_t id;
    size_t parent_id;
};

struct Cabinet {
    size_t id;
    std::vector<size_t> documents;
};

struct Documents{
    size_t count;
    Document* inner;
    double* scores;
};

struct Cabinets{
    size_t count;
    Cabinet* inner;
    double* scores;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    Document* documents;
    double* document_scores;
};

void assign_to_cabinets(const Cabinets &cabinets, Document* documents, size_t document_count);
void calculate_cabinet_averages(const Cabinets &cabinets, size_t subject_count, const Documents &documents);
double calculate_distance(size_t subject_count, const double* score_1, const double* score_2);
bool reassign_documents(const Cabinets &cabinets, const Documents &documents, size_t subject_count);
void move_document_to_cabinet(Document* document, Cabinet* to_cabinet);

void free_problem(const Problem &p);
void free_cabinets(const Cabinets &cabinets);

bool parse_problem(char* filename, Problem* p);
void print_problem(const Problem &p);
void print_cabinets(const Cabinets &cabinets, size_t subject_count);
void print_result(const Documents &documents);

int main(const int argc, char *argv[]) {

    if (argc != 2) {
        //fprintf(stderr, "Invalid number of arguments.");
        return 1;
    }

    Problem problem;
    bool status = parse_problem(argv[1], &problem);
    if (!status) goto cleanup;

    //print_problem(problem);

    double exec_time;

    Documents documents;
    documents.count = problem.document_count;
    documents.inner = problem.documents;
    documents.scores = problem.document_scores;

    Cabinets cabinets;
    cabinets.count = problem.cabinet_count;
    cabinets.scores = static_cast<double*>(calloc(problem.cabinet_count * problem.subject_count, sizeof(double)));
    if(!cabinets.scores) goto cleanup;
    cabinets.inner = new Cabinet[problem.cabinet_count];;
    if(!cabinets.inner) goto cleanup;

    for (size_t i = 0; i < problem.cabinet_count; i++) cabinets.inner[i].id = i;

    exec_time = -omp_get_wtime();

    assign_to_cabinets(cabinets, problem.documents, problem.document_count);
    do {
        calculate_cabinet_averages(cabinets, problem.subject_count, documents);
    } while (reassign_documents(cabinets, documents, problem.subject_count));

    exec_time += omp_get_wtime();
    fprintf(stderr, "%.1fs\n", exec_time);
    print_result(documents);

    cleanup:
    free_problem(problem);
    free_cabinets(cabinets);

    return 0;
}

void print_result(const Documents &documents) {

    for (size_t i = 0; i < documents.count; i++) {
        printf("%zu\n", documents.inner[i].parent_id);
    }
}

bool reassign_documents(const Cabinets &cabinets, const Documents &documents, size_t subject_count) {
    std::vector<vector<size_t>> to_reassign(cabinets.count);

    bool swaps = false;

    for (size_t i = 0; i < documents.count; i++) {
        double min_distance = INFINITY;
        size_t new_cabinet_index = 0;

        for (size_t j = 0; j < cabinets.count; j++) {
            double distance = calculate_distance(subject_count, &cabinets.scores[j * subject_count],
                                                 &documents.scores[i * subject_count]);
            if (distance < min_distance) {
                min_distance = distance;
                new_cabinet_index = j;
            }
        }
        swaps |= new_cabinet_index != documents.inner[i].parent_id;

        to_reassign[new_cabinet_index].push_back(documents.inner[i].id);
    }

    for (size_t i = 0; i < cabinets.count; i++) {
        for (const size_t document_id : to_reassign[i]) {
            documents.inner[document_id].parent_id = i;
        }
        cabinets.inner[i].documents.swap(to_reassign[i]);
    }

    return swaps;
}

double calculate_distance(size_t subject_count, const double* score_1, const double* score_2) {

    double sum = 0;
    for (size_t i = 0; i < subject_count; i++) {
        double diff = score_1[i] - score_2[i];
        sum += diff * diff;
    }
    return sum;
}

void calculate_cabinet_averages(const Cabinets &cabinets, size_t subject_count, const Documents &documents) {

    memset(cabinets.scores, 0, sizeof(double) * cabinets.count * subject_count);

    for (size_t i = 0; i < cabinets.count; i++) {
        if (cabinets.inner[i].documents.empty()) {
            continue;
        }
        for (auto document_id : cabinets.inner[i].documents) {
            for (size_t k = 0; k < subject_count; k++) {
                cabinets.scores[i * subject_count + k] += documents.scores[subject_count * document_id + k];
            }
        }

        for (size_t k = 0; k < subject_count; k++) {
            cabinets.scores[i * subject_count + k] /= (double) cabinets.inner[i].documents.size();
        }
    }
}

void free_cabinets(const Cabinets &cabinets) {
    free(cabinets.scores);
    //free(cabinets.inner);
    delete[] cabinets.inner;
}

void assign_to_cabinets(const Cabinets &cabinets, Document* documents, const size_t document_count) {

    for (size_t i = 0; i < document_count; i++) {
        Cabinet* parent = &cabinets.inner[i % cabinets.count];
        parent->documents.push_back(documents[i].id);
        documents[i].parent_id = parent->id;
    }
}

bool parse_problem(char* filename, Problem* p) {

    bool status = true;
    FILE* file = fopen(filename, "r");
    if (!file) {
        status = false;
        //fprintf(stderr, "Error opening file '%s' for reading: %s\n", filename,
        //strerror(errno));
        goto cleanup;
    }

    char line[1024];
    if (!fgets(line, 1024, file) ||
        !sscanf(line, "%zu %zu %zu", &p->cabinet_count, &p->document_count, &p->subject_count)) {
        status = false;
        //fprintf(stderr, "Error reading first line of file\n");
        goto cleanup;
    }

    p->documents = static_cast<Document *>(calloc(p->document_count, sizeof(Document)));
    if (!p->documents) goto cleanup;
    p->document_scores = static_cast<double *>(calloc(p->document_count * p->subject_count, sizeof(double)));
    if (!p->document_scores) goto cleanup;

    for(size_t i = 0; i < p->document_count; i++) {

        if (!fgets(line, 1024, file)) {
            status = false;
            //fprintf(stderr, "Error %zu-th document line\n", i);
            goto cleanup;
        }

        char* token = strtok(line, " ");
        if (token == NULL) {
            status = false;
            //fprintf(stderr, "Error extracting id token from %zu-th document line\n", i);
            goto cleanup;
        }

        p->documents[i].id = i;

        for(size_t j = 0; j < p->subject_count ; j++) {
            token = strtok(NULL, " ");
            if (token == NULL) {
                status = false;
                //fprintf(stderr, "Error extracting %zu-th score token from %zu-th"
                //" document line\n", j, i);
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
    free (p.document_scores);
    free (p.documents);
}

void print_cabinets(const Cabinets &cabinets, size_t subject_count) {
    printf("CABINETS:\n");

    for (size_t i = 0; i < cabinets.count; i++) {
        printf("\t%zu ->", i);
        for (auto document : cabinets.inner[i].documents) {
            printf(" %zu", i);
        }

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