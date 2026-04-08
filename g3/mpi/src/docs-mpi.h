#ifndef DOCS_MPI_H
#define DOCS_MPI_H

#include <math.h>
#include <mpi.h>
#include <omp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEED 1234
#define RAND_RANGE 10.0
#define UNIF01 ((double)rand() / RAND_MAX)

// --------------- Data structures ---------------

typedef struct Documents Documents;
typedef struct Cabinets Cabinets;
typedef struct Problem Problem;
typedef struct Change Change;

struct Documents {
    size_t count;
    size_t *parent_ids;
    double *scores;
};

struct Cabinets {
    size_t count;
    double *scores;
    size_t *doc_ids;
};

struct Problem {
    size_t cabinet_count;
    size_t document_count;
    size_t subject_count;
    double *document_scores;
};

struct Change {
    size_t cab_idx;
    double distance;
};

// --------------- Function declarations ---------------

// Core algorithm
void assign_to_cabinets();
void compute_best_local_cabinet_for_documents();
int assign_best_cabinet();
void recompute_scores();
size_t get_closest_local_cabinet_index(size_t parent_id,
                                       double *document_scores,
                                       double *distance);
double calculate_distance(double *score_1, double *score_2);

// MPI setup
void compute_comms();
void compute_partition(size_t total, int num_parts, int part_id, size_t *start,
                       size_t *end, size_t *count);
void gather_parent_ids();

// Init and cleanup
bool parse_problem(char *filename, Problem *p);
bool init_cabinets(Problem *problem);
bool init_documents(Problem *problem);
bool init_changes();
bool init_sums_counts();
void free_problem(Problem *p);

// Output
void print_result();

#endif
