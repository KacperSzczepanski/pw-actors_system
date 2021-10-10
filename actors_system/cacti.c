#include "cacti.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#ifdef __GNUC__
#define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#define UNUSED(x) UNUSED_ ## x
#endif

//implementation of queue of messages
typedef struct msgNode* msgNodePtr;
typedef struct msgQueue* msgQueuePtr;
struct msgNode {
    message_t msg;
    msgNodePtr next;
} msgNode_t;
struct msgQueue {
    msgNodePtr first, last;
    unsigned long size;
} msgQueue_t;
msgQueuePtr newMsgQueue() {
    msgQueuePtr Q = malloc(sizeof(msgQueue_t));
    Q->first = NULL;
    Q->last = NULL;
    Q->size = 0;
    
    return Q;
}
bool msgEmpty(msgQueuePtr Q) {
    return Q->size == 0;
}
unsigned long msgSize(msgQueuePtr Q) {
    return Q->size;
}
void msgPush(msgQueuePtr Q, message_t msg) {
    msgNodePtr newNode = malloc(sizeof(msgNode_t));
    newNode->msg = msg;
    newNode->next = NULL;

    if (msgEmpty(Q)) {
        Q->first = newNode;
        Q->last = newNode;
    } else {
        Q->last->next = newNode;
        Q->last = newNode;
    }
    
    ++Q->size;
}
void msgPop(msgQueuePtr Q) {
    if (msgEmpty(Q)){
        return;
    }
    --Q->size;
    
    msgNodePtr tmp = Q->first;
    Q->first = Q->first->next;
    free(tmp);
    
    if (msgEmpty(Q)) {
        Q->last = NULL;
    }
}
message_t msgFront(msgQueuePtr Q) {
    return Q->first->msg;
}
//implementation of queue of actors' ids
typedef struct idNode* idNodePtr;
typedef struct idQueue* idQueuePtr;
struct idNode {
    int id;
    idNodePtr next;
} idNode_t;
struct idQueue {
    idNodePtr first, last;
    unsigned long size;
} idQueue_t;
idQueuePtr newIdQueue() {
    idQueuePtr Q = malloc(sizeof(idQueue_t));
    Q->first = NULL;
    Q->last = NULL;
    Q->size = 0;
    
    return Q;
}
bool idEmpty(idQueuePtr Q) {
    return Q->size == 0;
}
unsigned long idSize(idQueuePtr Q) {
    return Q->size;
}
void idPush(idQueuePtr Q, actor_id_t id) {
    idNodePtr newNode = malloc(sizeof(idNode_t));
    newNode->id = id;
    newNode->next = NULL;
    
    if (idEmpty(Q)) {
        Q->first = newNode;
        Q->last = newNode;
    } else {
        Q->last->next = newNode;
        Q->last = newNode;
    }
    
    ++Q->size;
} 
void idPop(idQueuePtr Q) {
    if (idEmpty(Q)) {
        return;
    }
    --Q->size;
    
    idNodePtr tmp = Q->first;
    Q->first = Q->first->next;
    free(tmp);
    
    if (idEmpty(Q)) {
        Q->last = NULL;
    }
}
actor_id_t idFront(idQueuePtr Q) {
    return Q->first->id;
}

idQueuePtr actorsIdsQueue;
int err;
pthread_t threads[POOL_SIZE];
__thread actor_id_t currentId;
actor_id_t createdActors = 0, deadActors = 0;
bool stopCreatingNewActors;

sem_t somethingIsOnIdQueue, idQueueLock, newActorLock;

typedef struct actor {
    actor_id_t id;
    role_t *role;
    msgQueuePtr messages;
    bool goDieReceived, died;
    sem_t msgQueueLock;
    void *stateptr;
} actor_t;

