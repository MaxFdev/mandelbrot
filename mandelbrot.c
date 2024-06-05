#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

// Define BMP file headers
#pragma pack(push, 1)
typedef struct
{
    uint16_t type;
    uint32_t size;
    uint32_t reserved1;
    uint32_t offset;
} BmpFileHeader;
typedef struct
{
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits;
    uint32_t compression;
    uint32_t imagesize;
    int32_t xresolution;
    int32_t yresolution;
    uint32_t ncolors;
    uint32_t importantcolors;
} BmpInfoHeader;
#pragma pack(pop)

// define a struct for pixel data calculations
typedef struct
{
    double x, y;
    int column, row;
} Data;

// define the max_iteration amount
#define MAX_ITERATION 255

// declare functions
FILE *createOutputFile(const char *filename, int size);
void freeClose();
void *workerThread(void *arg);
void *engineThread(void *arg);
void *writerThread(void *arg);
int mandelbrot(double real, double imag);

// declare global vars
int img_dim;
int engines;
double UL_X, UL_Y;
double mandel_dim;

// vars guiding thread activity
int columnCount;
int rowCount;
int rowTarget;

// vars for threading and control flow
pthread_t *workerTs;
pthread_t *engineTs;
sem_t *workerBlock;
sem_t *engineBlock;
sem_t writerBlock;
pthread_mutex_t pixelAccess;
pthread_barrier_t tBarrier;

// vars for data
Data *data;
int *pixels;

// var for file
FILE *output;

int main(int argc, char *argv[])
{
    // ! 1 Start up

    // check for the right amount of input
    if (argc != 6)
    {
        printf("Program needs all 5 args.\n");
        return 1;
    }

    // get the input values
    img_dim = atoi(argv[1]);
    engines = atoi(argv[2]);
    UL_X = atof(argv[3]);
    UL_Y = atof(argv[4]);
    mandel_dim = atof(argv[5]);

    // set up row counter
    columnCount = 0;
    rowCount = 0;
    rowTarget = img_dim;

    // set up the output file
    output = createOutputFile("mandeloutput.bmp", img_dim);
    if (output == NULL)
    {
        printf("File problems\n");
        return 1;
    }

    // set up writer
    pthread_t writerT;

    // ! 2 Allocations

    // set up engine semaphores
    workerBlock = (sem_t *)malloc(engines * sizeof(sem_t));
    if (workerBlock == NULL)
    {
        printf("Worker Block allocation problem");
        freeClose();
        return 1;
    }

    // set up engine semaphores
    engineBlock = (sem_t *)malloc(engines * sizeof(sem_t));
    if (engineBlock == NULL)
    {
        printf("Engine Block allocation problem");
        freeClose();
        return 1;
    }

    // set up engines
    engineTs = (pthread_t *)malloc(engines * sizeof(pthread_t));
    if (engineTs == NULL)
    {
        printf("Engine allocation problem\n");
        freeClose();
        return 1;
    }

    // set up data array for engines
    data = (Data *)malloc(engines * sizeof(Data));
    if (data == NULL)
    {
        printf("Data allocation problem\n");
        freeClose();
        return 1;
    }

    // set up workers
    workerTs = (pthread_t *)malloc(img_dim * sizeof(pthread_t));
    if (workerTs == NULL)
    {
        printf("Worker allocation problem\n");
        freeClose();
        return 1;
    }

    // set up pixels arrays
    pixels = malloc(img_dim * sizeof(int));
    if (pixels == NULL)
    {
        printf("Pixels allocation problem\n");
        freeClose();
        return 1;
    }

    // ! 3 Initializations (first control flow then independent threads)

    // initialize the thread barrier
    if (pthread_barrier_init(&tBarrier, NULL, img_dim + 1))
    {
        printf("Problem making thread barrier\n");
        freeClose();
        return 1;
    }

    // create pixel access mutex
    if (pthread_mutex_init(&pixelAccess, NULL))
    {
        printf("Problem making pixel access mutex\n");
        freeClose();
        return 1;
    }

    // create the writer semaphore
    if (sem_init(&writerBlock, 0, 0))
    {
        printf("Problem making writer semaphore\n");
        freeClose();
        return 1;
    }

    // create worker semaphore
    for (int i = 0; i < engines; i++)
    {
        if (sem_init(&workerBlock[i], 0, 1))
        {
            printf("Problem making worker semaphore %d\n", i);
            freeClose();
            return 1;
        }
    }

    // create engine semaphore
    for (int i = 0; i < engines; i++)
    {
        if (sem_init(&engineBlock[i], 0, 0))
        {
            printf("Problem making engine semaphore %d\n", i);
            freeClose();
            return 1;
        }
    }

    // create writer
    if (pthread_create(&writerT, NULL, writerThread, NULL))
    {
        printf("Problem making writer thread\n");
        freeClose();
        return 1;
    }

    // create an array of args for engines
    int *args = malloc(engines * sizeof(int));

    // create engines
    for (int i = 0; i < engines; i++)
    {
        args[i] = i;
        if (pthread_create(&engineTs[i], NULL, engineThread, &args[i]))
        {
            printf("Problem making engine thread %d\n", i);
            freeClose();
            return 1;
        }
    }

    // free args to be reused
    free(args);

    // allocate args for workers
    args = malloc(img_dim * sizeof(int));

    // create workers
    for (int i = 0; i < img_dim; i++)
    {
        args[i] = i;
        if (pthread_create(&workerTs[i], NULL, workerThread, &args[i]))
        {
            printf("Problem making worker thread %d\n", i);
            freeClose();
            return 1;
        }
    }

    // free args
    free(args);

    // ! 4 Reconvening

    // join the writer
    pthread_join(writerT, NULL);

    // join all workers
    for (int i = 0; i < img_dim; i++)
    {
        pthread_join(workerTs[i], NULL);
    }

    // join all engines
    for (int i = 0; i < engines; i++)
    {
        pthread_join(engineTs[i], NULL);
    }

    // destroy thread barrier
    pthread_barrier_destroy(&tBarrier);

    // destroy pixel access mutex
    pthread_mutex_destroy(&pixelAccess);

    // destroy writer block
    sem_destroy(&writerBlock);

    // destroy worker blocks
    for (int i = 0; i < img_dim; i++)
    {
        sem_destroy(&workerBlock[i]);
    }

    // destroy engine blocks
    for (int i = 0; i < engines; i++)
    {
        sem_destroy(&engineBlock[i]);
    }

    // ! 5 Deallocations

    // free all memory
    freeClose();

    // return without error
    return 0;
}

