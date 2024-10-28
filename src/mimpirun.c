/**
 * This file is for implementation of mimpirun program.
 * */

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include "mimpi_common.h"
#include "channel.h"

int main(int argc, char* argv[]) {

    assert(argc >= 3);

    // number of copies of the program to be exec'd
    int n = atoi(argv[1]);
    assert(1 <= n && n <= 16);

    int tmp[2];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            ASSERT_SYS_OK(channel(tmp));
            dup_fd(tmp[0], get_transfer_read_fd(i, j));
            dup_fd(tmp[1], get_transfer_write_fd(i, j));
        }
    }

    // starting all copies
    char buf[12];
    pid_t pid;
    for (int i = 0; i < n; i++) {
        ASSERT_SYS_OK(pid = fork());
        if (!pid) { // child process
            // setting env vars
            sprintf(buf, "%d", i);
            ASSERT_SYS_OK(setenv("MIMPI_WORLD_RANK", buf, 1));
            ASSERT_SYS_OK(setenv("MIMPI_WORLD_SIZE", argv[1], 1));

            // exec copy
            ASSERT_SYS_OK(execvp(argv[2], argv + 2));
        }
    }

    // closing unnecessary file descriptors (all created above)
    close_all_transfer_fds(n);

    // waiting for all copies
    int ret = 0;
    int ret_child;
    for (int i = 0; i < n; i++) {
        ASSERT_SYS_OK(wait(&ret_child));
        if (WIFEXITED(ret_child) && WEXITSTATUS(ret_child) != 0) {
            ret = WEXITSTATUS(ret_child);
        }
    }

    return ret;

}