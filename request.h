#ifndef __REQUEST_H__

void requestHandle(int fd);

pthread_cond_t cond;
pthread_mutex_t lock;

pthread_cond_t blockcond;

int queue_capacity;
int currently_running;

/* ----------- QUEUE STUFF ------------*/ 
// Queue Policy
typedef enum Policy {block, dt, dh, bf, dynamic, randomPolicy} Policy;

typedef struct Node{
    struct Node* next;
    int request;
    struct timeval arrivalTime;
    struct timeval dispatchTime;
} Node;

typedef struct Queue{
    Node* head;
    int size;
    Policy policy;
    int dynamicSize;
} Queue;


Node* createNode(int item, struct timeval arrivalTime);
void enqueue(Queue* waitingQueue, int item);
Node* dequeue(Queue* waitingQueue);
void initQueue(Queue* queue);
void queueRemove(Queue* queue, int index);
Node* getNodeByCurrentThread();

/* ----------- STATISTICS STUFF ------------*/ 
typedef struct ThreadStat{
    pthread_t thread;
    int index;
    Node* currentRequest;
    int requestCounter;
    int dynamicCounter;
    int staticCounter;
} ThreadStat;

/* ----------- GLOBAL VARIABLES ------------*/ 

Queue* waitingQueue;
ThreadStat* threadPool;
int numOfThreads;

#endif