actor_t **actorsArr;
unsigned long arrSize, ind;
bool addNewActorToDynamicArray(actor_t *newActor) {
    if (arrSize == 0) {
        arrSize = 1;
        actorsArr = malloc(sizeof(actor_t) * 1);
        if (actorsArr == NULL) {
            return false;
        }
    }
    
    if (ind + 1 > arrSize) {
        arrSize <<= 1;
        actorsArr = realloc(actorsArr, sizeof(actor_t) * arrSize);
        if (actorsArr == NULL) {
            return false;
        }
    }
    
    actorsArr[ind] = newActor;
    ++ind;
    
    return true;
}
actor_id_t numberOfActorsInArray() {
    return ind;    
}

bool create_new_actor(actor_t *newActor, role_t *const role) {
    if (createdActors - deadActors >= CAST_LIMIT) {
        return false;
    }
    
    newActor->id = numberOfActorsInArray();
    newActor->role = role;
    newActor->messages = newMsgQueue();
    newActor->goDieReceived = false;
    newActor->died = false;
    newActor->stateptr = NULL;
    if (sem_init(&newActor->msgQueueLock, 0, 1) == -1) {
        return false;
    }
    if (addNewActorToDynamicArray(newActor) == false) {
        return false;
    }
    ++createdActors;
    
    return true;
}

void *worker(void* UNUSED(data)) {
    
    while (createdActors > deadActors) {
        sem_wait(&somethingIsOnIdQueue);
        
        if (createdActors == deadActors) {
            return 0;
        }
        
        sem_wait(&idQueueLock);
            currentId = idFront(actorsIdsQueue);
            idPop(actorsIdsQueue);
        sem_post(&idQueueLock);
        
        sem_wait(&actorsArr[currentId]->msgQueueLock);
            message_t message = msgFront(actorsArr[currentId]->messages);
            msgPop(actorsArr[currentId]->messages);
        sem_post(&actorsArr[currentId]->msgQueueLock);
        
        if (message.message_type == MSG_SPAWN && !stopCreatingNewActors) {
            sem_wait(&newActorLock);
                actor_t *newActor = malloc(sizeof(actor_t));
                bool ok;
                if (newActor != NULL) {
                    ok = create_new_actor(newActor, (role_t *)message.data);
                }
            sem_post(&newActorLock);
            
            if (ok) {
                message_t msg = {MSG_HELLO, 1, (void*)currentId};
                send_message(newActor->id, msg);
            } else {
                free(newActor);
            }
        } else if (message.message_type == MSG_GODIE) {
            actorsArr[currentId]->goDieReceived = true;
        } else {
            actorsArr[currentId]->role->prompts[message.message_type](&actorsArr[currentId]->stateptr, message.nbytes, message.data);
        }
        
        
        if (actorsArr[currentId]->goDieReceived && msgEmpty(actorsArr[currentId]->messages)) {
            ++deadActors;
            actorsArr[currentId]->died = true;
        }
    }
    
    for (unsigned long i = 0; i < POOL_SIZE - 1; ++i) {
        sem_post(&somethingIsOnIdQueue);
    }
    
    return 0;
}

void SigintHandler(int signo) {
    if (signo != SIGINT) {
        return;
    }
    
    stopCreatingNewActors = true;
    
    deadActors = createdActors;
    
    for (actor_id_t i = 0; i < numberOfActorsInArray(); ++i) {
        if (actorsArr[i] != NULL) {
            actorsArr[i]->goDieReceived = true;
        }
    }
    
    for (unsigned int i = 0; i < POOL_SIZE; ++i) {
        sem_post(&somethingIsOnIdQueue);
    }
    
    actor_system_join((actor_id_t)0);
    
    exit(0);
}

