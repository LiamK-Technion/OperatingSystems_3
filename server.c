#include "segel.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

pthread_cond_t cond;
pthread_mutex_t lock;

pthread_cond_t blockcond;
//pthread_mutex_t blocklock;

int queue_capacity=0;
int currently_running=0;

/* ----------- QUEUE STUFF ------------*/ 
// Queue Policy
typedef enum Policy {block, dt, dh, bf, dynamic, randomPolicy} Policy;

typedef struct Node{
    struct Node* next;
    void* request;
} Node;

typedef struct Queue{
    Node* head;
    int size;
    Policy policy;
    int dynamicSize;
} Queue;


Node* createNode(void* item);
void enqueue(Queue* waitingQueue, void* item);
Node* dequeue(Queue* waitingQueue);
void initQueue(Queue* queue);

Node* createNode(void* item)
{
    Node* node=malloc(sizeof(Node));
    if (!node)
        return NULL;
    node->request=item;
    node->next=NULL;
    return node;
}

void initQueue(Queue* queue)
{
    Node* node=createNode(NULL);
    queue->head=node;
    queue->size=0;
}

// Returns 1 if continue with the request, 0 if drop it
int handleOverload(Queue* waitingQueue)
{
    if (waitingQueue->policy==block)
    {
        while (waitingQueue->size + currently_running >= queue_capacity)
            pthread_cond_wait(&blockcond, &lock);
        return 1;
    }
    else if (waitingQueue->policy==dt)
    {
        return 0;
    }
    else if (waitingQueue->policy==dh)
    {
        // Not sure what to do if queue size < number of threads. Assumed this won't be tested.
        if (waitingQueue->size>0) {
            Node* dummy=waitingQueue->head;
            Node* head=dummy->next;
            dummy->next=head->next;
            free(head);
            return 1;
        }
    }
    else if (waitingQueue->policy==bf)
    {
        
    }
    else if (waitingQueue->policy==dynamic)
    {
        
    }
    else if (waitingQueue->policy==randomPolicy)
    {
        
    }
    return 0;
}

// Not implemented policies. If the queue is full, drop the requests
void enqueue(Queue* waitingQueue, void* item)
{
    pthread_mutex_lock(&lock);
    int result;
    if (waitingQueue->size + currently_running >= queue_capacity)
        result=handleOverload(waitingQueue);

    if (result==0) {
        Close(*(int*)item);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
        return;
    }
    // add the item to the queue
    Node* last=waitingQueue->head;
    while (last->next!=NULL)
        last=last->next;
    last->next=createNode(item);
    waitingQueue->size++;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
}

Node* dequeue(Queue* waitingQueue)
{
    pthread_mutex_lock(&lock);

    while(waitingQueue->size==0) {
        pthread_cond_wait(&cond, &lock);
    }
    // remove the request from waiting queue
    Node* dummy=waitingQueue->head;
    Node* head=dummy->next;
    dummy->next=head->next;
    waitingQueue->size--;
    currently_running++;
    //pthread_cond_signal(&block);

    pthread_mutex_unlock(&lock);
    return head;
}

void getargs(int *port, int* numOfThreads, int* sizeOfQueue, Policy* policy, int* dynamicSize, int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue> <schedule> \n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *numOfThreads = atoi(argv[2]);
    *sizeOfQueue = atoi(argv[3]);
    if (strcmp(argv[4], "block")==0)
        *policy=block;
    else if (strcmp(argv[4], "dt")==0)
        *policy=dt;
    else if (strcmp(argv[4], "dh")==0)
        *policy=dh;
    else if (strcmp(argv[4], "bf")==0)
        *policy=bf;
    else if (strcmp(argv[4], "dynamic")==0) {
        *policy=dynamic;
        if (argc<6) {
            fprintf(stderr, "Usage: %s <port> <threads> <queue> <schedule> <maxsize> \n", argv[0]);
            exit(1);
        }
        *dynamicSize=atoi(argv[5]);
    }
    else if (strcmp(argv[4], "random")==0)
        *policy=randomPolicy;
    else {
        fprintf(stderr, "invalid schedule\n");
        exit(1);
    }
}

void* thread_handle_request(void* ptr)
{
    Queue* queue=(Queue*) ptr;
    while(1)
    {
        Node* n=dequeue(queue);
        int connfd=*(int*)(n->request);
        requestHandle(connfd);
        Close(connfd);
        free(n);
        currently_running--;
        pthread_cond_signal(&blockcond);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, numOfThreads, sizeOfQueue, dynamicSize=0;
    Policy policy;
    struct sockaddr_in clientaddr;

    getargs(&port, &numOfThreads, &sizeOfQueue, &policy, &dynamicSize, argc, argv);
    if (sizeOfQueue<=0 || numOfThreads<=0)
    {
        fprintf(stderr, "Invalid arguments\n");
        return -1;
    }
    queue_capacity=sizeOfQueue;

    pthread_cond_init(&cond, NULL);
    pthread_cond_init(&blockcond, NULL);
    pthread_mutex_init(&lock, NULL);
    Queue* waitingQueue=malloc(sizeof(Queue));
    if(!waitingQueue)
        return -1;

    initQueue(waitingQueue);
    waitingQueue->policy=policy;
    waitingQueue->dynamicSize=dynamicSize;

    pthread_t* threadPool=malloc(numOfThreads*sizeof(pthread_t));
    for (int i=0; i<numOfThreads; i++) {
        pthread_create(&threadPool[i], NULL, thread_handle_request, (void*)waitingQueue);
    }

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

        enqueue(waitingQueue, &connfd);
        //requestHandle(connfd);
        //Close(connfd);
    }
}


    


 
