#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#define M_SIZE 1000
#define NC (M_SIZE*10 - 1) // Weight for No Connection (NC): Must be > M_SIZE in practice

// I wrote these prints as a proof-of-concept regarding passing a single row of the matrix to a function
// And also to see the matrix was generated sufficiently
void print_row(int r[M_SIZE]) {
    // Print the values of the row
    for (int j=0; j<M_SIZE; j++)
        printf("%d ", r[j]);
    printf("\n");
}
void print_m(int m[M_SIZE][M_SIZE]) {
    // Print matrix one row at a time
    for (int i=0; i < M_SIZE; i++)
        print_row(m[i]);
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

int main() {
    // Track the runtime of the program (measures CPU time, NOT wall time, which is desired)
    // Learned from https://stackoverflow.com/questions/5248915/execution-time-of-c-program
    // After completing all 3 parts, I am not sure the accuracy of this when threading is involved,
    // as the results shown by this clock are much larger than the wall time the crc provides
    clock_t begin = clock();

    // Set the seed so the matrix M is deterministic
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

    // Create the distance matrix for results
    int (*dist)[M_SIZE] = malloc(sizeof(int[M_SIZE][M_SIZE]));
    // Calculate ALL shortest path weights
    dijkstra_all(m,dist);

    // I disabled printing results when M_SIZE is large for a few reasons
    // 1: when large it is hard/impossible to compare at a glance
    // 2: program spends SIGNIFICANT amount of time just printing and this makes the timing data unreliable
    // I have included results of printed small arrays (proof of working) and large ones (just for the runtime for comparison)
    if (M_SIZE <= 100) {
        printf("M = \n");
        print_m(m);
        printf("Dist = \n");
        print_m(dist);
    }

    // Take that valgrind!
    free(m);
    free(dist);

    // Equation to get runtime in seconds, see earlier comment for source
    clock_t end = clock();
    double run_time = (double)(end - begin) / CLOCKS_PER_SEC;
    // Display results
    printf("\nProgram runtime: %f\n", run_time);
}