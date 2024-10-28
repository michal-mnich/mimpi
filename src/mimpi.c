/**
 * This file is for implementation of MIMPI library.
 * */

#include <stdlib.h>
#include "channel.h"
#include "mimpi.h"
#include "mimpi_common.h"

static bool detection;
static bool deadlock;

static int my_world_rank;
static int my_world_size;

volatile static int match_source;
volatile static int match_tag;
volatile static int match_count;
volatile static char* match_data;

static bool* exited;
volatile static int num_exited;

static int parent;
static int left;
static int right;
static int num_children;

static struct pollfd* fds;
static pthread_t worker;
static pthread_mutex_t worker_mutex;
static pthread_cond_t wait_recv;
static pthread_cond_t wait_group;

static buffer_t** buffers;
static buffer_t* log;

static bool check_deadlock(int source, int tag, int count) {
    node_t* curr = log->front;
    fprintf(stderr, "enter\n");
    while (curr != NULL) {
        fprintf(stderr, "%d %d %d\n", *(curr->data), curr->tag, curr->count);
        if (*(curr->data) == (char)source && curr->tag == tag && curr->count == count) break;
        curr = curr->next;
    }
    if (curr == NULL) return false;
    curr = curr->next;
    while (curr != NULL) {
        if (*(curr->data) == (char)16 && curr->tag == tag && curr->count == count) return false;
        curr = curr->next;
    }
    return true;
}

static void handle_poll_error(int source) {
    char* code = NULL;
    if (fds[source].revents & POLLERR) code = "POLLERR";
    if (fds[source].revents & POLLNVAL) code = "POLLNVAL";
    if (code != NULL) {
        fprintf(stderr, "Poll error: fd %d, channel %d -> %d, code %s\n", fds[source].fd, source, my_world_rank, code);
        assert(false);
    }
}

static void handle_incoming_message(int source) {
    int fd = fds[source].fd;

    // read tag
    int tag;
    read_full(fd, &tag, sizeof(int));

    // read count
    int count;
    read_full(fd, &count, sizeof(int));

    if (detection && tag == DEADLOCK_TAG) {
        node_t* tmp = (node_t*) malloc(sizeof(node_t));
        read_full(fd, tmp, sizeof(node_t));

        ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

        buffer_add(log, tmp->tag, tmp->count, tmp->data);
        fprintf(stderr, "%d %d %d\n", 1, tmp->tag, tmp->count);

        ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));
    }
    else {
        // allocate memory for data and read it
        char* data = (char*) malloc(count * sizeof(char));
        assert(data != NULL);
        read_full(fd, data, count);

        ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

        buffer_add(buffers[source], tag, count, data);

        ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));
    }
}

static void handle_signal_recv(int source) {
    // assumes locked mutex
    if (detection && match_source == source) {
        deadlock = deadlock || check_deadlock(source, match_tag, match_count);
        if (deadlock) {
            ASSERT_ZERO(pthread_cond_signal(&wait_recv));
        }
    }
    else if (match_source == source && match_data == NULL) {
        match_data = extract_matching_data(buffers[match_source], match_tag, match_count);
        if (match_data != NULL || exited[match_source]) {
            ASSERT_ZERO(pthread_cond_signal(&wait_recv));
        }
    }
}

// poll read fds of channels i -> my_world_rank
static void poll_transfer_read_init() {
    for (int i = 0; i < my_world_size; i++) {
        fds[i].fd = get_transfer_read_fd(i, my_world_rank);
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
}

// worker thread code
static void* worker_runnable(void* arg) {
    (void) arg;
    while (true) {
        // poll is used with timeout set to -1, which means no timeout
        ASSERT_SYS_OK(poll(fds, my_world_size, -1));

        for (int i = 0; i < my_world_size; i++) {
            handle_poll_error(i);
            if (fds[i].revents & POLLIN) {
                // fprintf(stderr, "POLLIN  %d -> %d\n", i, my_world_rank);
                // incoming message
                handle_incoming_message(i);

                ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

                handle_signal_recv(i);

                ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));
            }
            else if ((fds[i].revents & POLLHUP) && !exited[i]) {
                // fprintf(stderr, "POLLHUP %d -> %d\n", i, my_world_rank);
                // process 'i' is in MIMPI_Finalize and its channel is empty

                ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

                exited[i] = true;
                handle_signal_recv(i);

                ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));

                if (++num_exited == my_world_size) return NULL;
            }
        }
    }
}

