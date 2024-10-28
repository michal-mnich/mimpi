/**
 * This file is for declarations of  common interfaces used in both
 * MIMPI library (mimpi.c) and mimpirun program (mimpirun.c).
 * */

#ifndef MIMPI_COMMON_H
#define MIMPI_COMMON_H

#include <assert.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include "channel.h"
#include "mimpi.h"

/*
    Assert that expression doesn't evaluate to -1 (as almost every system function does in case of error).

    Use as a function, with a semicolon, like: ASSERT_SYS_OK(close(fd));
    (This is implemented with a 'do { ... } while(0)' block so that it can be used between if () and else.)
*/
#define ASSERT_SYS_OK(expr)                                                                \
    do {                                                                                   \
        if ((expr) == -1)                                                                  \
            syserr(                                                                        \
                "system command failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ", \
                #expr, __func__, __FILE__, __LINE__                                        \
            );                                                                             \
    } while(0)

/* Assert that expression evaluates to zero (otherwise use result as error number, as in pthreads). */
#define ASSERT_ZERO(expr)                                                                  \
    do {                                                                                   \
        int const _errno = (expr);                                                         \
        if (_errno != 0)                                                                   \
            syserr(                                                                        \
                "Failed: %s\n\tIn function %s() in %s line %d.\n\tErrno: ",                \
                #expr, __func__, __FILE__, __LINE__                                        \
                \
            );                                                                             \
    } while(0)

/* Prints with information about system error (errno) and quits. */
_Noreturn extern void syserr(const char* fmt, ...);

/* Prints (like printf) and quits. */
_Noreturn extern void fatal(const char* fmt, ...);

#define TODO fatal("UNIMPLEMENTED function %s", __PRETTY_FUNCTION__);


/////////////////////////////////////////////
// Put your declarations here

#define BARRIER_WAIT 10
#define BARRIER_WAKE 20

#define BARRIER_TAG -2
#define BCAST_TAG -3
#define REDUCE_TAG -4
#define DEADLOCK_TAG -5

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define SUM(x, y) ((x) + (y))
#define PROD(x, y) ((x) * (y))

#define MIMPI_CHECK(expr)                        \
    do {                                         \
        MIMPI_Retcode ret = expr;                \
        if (ret != MIMPI_SUCCESS) {              \
            return ret;                          \
        }                                        \
    } while(0)

#define MIMPI_CHECK1(expr, res1)                 \
    do {                                         \
        MIMPI_Retcode ret = expr;                \
        if (ret != MIMPI_SUCCESS) {              \
            free(res1);                          \
            return ret;                          \
        }                                        \
    } while(0)

#define MIMPI_CHECK2(expr, res1, res2)           \
    do {                                         \
        MIMPI_Retcode ret = expr;                \
        if (ret != MIMPI_SUCCESS) {              \
            free(res1);                          \
            free(res2);                          \
            return ret;                          \
        }                                        \
    } while(0)

typedef struct Node {
    int tag;
    int count;
    char* data;
    struct Node* next;
} node_t;

typedef struct Buffer {
    node_t* front;
    node_t* rear;
} buffer_t;

typedef struct Entry {
    int tag;
    int count;
    int whoswaiting;
} entry_t;


buffer_t* buffer_create();

void buffer_destroy(buffer_t* buf);

void buffer_add(buffer_t* buf, int tag, int count, char* data);

char* extract_matching_data(buffer_t* buf, int tag, int count);

int get_transfer_read_fd(int i, int j);

int get_transfer_write_fd(int i, int j);

void close_all_transfer_fds(int n);

void close_foreign_transfer_fds(int rank, int n);

void close_my_incoming_transfer_write_fds(int rank, int n);

void close_my_outgoing_transfer_read_fds(int rank, int n);

void close_my_outgoing_transfer_write_fds(int rank, int n);

void close_my_incoming_transfer_read_fds(int rank, int n);

void write_full(int fd, const void* data, size_t n);

void read_full(int fd, void* data, size_t count);

void dup_fd(int from_fd, int to_fd);

void* merge_data(const void* data1, size_t count1, const void* data2, size_t count2);

void partially_reduce(u_int8_t* partial, const u_int8_t* update, int count, MIMPI_Op op);

#endif // MIMPI_COMMON_H