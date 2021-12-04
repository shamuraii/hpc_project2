#include <string.h> // for memcpy
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <mpi.h>

#define M_SIZE 100
#define NC 999 // Weight for No Connection (NC): Must be > M_SIZE in practice

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

int main() {
    // Track the runtime of the program (measures CPU time, NOT wall time, which is desired)
    // Learned from https://stackoverflow.com/questions/5248915/execution-time-of-c-program
    // After completing all 3 parts, I am not sure the accuracy of this when threading is involved,
    // as the results shown by this clock are much larger than the wall time the crc provides
    clock_t begin = clock();

    // Set the seed so the matrix M is deterministic
    // In "real life", this identical matrix could be recieved by all files via some
    // external source, such as a file, network, or other shared resource
    // Alternatively, the main world could create the matrix and share it with all other nodes,
    // I am opting not to do that since it seems like it is not the intention of this project
    // For this project, I will simply have each thread create identical (arbitray) matrices
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

    // Initialize the MPI environment
    MPI_Init(NULL, NULL);
    // Get the number of processes
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    // Get the rank of the process
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    int start, end; // FIRST and LAST node to calculate SP weights for
    // Use world_rank to determine role in computations
    start = (M_SIZE / world_size) * world_rank;
    end = (M_SIZE / world_size) * (world_rank + 1) - 1;
    // Last node computes a the remainder in case M_SIZE not evenly divisible by WORLD_SIZE
    if (world_rank == world_size - 1)
        end = M_SIZE - 1;

    // Print a message to signal node is running
    printf("Processor %s, rank %d / %d: start = %d, end = %d\n", processor_name, world_rank, world_size, start, end);

    // Run dijkstras on this processors portion of the nodes
    // Store the results into appropriate rows of a local dist matrix
    // Memory could be saved by only creating dist to be [end-start][M_SIZE], but I chose
    // to make dist the full size so that it is 1: known at compile time, 2: the same size for all processors
    // Point 2 will come in handy when MPI_SEND/RECV are called on the entire contiguous memory location at once
    int (*dist)[M_SIZE] = malloc(sizeof(int[M_SIZE][M_SIZE]));
    for (int i=start; i<=end; i++)
        dijkstra_one(m,i,dist[i]);
    
    // Share data with processor 0
    // Code to send MxM matrix at once from https://stackoverflow.com/questions/5901476/sending-and-receiving-2d-array-over-mpi
    // This works since I created dist as a contiguous memory block with M_SIZE * M_SIZE elements
    if (world_rank != 0) {
        printf("Proc: %d sending dist\n", world_rank);
        MPI_Send(&(dist[0][0]), M_SIZE*M_SIZE, MPI_INT, 0, 0, MPI_COMM_WORLD);
    } else {
        // This is PROC_0 and is responsible for assembling / combining all results together
        // Create buffer for results to be recieved into
        // Again, proc_dist being the same size for all processors is coming in handy here
        // The same buffer can be reused for all recieves and then ultimately freed
        int (*proc_dist)[M_SIZE] = malloc(sizeof(int[M_SIZE][M_SIZE]));
        for (int i = 1; i < world_size; i++) {
            // Await distance matrix results from PROC_i
            MPI_Recv(&(proc_dist[0][0]), M_SIZE*M_SIZE, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Use world_rank to determine role in computations
            // I could have had each processor send their start and end data with MPI_SEND
            // I opted not to, since the overhead seemed like it would not be worth when
            // compared to 3 simple arithmetic operations
            int proc_start = (M_SIZE / world_size) * i;
            int proc_end = (M_SIZE / world_size) * (i + 1) - 1;
            if (i == world_size - 1)
                proc_end = M_SIZE - 1;

            // Combine relevant rows into main dist matrix
            // We know that the results of a PROC_i saved their results
            // into the rows of dist from dist[start] : dist[end]
            // I loop through each of these rows and copy the contiguos memory
            // into the main dist matrix stored in proc_0
            // I *could* have done this in 1 line with something like
            // memcpy(dist[r], proc_dist[r], sizeof(int[M_SIZE][proc_end-proc_start]));
            // but that seems very sketchy to me
            for (int r=proc_start; r <= proc_end; r++)
                memcpy(dist[r], proc_dist[r], sizeof(int[M_SIZE]));
        }
        // Our trusty buffer can now be freed
        // since all messages are completed
        free(proc_dist);

        // Proc_0 prints the matrix (for debugging)
        if (world_rank == 0) {
            // Printing will be interrupted by other processor prints, and is not necessary outside testing
            // But it is sufficient enough to determine accuracy/functionality
            if (M_SIZE <= 100) {
                printf("M = \n");
                print_m(m);
                printf("dist = \n");
                print_m(dist);
            }
        }
    }

    // Equation to get runtime in seconds, see earlier comment for source
    clock_t stop = clock();
    double run_time = (double)(stop - begin) / CLOCKS_PER_SEC;
    // Display results
    printf("\nProgram runtime: %f\n", run_time);

    // Finalize the MPI environment.
    MPI_Finalize();
}