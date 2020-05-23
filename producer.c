#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>


// Struct for the buffer to be shared among processes
struct buffer {
    int numOfActiveProcesses; // Number of processes currently attached to the shared buffer
    int buffSize; // Size of the buffer specified by the user
    int numOfCurrItems; // Current number of items in the buffer
    int buffItem[100]; // Max possible size of the buffer
};

// Declaring variables to be used by shared memory and semaphores
struct buffer *shmaddr;
int shmid, mutex, empty, full, BUFFSIZE;

union Semun
{
    int val;                /* value for SETVAL */
    struct semid_ds *buf;   /* buffer for IPC_STAT & IPC_SET */
    ushort *array;          /* array for GETALL & SETALL */
    struct seminfo *__buf;  /* buffer for IPC_INFO */
    void *__pad;
};

// Create a semaphore and give it an initial value
int create_sem(int key, int initial_value)
{
    union Semun semun;

    int sem = semget(key, 1, 0666|IPC_CREAT);

    if(sem == -1)
    {
        perror("Error in create sem.\n");
        exit(-1);
    }

    semun.val = initial_value;  /* initial value of the semaphore, Binary semaphore */
    if(semctl(sem, 0, SETVAL, semun) == -1)
    {
        perror("Error in semctl.\n");
        exit(-1);
    }
    
    return sem;
}

// Destroy a semaphore
void destroy_sem(int sem)
{
    if(semctl(sem, 0, IPC_RMID) == -1)
    {
        perror("Error in semctl");
        exit(-1);
    }
}

// Down a semaphore
void down(int sem)
{
    struct sembuf p_op;

    p_op.sem_num = 0;
    p_op.sem_op = -1;
    p_op.sem_flg = !IPC_NOWAIT;

    if(semop(sem, &p_op, 1) == -1)
    {
        perror("Error in down()");
        exit(-1);
    }
}

// Up a semaphore
void up(int sem)
{
    struct sembuf v_op;

    v_op.sem_num = 0;
    v_op.sem_op = 1;
    v_op.sem_flg = !IPC_NOWAIT;

    if(semop(sem, &v_op, 1) == -1)
    {
        perror("Error in up()");
        exit(-1);
    }
}

// Handler to be executed in case of a SIGINT
void handler(int signum) {

    down(mutex); // Wait till no one is accessing the shared buffer
    if (shmaddr->numOfActiveProcesses == 1) { // If this process is the only one attached to the shared memory
        shmctl(shmid, IPC_RMID, (struct shmid_ds*)0); // Destroy the shared memory
        destroy_sem(mutex); // Destroy the mutex semaphore
        destroy_sem(empty); // Destroy the empty semaphore
        destroy_sem(full); // Destroy the full semaphore
        printf("Destroying shared memory and semaphore in producer.\n");
    }
    else { // If other processes are still attached to the shared memory
        shmaddr->numOfActiveProcesses--; // Decrement the number of processes attached to the memory before detaching from it
        up(mutex); // Do not block access to the shared memory
        up(full); // To allow consumer to get unblocked (if it was blocked because the buffer is empty) so that is can exit
        shmdt(shmaddr); // Detach from the shared memory
        printf("Detaching from shared memory in producer.\n");
    }
    exit(1); // Exit
}

int main(int argc, char * argv[]) {
    // Producer code
 
    // Rate of production
    double producerRate;

    if (argc == 2) { // Check if only one other argument (producerRate) is passed to the producer other than the process name
        producerRate = atof(argv[1]);
        printf("Producer rate is: %f.\n", producerRate);
    }
    else { // If a wrong number of arguments is passed, print out this message to the user and exit
        printf("Wrong number of passed arguments. Exiting.\n");
        exit(-1); // Exit
    }

    // Specify handler to SIGINT
    signal(SIGINT, handler);

    // Create shared memory
    shmid = shmget(32779, sizeof(struct buffer), IPC_CREAT|0644);
    if(shmid == -1)
    {
        perror("Error in creating shared buffer in producer.\n");
        exit(-1);
    }
    else 
    {
        printf("Shared buffer created in producer with ID = %d\n", shmid);
    }

    // Attach shared memory to producer
    shmaddr = shmat(shmid, (void *)0, 0);
    if(shmaddr == -1)
    {
        perror("Error in attaching to shared buffer to producer.\n");
        exit(-1);
    }
    else
    {
        printf("Shared buffer attached at address %x in producer.\n", shmaddr);
    }

    // Create a semaphore to guarantee one-at-a-time access to the shared memory
    mutex = create_sem(32770, 0);

    // Initialize the number of active processes
    shmaddr->numOfActiveProcesses = 1;

    // Let user specify the size of the buffer
    printf("\nPlease enter the size of the buffer (min 1, max 100):\n");
    scanf("%d", &(shmaddr->buffSize));

    // Check that the buffer size is valid
    while ((shmaddr->buffSize > 100) || (shmaddr->buffSize < 1)) {
        printf("\nPlease enter a valid size for the buffer (min 1, max 100):\n");
        scanf("%d", &(shmaddr->buffSize));
    }
    BUFFSIZE = shmaddr->buffSize;

    // Create semaphores to count empty and full slots in the shared buffer
    empty = create_sem(32771, BUFFSIZE);
    full = create_sem(32772, 0);

    up(mutex);

    // The index of the buffer array in which the producer is to add its produced item
    int prodPointer = 0;
    // Currently produced item
    int currItem;

    // Flag needed in handling the case of the consumer exiting while the producer is blocked waiting for items to be consumed
    int flag = 0;

    while(1) {

        // Enter critical section
        down(mutex);

        if ((shmaddr->numOfActiveProcesses == 1) && (shmaddr->numOfCurrItems == BUFFSIZE)) { // Check if this is the only process attached to the memory and if the buffer is full
            printf("This is the only process attached to the memory and the buffer is full. Therefore, exiting.\n");
            // Leaving critical section
            up(mutex);
            break;
        }
        // Leave critical section
        up(mutex);

        printf("Will attempt to produce an item.\n");

        // Block if buffer is full
        down(empty);
        // Block if another process is accessing the shared memory
        down(mutex);

        // Check if buffer was full but producer got unblocked because the consumer exited
        if ((!flag) && (shmaddr->numOfActiveProcesses == 1)) {
            // Set flag to 1 indicating that the consumer exited
            flag = 1;

            if (shmaddr->numOfCurrItems == BUFFSIZE) { // If buffer is full
                // Leave critical section
                up(mutex);
                // Continue in order to exit
                continue;
            }
            else { // If buffer is not full
                // Decrement empty to compensate for its incrementation when the consumer exited
                down(empty);
            }

        }

        // Current item is the number of items in the buffer at this moment
        currItem = shmaddr->numOfCurrItems;

        // Place item in the appropriate index of the buffer array
        shmaddr->buffItem[prodPointer % BUFFSIZE] = currItem;
        // Increment the number of items in the buffer
        shmaddr->numOfCurrItems++;

        // Leave critical section
        up(mutex);
        // Incremet full to indicate that an item was produced
        up(full);

        printf("Produced an item with value: %d.\n\n", currItem);
        prodPointer++;

        // Sleep with 1/producerRate to ensure production is done at the desired rate
        usleep((1/producerRate)*1000000);
    }

    raise(SIGINT);
}