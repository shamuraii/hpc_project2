#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define M_SIZE 1000
#define NC 9999 // Weight for No Connection (NC): Must be > M_SIZE in practice
#define NUM_THREADS 10

// Use struct to pass data to pthreads on creation
// Based on https://hpc-tutorials.llnl.gov/posix/example_code/hello_arg2.c
struct thread_data {
    int thread_id; // # of thread (serves no purpose here)
    int start; // node to start calculating SPs
    int end; // node to end calculating SPs
    int (*d)[M_SIZE]; // Pointer to dist matrix for results
    int (*m)[M_SIZE]; // Pointer to M matrix to access graph
};

// I wrote these prints as a proof-of-concept regarding passing a single row of the matrix to a function
// And also to see the matrix was generated sufficiently
void print_row(int r[], int N) {
    // Print the values of the row
    for (int j=0; j<N; j++)
        printf("%d ", r[j]);
    printf("\n");
}
void print_m(int m[M_SIZE][M_SIZE]) {
    // Print matrix one row at a time
    for (int i=0; i < M_SIZE; i++)
        print_row(m[i],M_SIZE);
    printf("\n");
}

// See dijkstra comments
int minDistance(int dist[M_SIZE], bool sptSet[M_SIZE]) {
    int min = NC, min_index;
    for (int i = 0; i < M_SIZE; i++)
        if (sptSet[i] == false && dist[i] <= min)
            min = dist[i], min_index = i;
    return min_index;
}

/*
https://www.tutorialspoint.com/c-cplusplus-program-for-dijkstra-s-shortest-path-algorithm
Small changes have been made from their function, including an out of bounds check and the return
The function also assumes the matrix connections are in the rows, while the assignment says columns
Because of this, I flipped the iterators when accessing m, hand verified to work for m_size=3,4,and 5
This function takes in M, and a source point (0:M_SIZE-1)
It then computes the least jumps from src to all nodes
The distance from src->X is saved in dist[X]
*/
void dijkstra_one(int m[M_SIZE][M_SIZE], int src, int dist[M_SIZE]) {
    // Bounds check
    if (src >= M_SIZE) {
        printf("DIJKSTRA_ONE OUT_OF_BOUNDS: %d",src);
        return;
    }

    bool sptSet[M_SIZE]; 
    for (int i = 0; i < M_SIZE; i++) {
        dist[i] = NC;
        sptSet[i] = false;
    }
    dist[src] = 0;
    
    for (int count = 0; count < M_SIZE - 1; count++) {
        int u = minDistance(dist, sptSet);
        sptSet[u] = true;
        for (int j = 0; j < M_SIZE; j++) {
            if (!sptSet[j] && m[j][u] && dist[u] != NC && (dist[u] + m[j][u]) < dist[j])
                dist[j] = dist[u] + m[j][u];
        }
    }
}

/*
This function gets the SP weight from all nodes to all other nodes
The results are saved in a dist matrix
The distance from src->des is available in dist[src][des]
** Note this is transposed compared to the connections found in m
*/
void dijkstra_all(int m[M_SIZE][M_SIZE], int dist[M_SIZE][M_SIZE]) {
    for (int i=0; i<M_SIZE; i++)
        dijkstra_one(m, i, dist[i]);
}

/*
This function is the one ran by each pthread
It begins by parsing the arguments into the appropriate data structure to access critical info
It then simply runs dijkstra_one on each of its assigned nodes (from start:end inclusive)
The results are stored as it goes into the row dist[i]
dist is an M_SIZE*M_SIZE matrix that all threads are given access to
This is okay, however since no threads have overlapping writes, as each is assigned different rows
of dist to compute. Meaning, all threads can change dist as they progress and no locks are necessary, nice!
M is also shared between all threads, but M is only read, so no worries there either
*/
void *dijkstra_some(void *threadarg) {
    struct thread_data *my_data;
    my_data = (struct thread_data *) threadarg;
    int taskid = my_data->thread_id;
    int start = my_data->start;
    int end = my_data->end;
    int (*m)[M_SIZE] = my_data->m;
    int (*dist)[M_SIZE] = my_data->d;

    for (int i=start; i<=end; i++)
        dijkstra_one(m,i,dist[i]);
}