void prepare_sigint() {
    struct sigaction newSigint;
    newSigint.sa_flags = SA_RESETHAND;
    sigemptyset(&newSigint.sa_mask);
    newSigint.sa_handler = SigintHandler;
    
    sigaction(SIGINT, &newSigint, NULL);
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (stopCreatingNewActors) {
        return -1;
    }
    
    actorsIdsQueue = newIdQueue();
    
    if (actorsIdsQueue == NULL) {
        return -2;
    }
    
    prepare_sigint();
    stopCreatingNewActors = false;
    
    if (sem_init(&somethingIsOnIdQueue, 0, 0) == -1) {
        free(actorsIdsQueue);
        return -3;
    }
    if (sem_init(&idQueueLock, 0, 1) == -1) {
        free(actorsIdsQueue);
        sem_destroy(&somethingIsOnIdQueue);
        return -4;
    }
    if (sem_init(&newActorLock, 0, 1) == -1) {
        free(actorsIdsQueue);
        sem_destroy(&somethingIsOnIdQueue);
        sem_destroy(&idQueueLock);
        return -5;
    }
    
    pthread_attr_t attr;
    pthread_attr_init (&attr);
    
    *actor = 0;
    actor_t *actorZero = malloc(sizeof(actor_t));
    if (actorZero == NULL) {
        free(actorsIdsQueue);
        sem_destroy(&somethingIsOnIdQueue);
        sem_destroy(&idQueueLock);
        sem_destroy(&newActorLock);
        return -6;
    }
    if (!create_new_actor(actorZero, role)) {
        free(actorsIdsQueue);
        sem_destroy(&somethingIsOnIdQueue);
        sem_destroy(&idQueueLock);
        sem_destroy(&newActorLock);
        free(actorZero);
        return -7;
    }
    
    if (sem_init(&actorZero->msgQueueLock, 0, 1) == -1) {
        free(actorsIdsQueue);
        sem_destroy(&somethingIsOnIdQueue);
        sem_destroy(&idQueueLock);
        sem_destroy(&newActorLock);
        free(actorZero);
        return -8;
    }
    
    message_t msg = {MSG_HELLO, 0, (void*)0};
    send_message((actor_id_t)0, msg);
    
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (pthread_create(&threads[i], &attr, worker, 0) != 0) {
            return -9;
        }
    }
    
    return 0;
}

void actor_system_join(actor_id_t actor) {
    if (actor >= createdActors && actorsArr[actor]->died) {
        return;
    }
    
    for (int i = 0; i < POOL_SIZE; ++i) {
        int* res;
        if ((err = pthread_join(threads[i], (void **) &res)) != 0) {
            exit(1);
        }
    }
    
    for (actor_id_t i = 0; i < numberOfActorsInArray(); ++i) {
        if (actorsArr[i] != NULL) {
            if (actorsArr[i]->messages != NULL) {
                while (!msgEmpty(actorsArr[i]->messages)) {
                    msgPop(actorsArr[i]->messages);
                }
            
                free(actorsArr[i]->messages);   
            }
            
            sem_destroy(&actorsArr[i]->msgQueueLock);
            
            if (actorsArr[i]->stateptr != NULL) {
                free(actorsArr[i]->stateptr);
            }
            
            free(actorsArr[i]);
        }
    }
    free(actorsArr);
    actorsArr = NULL;
    
    if (actorsIdsQueue != NULL) {
        while (!idEmpty(actorsIdsQueue)) {
            idPop(actorsIdsQueue);
        }
        
        free(actorsIdsQueue);
        actorsIdsQueue = NULL;
    }
    
    stopCreatingNewActors = true;
    
    arrSize = ind = 0;
    createdActors = deadActors = 0;
    
    sem_destroy(&somethingIsOnIdQueue);
    sem_destroy(&idQueueLock);
    sem_destroy(&newActorLock);
}

int send_message(actor_id_t actor, message_t message) {
    sem_wait(&newActorLock);
    if (actor >= numberOfActorsInArray() || actorsArr[actor]->died) {
        return -2;
    }
    sem_post(&newActorLock);
    
    if (actorsArr[actor]->goDieReceived) {
        return -1;
    }
    
    if (msgSize(actorsArr[actor]->messages) >= ACTOR_QUEUE_LIMIT) {
        return -3;
    }
    
    sem_wait(&actorsArr[actor]->msgQueueLock);
        msgPush(actorsArr[actor]->messages, message);
    sem_post(&actorsArr[actor]->msgQueueLock);
    
    idPush(actorsIdsQueue, actor);
    sem_post(&somethingIsOnIdQueue);
    
    return 0;
}

actor_id_t actor_id_self() {
    return currentId;
}