void MIMPI_Init(bool enable_deadlock_detection) {
    deadlock = false;
    detection = enable_deadlock_detection;
    channels_init();

    my_world_rank = MIMPI_World_rank();
    my_world_size = MIMPI_World_size();

    // close transfer channels that do not belong to this process
    close_foreign_transfer_fds(my_world_rank, my_world_size);

    // close unnecessary transfer channel ends
    close_my_incoming_transfer_write_fds(my_world_rank, my_world_size);
    close_my_outgoing_transfer_read_fds(my_world_rank, my_world_size);

    match_source = -1;
    match_tag = -1;
    match_count = -1;
    match_data = NULL;

    num_exited = 0;

    parent = (my_world_rank - 1) / 2;
    left = 2 * my_world_rank + 1;
    right = 2 * my_world_rank + 2;
    num_children = (left < my_world_size) + (right < my_world_size);

    exited = (bool*) malloc(my_world_size * sizeof(bool));
    buffers = (buffer_t**) malloc(my_world_size * sizeof(buffer_t*));
    fds = (struct pollfd*) malloc(my_world_size * sizeof(struct pollfd));
    assert(exited != NULL);
    assert(buffers != NULL);
    assert(fds != NULL);

    for (int i = 0; i < my_world_size; i++) {
        exited[i] = false;
        buffers[i] = buffer_create();
    }

    log = (buffer_t*) malloc(sizeof(buffer_t));
    assert(log != NULL);
    log = buffer_create();

    // start worker thread that polls incoming channels
    poll_transfer_read_init();
    ASSERT_ZERO(pthread_mutex_init(&worker_mutex, NULL));
    ASSERT_ZERO(pthread_cond_init(&wait_recv, NULL));
    ASSERT_ZERO(pthread_cond_init(&wait_group, NULL));
    ASSERT_ZERO(pthread_create(&worker, NULL, worker_runnable, NULL));
}

void MIMPI_Finalize() {
    // generate POLLHUP in every worker for every one of my outgoing channels
    close_my_outgoing_transfer_write_fds(my_world_rank, my_world_size);

    // synchronize on all processes' MIMPI_Finalize (beacuse worker only returns when all processes have sent exit_event)
    ASSERT_ZERO(pthread_join(worker, NULL));

    // close channel ends that were polled by worker
    close_my_incoming_transfer_read_fds(my_world_rank, my_world_size);

    // destroy pthread variables
    ASSERT_ZERO(pthread_mutex_destroy(&worker_mutex));
    ASSERT_ZERO(pthread_cond_destroy(&wait_recv));
    ASSERT_ZERO(pthread_cond_destroy(&wait_group));

    // fprintf(stderr, "rank %d\n", my_world_rank);
    for (int i = 0; i < my_world_size; i++) {
        buffer_destroy(buffers[i]);
    }

    free(exited);
    free(buffers);
    free(log);
    free(fds);

    assert(match_source == -1);
    assert(match_tag == -1);
    assert(match_count == -1);
    assert(match_data == NULL);

    channels_finalize();
}

int MIMPI_World_size() {
    return atoi(getenv("MIMPI_WORLD_SIZE"));
}

int MIMPI_World_rank() {
    return atoi(getenv("MIMPI_WORLD_RANK"));
}

MIMPI_Retcode MIMPI_Send(void const* data, int count, int destination, int tag) {
    // check for errors
    if (destination == my_world_rank) {
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;
    }
    if (destination < 0 || destination >= my_world_size) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }

    ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

    if (exited[destination]) {
        ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));
        return MIMPI_ERROR_REMOTE_FINISHED;
    }

    ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));

    char* combined1 = merge_data(&tag, sizeof(int), &count, sizeof(int));
    char* combined2 = merge_data(combined1, 2 * sizeof(int), data, count);

    write_full(get_transfer_write_fd(my_world_rank, destination), combined2, 2 * sizeof(int) + (size_t)count);

    free(combined1);
    free(combined2);

    if (detection && tag >= 0) {
        ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

        fprintf(stderr, "send: tag: %d count: %d dest: %d\n", tag, count, destination);
        buffer_add(log, tag, count, &(char) {16});

        ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));

    }

    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Recv(void* data, int count, int source, int tag) {
    // check for errors
    if (source == my_world_rank) {
        return MIMPI_ERROR_ATTEMPTED_SELF_OP;
    }
    if (source < 0 || source >= my_world_size) {
        return MIMPI_ERROR_NO_SUCH_RANK;
    }

    ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

    match_data = extract_matching_data(buffers[source], tag, count);

    if (match_data == NULL && !exited[source] && detection) {
        ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));

        node_t* tosend = node_create(tag, count, &(char) {my_world_rank});
        MIMPI_Send(tosend, sizeof(node_t), source, DEADLOCK_TAG);
        // node_destroy(tosend);

        ASSERT_ZERO(pthread_mutex_lock(&worker_mutex));

        deadlock = deadlock || check_deadlock(source, tag, count);
    }

    while (match_data == NULL && !exited[source] && !deadlock) {
        match_source = source;
        match_tag = tag;
        match_count = count;
        ASSERT_ZERO(pthread_cond_wait(&wait_recv, &worker_mutex));
        match_source = -1;
        match_tag = -1;
        match_count = -1;
    }

    int ret;
    if (match_data != NULL) {
        memcpy(data, (char*)match_data, count);
        free((char*)match_data);
        match_data = NULL;
        ret = MIMPI_SUCCESS;
    }
    else if (deadlock)
        ret = MIMPI_ERROR_DEADLOCK_DETECTED;
    else {
        assert(exited[source]);
        ret = MIMPI_ERROR_REMOTE_FINISHED;
    }

    ASSERT_ZERO(pthread_mutex_unlock(&worker_mutex));

    return ret;
}