int main() {
    // Track the runtime of the program (measures CPU time, NOT wall time, which is desired)
    // Learned from https://stackoverflow.com/questions/5248915/execution-time-of-c-program
    // After completing all 3 parts, I am not sure the accuracy of this when threading is involved,
    // as the results shown by this clock are much larger than the wall time the crc provides
    clock_t begin = clock();

    // Set the seed so the matrix M is deterministic (for testing)
    srand(0);

    // Create the matrix of relationships
    // Randomly set every connection to 0 or 1 with SPLIT = % of connections (to adjust sparcity)
    int split = 5;
    int (*m)[M_SIZE] = malloc(sizeof(int[M_SIZE][M_SIZE]));
    for (int i=0; i < M_SIZE; i++) {
        for (int j=0; j < M_SIZE; j++) {
            m[i][j] = rand()%100 < split;
        }
        // Set all diagonals to 1
        // (Likely unecessary, but may cause unplanned issues and is logical to me anyway)
        m[i][i] = 1;
    }

    pthread_t threads[NUM_THREADS]; // Array of all pthread IDs
    int starts[NUM_THREADS]; // Array of starting values for SP computation for each thread
    int ends[NUM_THREADS]; // Array of ending values for SP computation for each thread
    struct thread_data thread_data_array[NUM_THREADS]; // Array of parameters to pass to each thread

    // Divide the workload (somewhat) evenly amongst the threads by setting their start/end indeces
    // If NUM_THREADS > M_SIZE, this will break. I do not plan to fix that, just don't do that
    // To fix that, check if (ends[i] = -1) and don't then dont create the thread / ignore results, etc.
    for (int i=0; i < NUM_THREADS; i++) {
        starts[i] = (M_SIZE / NUM_THREADS) * i;
        ends[i] = (M_SIZE / NUM_THREADS) * (i+1) - 1;
    }
    // To account for M_SIZE not being evenly divisible by NUM_THREADS, the last thread handles the remaining values
    ends[NUM_THREADS-1] = M_SIZE-1;

    // Print which nodes each thread will process
    printf("Starts = ");
    print_row(starts,NUM_THREADS);
    printf("Ends = ");
    print_row(ends,NUM_THREADS);

    // Create dist matrix for all threads to share access to, and for main to access the results of the threads
    int (*dist)[M_SIZE] = malloc(sizeof(int[M_SIZE][M_SIZE]));
    for (int t=0; t<NUM_THREADS; t++) {
        // Fill struct with appropriate values
        thread_data_array[t].thread_id = t;
        thread_data_array[t].start = starts[t];
        thread_data_array[t].end = ends[t];
        thread_data_array[t].m = m;
        thread_data_array[t].d = dist;

        // Create the pthread by passing the struct of parameters/outputs
        printf("Creating thread %d\n", t);
        int rc = pthread_create(&threads[t], NULL, dijkstra_some, (void *) &thread_data_array[t]);
        if (rc) {
            // Unlikely, but in case of error catch it
            // https://hpc-tutorials.llnl.gov/posix/example_code/hello_arg2.c
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    // Join all the threads to ensure they have completed running
    // before accessing the results in dist
    for (int t=0; t<NUM_THREADS; t++) {
        pthread_join(threads[t],NULL);
    }
    
    // I disabled printing results when M_SIZE is large for many reasons
    // 1: when large it is hard/impossible to compare at a glance
    // 2: program spends SIGNIFICANT amount of time just printing and this makes the threading
    // improvements seem worse than they really are.
    // I have included results of both small arrays (proof of working) and large ones (just the runtime for comparison)
    if (M_SIZE <= 100) {
        printf("M = \n");
        print_m(m);
        printf("Dist = \n");
        print_m(dist);
    }

    free(m);
    free(dist);

    // Equation to get runtime in seconds, see earlier comment for source
    clock_t end = clock();
    double run_time = (double)(end - begin) / CLOCKS_PER_SEC;
    // Display results
    printf("\nProgram runtime: %f\n", run_time);
}