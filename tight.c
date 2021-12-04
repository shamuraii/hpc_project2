#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define M_SIZE 100
#define NC 999 // Weight for No Connection (NC): Must be > M_SIZE in practice
#define NUM_THREADS 10

// Use struct to pass data to pthreads on creation
// Based on https://hpc-tutorials.llnl.gov/posix/example_code/hello_arg2.c
struct thread_data {
    int thread_id;
    int start;
    int end;
    int (*d)[M_SIZE];
    int (*m)[M_SIZE];
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
    for (int i=0; i < NUM_THREADS; i++) {
        starts[i] = (M_SIZE / NUM_THREADS) * i;
        ends[i] = (M_SIZE / NUM_THREADS) * (i+1) - 1;
    }
    // To account for M_SIZE not being evenly divisible by NUM_THREADS, the last thread handles the remaining values
    ends[NUM_THREADS-1] = M_SIZE-1;

    printf("Starts = ");
    print_row(starts,NUM_THREADS);
    printf("Ends = ");
    print_row(ends,NUM_THREADS);

    int dist[M_SIZE][M_SIZE];
    for (int t=0; t<NUM_THREADS; t++) {
        thread_data_array[t].thread_id = t;
        thread_data_array[t].start = starts[t];
        thread_data_array[t].end = ends[t];
        thread_data_array[t].m = m;
        thread_data_array[t].d = dist;

        printf("Creating thread %d\n", t);
        int rc = pthread_create(&threads[t], NULL, dijkstra_some, (void *) &thread_data_array[t]);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
        //dijkstra_some((void *) &thread_data_array[t]);
    }

    for (int t=0; t<NUM_THREADS; t++) {
        pthread_join(threads[t],NULL);
    }
    
    printf("M = \n");
    print_m(m);
    //int sp[M_SIZE][M_SIZE];
    //dijkstra_all(m,dist);
    printf("SP = \n");
    print_m(dist);

    free(m);

    // Equation to get runtime in seconds, see earlier comment for source
    clock_t end = clock();
    double run_time = (double)(end - begin) / CLOCKS_PER_SEC;
    // Display results
    printf("\nProgram runtime: %f\n", run_time);
}