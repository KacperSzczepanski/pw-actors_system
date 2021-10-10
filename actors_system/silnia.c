#include "cacti.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>

#ifdef __GNUC__
#define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#define UNUSED(x) UNUSED_ ## x
#endif

typedef unsigned long long int llu;

actor_id_t root;
sem_t lock;

llu arr[3];

static void hello_there(void **stateptr, size_t nbytes, void* data);
static void silnia(void **stateptr, size_t nbytes, void* data);
static void zwrotne_hello(void **stateptr, size_t nbytes, void* data);

static act_t f1[3] = {hello_there, zwrotne_hello, silnia};
static role_t r1 = {3, f1};

struct dane {
    actor_id_t ojciec;
    actor_id_t syn;
    llu wynik;
    int n, n_docelowe;
} silnia_t;

static void hello_there(void **stateptr, size_t UNUSED(nbytes), void* data) {
    actor_id_t actor = (actor_id_t)data;
    
    if (*stateptr == NULL) {
        *stateptr = malloc(sizeof(silnia_t));
        
        ((struct dane*)(*stateptr))->ojciec = actor;
        ((struct dane*)(*stateptr))->syn = -1;
        ((struct dane*)(*stateptr))->wynik = -1;
        ((struct dane*)(*stateptr))->n = -1;
        ((struct dane*)(*stateptr))->n_docelowe = -1;
        
        if (actor_id_self() != root) {
            message_t msg = {(message_type_t)1, 1, (void*)actor_id_self()};
            send_message(actor, msg);
        }
    }
}
static void zwrotne_hello(void **stateptr, size_t UNUSED(nbytes), void* data) {
    actor_id_t actor = (actor_id_t)data;
    
    ((struct dane*)(*stateptr))->syn = actor;
    
    sem_wait(&lock);
    arr[0] = ((struct dane*)(*stateptr))->n_docelowe;
    arr[1] = ((struct dane*)(*stateptr))->n;
    arr[2] = ((struct dane*)(*stateptr))->wynik;
    
    message_t msg = {(message_type_t)2, 3, (void*)arr};
    send_message(actor, msg);
    sem_post(&lock);
    
    message_t msg2 = {MSG_GODIE, 0, (void*)0};
    send_message(actor_id_self(), msg2);
}
static void silnia(void **stateptr, size_t UNUSED(nbytes), void* data) {
    llu *arr = (llu *)data;
    
    if (arr[0] == 0 || arr[0] == 1) {
        printf ("1\n");
        message_t msg = {MSG_GODIE, 0, (void*)0};
        send_message(actor_id_self(), msg);
        return;
    }
    
    ++arr[1];
    arr[2] *= arr[1];
    
    if (arr[1] < arr[0]) {
        
        ((struct dane*)(*stateptr))->wynik = arr[2];
        ((struct dane*)(*stateptr))->n = arr[1];
        ((struct dane*)(*stateptr))->n_docelowe = arr[0];
        
        message_t msg1 = {MSG_SPAWN, 1, &r1};
        send_message(actor_id_self(), msg1);
        
    } else {
        printf ("%llu\n", arr[2]);
        message_t msg = {MSG_GODIE, 0, (void*)0};
        send_message(actor_id_self(), msg);
    }
}

int n;

int main() {
    
    scanf ("%d", &n);
    
    sem_init(&lock, 0, 1);
    int ret = actor_system_create(&root, &r1);
    
    if (ret < 0) {
        exit(1);
    }
    
    arr[0] = n;
    arr[1] = 1;
    arr[2] = 1;
    message_t msg = {(message_type_t)2, 3, (void*)arr};
    send_message(root, msg);
    
    actor_system_join(root);
    
	return 0;
}