MIMPI_Retcode MIMPI_Barrier() {
    char buf;

    // wait for children to enter this function
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK(MIMPI_Recv(&buf, 1, left + i, BARRIER_TAG));
        assert(buf == BARRIER_WAIT);
    }

    if (my_world_rank != 0) {
        // notify parent that this process has entered this function or propagate error
        MIMPI_CHECK(MIMPI_Send(&(char) {BARRIER_WAIT}, 1, parent, BARRIER_TAG));

        // wait for parent to wake this process up or register error
        MIMPI_CHECK(MIMPI_Recv(&buf, 1, parent, BARRIER_TAG));
        assert(buf == BARRIER_WAKE);
    }

    // wake up children or propagate error
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK(MIMPI_Send(&(char) {BARRIER_WAKE}, 1, left + i, BARRIER_TAG));
    }

    return MIMPI_SUCCESS;
}

static bool is_bcast_path(int v, int root) {
    if (v == root) return true;
    int curr = root;
    while (curr != 0) {
        curr = (curr - 1) / 2;
        if (curr == v) return true;
    }
    return false;
}

MIMPI_Retcode MIMPI_Bcast(void* data, int count, int root) {
    // check error
    if (root < 0 || root >= my_world_size) return MIMPI_ERROR_NO_SUCH_RANK;

    char* buf = (char*) malloc(count * sizeof(char));
    assert(buf != NULL);

    // to be safe initialize our data to zeros
    if (my_world_rank != root) memset(data, 0, count);

    // wait for children to enter this function
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK1(MIMPI_Recv(buf, count, left + i, BCAST_TAG), buf);
        if (is_bcast_path(left + i, root)) {
            // receive bcast data from child
            memcpy(data, buf, count);
        }
    }

    free(buf);

    if (my_world_rank != 0) {
        // notify parent that we are waiting
        MIMPI_CHECK(MIMPI_Send(data, count, parent, BCAST_TAG));

        // wait for parent to send bcast data or register error
        MIMPI_CHECK(MIMPI_Recv(data, count, parent, BCAST_TAG));
    }

    // send bcast data to children or propagate error
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK(MIMPI_Send(data, count, left + i, BCAST_TAG));
    }

    return MIMPI_SUCCESS;
}

MIMPI_Retcode MIMPI_Reduce(void const* send_data, void* recv_data, int count, MIMPI_Op op, int root) {
    // check error
    if (root < 0 || root >= my_world_size) return MIMPI_ERROR_NO_SUCH_RANK;

    u_int8_t* partial = (u_int8_t*) malloc(count * sizeof(u_int8_t));
    assert(partial != NULL);
    u_int8_t* buf = (u_int8_t*) malloc(count * sizeof(u_int8_t));
    assert(buf != NULL);

    // initialize partial result as this process' data
    memcpy(partial, send_data, count);

    // wait for children to enter this function
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK2(MIMPI_Recv(buf, count, left + i, REDUCE_TAG), partial, buf);
        // receive partial result from child and update this process' partial result
        partially_reduce(partial, buf, count, op);
    }

    free(buf);

    if (my_world_rank != 0) {
        // send partial result to parent
        MIMPI_CHECK1(MIMPI_Send(partial, count, parent, REDUCE_TAG), partial);

        // wait for parent to send complete result or register error
        MIMPI_CHECK1(MIMPI_Recv(partial, count, parent, REDUCE_TAG), partial);
    }

    // write complete result
    if (my_world_rank == root) {
        memcpy(recv_data, partial, count);
    }

    // send complete result to children or propagate error
    for (int i = 0; i < num_children; i++) {
        MIMPI_CHECK1(MIMPI_Send(partial, count, left + i, REDUCE_TAG), partial);
    }

    free(partial);

    return MIMPI_SUCCESS;
}