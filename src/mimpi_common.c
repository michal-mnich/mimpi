/**
 * This file is for implementation of common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#include "mimpi_common.h"

_Noreturn void syserr(const char* fmt, ...) {
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, " (%d; %s)\n", errno, strerror(errno));
    exit(1);
}

_Noreturn void fatal(const char* fmt, ...) {
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    fprintf(stderr, "\n");
    exit(1);
}

/////////////////////////////////////////////////
// Put your implementation here

// allocate a single node
static node_t* node_create(int tag, int count, char* data) {
    node_t* new_node = (node_t*) malloc(sizeof(node_t));
    assert(new_node != NULL);

    new_node->tag = tag;
    new_node->count = count;
    new_node->data = data;
    new_node->next = NULL;

    return new_node;
}

static void node_destroy(node_t* node) {
    free(node->data); // this will be called only for unread messages
    free(node);
}

// free the space occupied by a list
static void list_destroy(node_t* head) {
    node_t* current = head;
    node_t* next;
    while (current != NULL) {
        next = current->next;
        node_destroy(current);
        current = next;
    }
}

// allocate a new buffer
buffer_t* buffer_create() {
    buffer_t* new_buf = (buffer_t*) malloc(sizeof(buffer_t));
    assert(new_buf != NULL);

    new_buf->front = NULL;
    new_buf->rear = NULL;

    return new_buf;
}

// free the space occupied by a buffer
void buffer_destroy(buffer_t* buf) {
    list_destroy(buf->front);
    free(buf);
}

// add message at the end of buffer
void buffer_add(buffer_t* buf, int tag, int count, char* data) {
    node_t* new_node = node_create(tag, count, data);
    if (buf->rear == NULL) {
        // buf->front must also be NULL
        buf->front = new_node;
        buf->rear = new_node;
    }
    else {
        buf->rear->next = new_node;
        buf->rear = new_node;
    }
}


char* extract_matching_data(buffer_t* buf, int tag, int count) {
    char* ret;
    node_t* prev = NULL;
    node_t* current = buf->front;

    while (current != NULL) {
        if ((current->tag == tag || tag == MIMPI_ANY_TAG) && current->count == count) {
            ret = current->data;
            if (current == buf->front && current == buf->rear) {
                buf->front = NULL;
                buf->rear = NULL;
            }
            else if (current == buf->front) {
                buf->front = current->next;
            }
            else if (current == buf->rear) {
                assert(prev != NULL);
                prev->next = NULL;
                buf->rear = prev;
            }
            else {
                assert(prev != NULL);
                prev->next = current->next;
            }

            // caller of this function will free the data from this node
            free(current);

            return ret;
        }
        prev = current;
        current = current->next;
    }

    return NULL;
}

// calculate a unique read file descriptor for transfer channel from process 'i' to process 'j'
int get_transfer_read_fd(int i, int j) {
    return 20 + 2 * (16 * i + j);
}

// calculate a unique write file descriptor for transfer channel from process 'i' to process 'j'
int get_transfer_write_fd(int i, int j) {
    return 20 + 2 * (16 * i + j) + 1;
}

void write_full(int fd, const void* data, size_t count) {
    size_t total_written = 0;
    ssize_t bytes_written;
    const char* buf = (const char*) data;
    while (total_written < count) {
        bytes_written = chsend(fd, buf + total_written, count - total_written);
        if (bytes_written == -1 && errno == EINTR) continue;
        ASSERT_SYS_OK(bytes_written);
        assert(bytes_written > 0);
        total_written += bytes_written;
    }
    assert(total_written == count);
}

void read_full(int fd, void* data, size_t count) {
    size_t total_read = 0;
    ssize_t bytes_read;
    char* buf = (char*) data;
    while (total_read < count) {
        bytes_read = chrecv(fd, buf + total_read, count - total_read);
        if (bytes_read == -1 && errno == EINTR) continue;
        ASSERT_SYS_OK(bytes_read);
        assert(bytes_read > 0);
        total_read += bytes_read;
    }
    assert(total_read == count);
}

// mimpirun
void close_all_transfer_fds(int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            ASSERT_SYS_OK(close(get_transfer_read_fd(i, j)));
            ASSERT_SYS_OK(close(get_transfer_write_fd(i, j)));
        }
    }
}

// MIMPI_Init
void close_foreign_transfer_fds(int rank, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i != rank && j != rank) {
                ASSERT_SYS_OK(close(get_transfer_read_fd(i, j)));
                ASSERT_SYS_OK(close(get_transfer_write_fd(i, j)));
            }
        }
    }
}

// MIMPI_Init
void close_my_incoming_transfer_write_fds(int rank, int n) {
    for (int i = 0; i < n; i++) {
        if (i != rank) {
            ASSERT_SYS_OK(close(get_transfer_write_fd(i, rank)));
        }
    }
}

// MIMPI_Init
void close_my_outgoing_transfer_read_fds(int rank, int n) {
    for (int i = 0; i < n; i++) {
        if (i != rank) {
            ASSERT_SYS_OK(close(get_transfer_read_fd(rank, i)));
        }
    }
}

// MIMPI_Finalize (used to trigger POLLHUPs)
void close_my_outgoing_transfer_write_fds(int rank, int n) {
    for (int i = 0; i < n; i++) {
        // including i == rank
        ASSERT_SYS_OK(close(get_transfer_write_fd(rank, i)));
    }
}

// MIPI_Finalize (used at the very end)
void close_my_incoming_transfer_read_fds(int rank, int n) {
    for (int i = 0; i < n; i++) {
        // including i == rank
        ASSERT_SYS_OK(close(get_transfer_read_fd(i, rank)));
    }
}

void dup_fd(int from_fd, int to_fd) {
    if (from_fd != to_fd) {
        ASSERT_SYS_OK(dup2(from_fd, to_fd));
        ASSERT_SYS_OK(close(from_fd));
    }
}

void* merge_data(const void* data1, size_t count1, const void* data2, size_t count2) {
    char* new_data = (char*) malloc(count1 + count2);
    assert(new_data != NULL);
    memcpy(new_data, (char*)data1, count1);
    memcpy(new_data + count1, (char*)data2, count2);
    return new_data;
}

void partially_reduce(u_int8_t* partial, const u_int8_t* update, int count, MIMPI_Op op) {
    for (int i = 0; i < count; i++) {
        switch (op) {
            case MIMPI_MAX:
                partial[i] = MAX(partial[i], update[i]);
                break;
            case MIMPI_MIN:
                partial[i] = MIN(partial[i], update[i]);
                break;
            case MIMPI_SUM:
                partial[i] = SUM(partial[i], update[i]);
                break;
            case MIMPI_PROD:
                partial[i] = PROD(partial[i], update[i]);
                break;
        }
    }
}