/**
 * Create/open the output file and return it.
 */
FILE *createOutputFile(const char *filename, int size)
{
    BmpFileHeader fileHeader = {0x4D42, sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + size * size * 3, 0, sizeof(BmpFileHeader) + sizeof(BmpInfoHeader)};
    BmpInfoHeader infoHeader = {sizeof(BmpInfoHeader), size, size, 1, 24, 0, size * size * 3, 0, 0, 0, 0};
    FILE *bmpFile = fopen(filename, "wb");
    if (!bmpFile)
    {
        perror("Error opening file");
        return NULL;
    }
    // Write headers
    fwrite(&fileHeader, sizeof(BmpFileHeader), 1, bmpFile);
    fwrite(&infoHeader, sizeof(BmpInfoHeader), 1, bmpFile);
    return bmpFile;
}

/**
 * Free all heap space and close output file.
 */
void freeClose()
{
    // free heap space
    free(workerBlock);
    free(engineBlock);
    free(engineTs);
    free(data);
    free(workerTs);
    free(pixels);

    // close the file
    fclose(output);
}

/**
 * Create a thread that keeps track of a pixel column and chooses an engine to calculate it.
 */
void *workerThread(void *arg)
{
    // get id
    int id = *(int *)arg;

    // process while there are more pixels to calculate
    while (1)
    {
        // wait at barrier before starting
        pthread_barrier_wait(&tBarrier);

        // check for exit condition
        if (rowCount == rowTarget)
        {
            pthread_exit(0);
        }

        // set up the random generator seed with id
        srand(id + rowCount);

        // choose an engine
        int engine = rand() % engines;

        // set up the data
        Data processData;
        processData.x = UL_X + (id * mandel_dim / img_dim);
        processData.y = UL_Y + ((img_dim - rowCount - 1) * mandel_dim / img_dim);
        processData.column = id;
        processData.row = rowCount;

        // lock the array to place data
        sem_wait(&workerBlock[engine]);

        // put data in array
        data[engine] = processData;

        // wake up the engine to process data
        sem_post(&engineBlock[engine]);
    }
}

/**
 * Create a thread that takes in coordinates and generates the pixel color.
 */
void *engineThread(void *arg)
{
    // get id
    int id = *(int *)arg;

    // process while there are more pixels to calculate
    while (1)
    {
        // wait for data to process
        sem_wait(&engineBlock[id]);

        // escape when calculations are done
        if (rowCount == rowTarget)
        {
            pthread_exit(0);
        }

        // extract the worker data
        double x = data[id].x;
        double y = data[id].y;
        int column = data[id].column;
        int row = data[id].row;

        // run the algorithm
        int color = mandelbrot(x, y);

        // wait for a chance to enter the pixel to the array
        pthread_mutex_lock(&pixelAccess);

        // enter the pixel in the writing array
        pixels[column] = color;

        // increase the column count
        columnCount++;

        // check if the writer can run now
        if (columnCount == img_dim)
        {
            // reset the column count
            columnCount = 0;

            // increase the total completed rows counter
            rowCount = row + 1;

            // start the writing procedure
            sem_post(&writerBlock);
        }

        // release the lock on the pixel array
        pthread_mutex_unlock(&pixelAccess);

        // release the worker at the end so writer can write
        sem_post(&workerBlock[id]);
    }
}

/**
 * Creates a thread that takes the pixel row and prints it in the file.
 */
void *writerThread(void *arg)
{
    // wait to start all workers
    pthread_barrier_wait(&tBarrier);

    // process while there are more pixels to calculate
    while (1)
    {
        // wait for row to be ready to write
        sem_wait(&writerBlock);

        // write the row
        for (int x = 0; x < img_dim; x++)
        {
            uint8_t b = pixels[x] & 0xFF;
            uint8_t g = pixels[x] & 0xFF;
            uint8_t r = pixels[x] & 0xFF;
            fwrite(&b, 1, 1, output);
            fwrite(&g, 1, 1, output);
            fwrite(&r, 1, 1, output);
        }

        // allow workers to continue
        pthread_barrier_wait(&tBarrier);

        // check for exit condition
        if (rowCount == rowTarget)
        {
            // before exiting release all engines
            for (int i = 0; i < engines; i++)
            {
                sem_post(&engineBlock[i]);
            }

            // exit
            pthread_exit(0);
        }
    }
}

/**
 * The mandelbrot optimized naive escape time algorithm.
 */
int mandelbrot(double px, double py)
{
    int iteration = 0;
    double x = 0.0;
    double y = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    while (iteration < 256)
    {
        x2 = x * x;
        y2 = y * y;
        if (x2 + y2 > 4.0)
        {
            return 255 - iteration;
        }
        y = 2 * x * y + py;
        x = x2 - y2 + px;
        iteration++;
    }
    return 0;
}
