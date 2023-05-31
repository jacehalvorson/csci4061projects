#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connection_queue.h"

int connection_queue_init(connection_queue_t *queue) {
    memset(&queue->client_fds, -1, CAPACITY * sizeof(int));
    queue->length = 0;
    queue->read_idx = 0; // read_idx == write_idx --> length == 0
    queue->write_idx = 0;
    queue->shutdown = 0; // if changed to 1, all enqueues and dequeues should stop

    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        fprintf(stderr, "pthread_mutex_init failed\n");
        return -1;
    }
    if (pthread_cond_init(&queue->empty, NULL) != 0 ||
        pthread_cond_init(&queue->full, NULL) != 0)
    {
        fprintf(stderr, "pthread_cond_init failed\n");
        return -1;
    } // destroyed in connection_queue_free
    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    // printf("enqueue %d\n", connection_fd); // debugging
    if (pthread_mutex_lock(&queue->lock) != 0) {
        fprintf(stderr, "pthread_mutex_lock failed\n");
        return -1;
    }

    // check shutdown bc wait
    if (queue->shutdown) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            fprintf(stderr, "pthread_mutex_unlock failed\n");
            return -1;
        }
        return 0; // successfully shut down
    }

    while (queue->length >= CAPACITY && !queue->shutdown) {
        // printf("inside while, length = %d\n", queue->length);
        if (pthread_cond_wait(&queue->full, &queue->lock) != 0) { // wait until there's an open slot
            fprintf(stderr, "pthread_cond_wait");
            return -1;
        }
    }
    
    // check shutdown again because another wait
    if (queue->shutdown) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            fprintf(stderr, "pthread_mutex_unlock failed\n");
            return -1;
        }
        return 0; // successfully shut down
    }

    // add item to queue
    queue->client_fds[queue->write_idx] = connection_fd; //update array
    queue->write_idx = (queue->write_idx + 1) % CAPACITY; // increment write_idx
    queue->length++;

    if (pthread_cond_signal(&queue->empty) != 0) { // if queue was empty before, it no longer is
        fprintf(stderr, "pthread_cond_signal failed\n");
        return -1;
    }
    if (pthread_mutex_unlock(&queue->lock) != 0) { // release lock
        fprintf(stderr, "pthread_mutex_unlock failed\n");
        return -1;
    }

    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    // printf("dequeue %d (if it's -1 that means this thread is waiting for a value)\n", queue->client_fds[queue->read_idx]); // debugging
    if (pthread_mutex_lock(&queue->lock) != 0) {
        perror("pthread_mutex_lock");
        return -1;
    }

    // check shutdown bc wait
    if (queue->shutdown) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            fprintf(stderr, "pthread_mutex_unlock failed\n");
            return -1;
        }
        return 0; // successfully shut down
    }

    while (queue->length <= 0 && !queue->shutdown) {
        // printf("inside while, queue->length = %d, shutdown = %d\n", queue->length, queue->shutdown); // debugging
        if (pthread_cond_wait(&queue->empty, &queue->lock) != 0) { // wait until there's an open slot
            fprintf(stderr, "pthread_cond_wait failed\n");
            return -1;
        }
    }
    
    // printf("signaled, checking shutdown = %d\n", queue->shutdown);
    // check shutdown again because another wait
    if (queue->shutdown) {
        if (pthread_mutex_unlock(&queue->lock) != 0) {
            fprintf(stderr, "pthread_mutex_unlock failed\n");
            return -1;
        }
        return 0; // successfully shut down
    }

    // add item to queue
    int ret_val = queue->client_fds[queue->read_idx]; //read from front of queue
    queue->read_idx = (queue->read_idx + 1) % CAPACITY; // increment write_idx
    queue->length--;

    if (pthread_cond_signal(&queue->full) != 0) { // if queue was full before, it no longer is
        fprintf(stderr, "pthread_cond_signal failed\n");
        return -1;
    }
    if (pthread_mutex_unlock(&queue->lock) != 0) { // release lock
        fprintf(stderr, "pthread_mutex_unlock failed\n");
        return -1;
    }

    return ret_val;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    queue->shutdown = 1;
    int err;
    // wake up all other threads
    err = pthread_cond_broadcast(&queue->empty);
    if (err != 0) { //this SHOULD wake up all threads waiting on empty
        fprintf(stderr, "pthread_cond_broadcast errno %d", err);
        return -1;
    }
    err = pthread_cond_broadcast(&queue->full);
    if (err != 0) { //this SHOULD wake up all threads waiting on full
        fprintf(stderr, "pthread_cond_broadcast errno %d", err);
        return -1;
    }
    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    int ret_val = 0;
    // threads already waited for before call to this function

    if (pthread_mutex_destroy(&queue->lock) != 0 ||
        pthread_cond_destroy(&queue->full) != 0 ||
        pthread_cond_destroy(&queue->empty) != 0)
    {
        fprintf(stderr, "pthread_mutex_destroy failed\n");
        ret_val = -1;
    }
    
    free(queue);
    return ret_val;
}

// int main(int argc, char *argv[]) {
//     connection_queue_t* q = malloc(sizeof(connection_queue_t));
//     if (connection_queue_init(q) == -1) {
//         printf("init didn't work\n");
//     }
//     connection_enqueue(q, 1);
//     printf("past enqueue\n");
//     connection_enqueue(q, 2);
//     connection_enqueue(q, 3);
//     printf("%d\n", connection_dequeue(q));
//     printf("%d\n", connection_dequeue(q));
//     printf("%d\n", connection_dequeue(q));
//     printf("about to free\n");
//     connection_queue_free(q);
//     return 0;
// }