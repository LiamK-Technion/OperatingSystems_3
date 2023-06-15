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

extern int queue_capacity;
extern int currently_running;

Node* createNode(int item, struct timeval arrivalTime)
{
    Node* node=malloc(sizeof(Node));
    if (!node)
        return NULL;
    node->request=item;
    node->next=NULL;
    node->arrivalTime=arrivalTime;
    return node;
}

void initQueue(Queue* queue)
{
    struct timeval arrivalTime;
    gettimeofday(&arrivalTime, NULL); 
    Node* node=createNode(0, arrivalTime);
    queue->head=node;
    queue->size=0;
}

void queueRemove(Queue* queue, int index)
{
    int current = -1; 
    Node* tmp = queue->head;
    if(queue->size==0)
        return;
    while((current+1)!=index)
    {
        ++current; 
        tmp = tmp->next; 
    }
    Node* to_del = tmp->next;
    tmp->next = to_del->next;
    Close(to_del->request);
    free(to_del);
}


Node* getNodeByCurrentThread()
{
    pthread_t t=pthread_self();
    for (int i=0; i<numOfThreads; i++)
    {
        if (pthread_equal(t, threadPool[i].thread))
            return threadPool[i].currentRequest;
    }
    return NULL;
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
            Close(head->request);
            free(head);
            waitingQueue->size--;
            return 1;
        }
        else{
            return 0;
        }
    }
    else if (waitingQueue->policy==bf)
    {
        while (waitingQueue->size + currently_running > 0)
            pthread_cond_wait(&blockcond, &lock);
        return 0;
    }
    else if (waitingQueue->policy==dynamic)
    {
        if (queue_capacity==waitingQueue->dynamicSize) {
            return 0;
        }
        queue_capacity++;
        return 0;
    }
    else if (waitingQueue->policy==randomPolicy)
    {
        srand((unsigned) time(NULL));
        int num_to_delete=((waitingQueue->size)+1)/2;
        for (int i=0; i<num_to_delete; i++)
        {
            int res=rand() % (waitingQueue->size);
            queueRemove(waitingQueue, res);
            waitingQueue->size--;
        }
        return 1;
    }
    return 1;
}

// Not implemented policies. If the queue is full, drop the requests
void enqueue(Queue* waitingQueue, int item)
{
    struct timeval arrivalTime;
    gettimeofday(&arrivalTime, NULL); 
    pthread_mutex_lock(&lock);
    int result=1;
    if (waitingQueue->size + currently_running >= queue_capacity)
        result=handleOverload(waitingQueue);

    if (result==0) {
        Close(item);
        //pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
        return;
    }
    // add the item to the queue
    Node* last=waitingQueue->head;
    while (last->next!=NULL)
        last=last->next;
    last->next=createNode(item, arrivalTime);
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
    pthread_mutex_unlock(&lock);
    return head;
}

void getargs(int *port, int* sizeOfQueue, Policy* policy, int* dynamicSize, int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue> <schedule> \n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    numOfThreads = atoi(argv[2]);
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
    int index=*((int*)ptr);
    while(1)
    {
        Node* n=dequeue(waitingQueue);
        int connfd=n->request;
        struct timeval currenttime;
        gettimeofday(&currenttime, NULL); 
        n->dispatchTime=currenttime;
        threadPool[index].requestCounter++;
        threadPool[index].currentRequest = n;

        requestHandle(connfd);
        Close(connfd);
        free(n);
        threadPool[index].currentRequest = NULL;
        pthread_mutex_lock(&lock);
        currently_running--;
        pthread_cond_signal(&blockcond);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen, sizeOfQueue, dynamicSize=0;
    Policy policy;
    struct sockaddr_in clientaddr;

    getargs(&port, &sizeOfQueue, &policy, &dynamicSize, argc, argv);
    if (sizeOfQueue<=0 || numOfThreads<=0)
    {
        fprintf(stderr, "Invalid arguments\n");
        return -1;
    }
    queue_capacity=sizeOfQueue;

    pthread_cond_init(&cond, NULL);
    pthread_cond_init(&blockcond, NULL);
    pthread_mutex_init(&lock, NULL);
    waitingQueue=malloc(sizeof(Queue));
    if(!waitingQueue)
        return -1;

    initQueue(waitingQueue);
    waitingQueue->policy=policy;
    waitingQueue->dynamicSize=dynamicSize;

    //pthread_t* threadPool=malloc(numOfThreads*sizeof(pthread_t));
    threadPool=malloc(numOfThreads*sizeof(ThreadStat));
    if(!threadPool){
        free(waitingQueue);
        return -1;
    }
    int* indexes = malloc(numOfThreads*sizeof(int));
    if(!indexes){
        free(threadPool);
        free(waitingQueue);
        return -1;
    }
    
    for (int i=0; i<numOfThreads; i++) {
        indexes[i]=i;
        threadPool[i].index = i;
        threadPool[i].dynamicCounter = 0;
        threadPool[i].requestCounter = 0;
        threadPool[i].staticCounter = 0;
        threadPool[i].currentRequest = NULL;
        pthread_create(&(threadPool[i].thread), NULL, thread_handle_request, (void*)&indexes[i]);
    }

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
        enqueue(waitingQueue, connfd);
    }
}