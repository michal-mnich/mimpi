# MIMPI

[MPI](https://en.wikipedia.org/wiki/Message_Passing_Interface) is a standard communication protocol used for exchanging data between processes in parallel programs, primarily used in supercomputing. The goal of this project, as suggested by the name _MIMPI_ — an acronym for _My Implementation of MPI_ — is to implement a small, slightly modified fragment of MPI. The project includes:

- The `mimpirun` program code (in `mimpirun.c`) that runs parallel computations.
- The implementation (in `mimpi.c`) of procedures declared in `mimpi.h`.

## Program

The `mimpirun` program accepts the following command-line arguments:

1. $n$ - the number of copies to run (a natural number between 1 and 16 inclusive).
2. $prog$ - the path to the executable file (it may be located in PATH). If the `exec` call fails (e.g., due to an incorrect path), `mimpirun` terminates with a non-zero exit code.
3. $args$ - optionally, any number of arguments to pass to all instances of the $prog$ program.

The `mimpirun` program performs the following steps sequentially (the next step starts only after the previous one is fully completed):

1. Prepares the environment.
2. Runs $n$ copies of the $prog$ program, each in a separate process.
3. Waits for all created processes to finish.
4. Terminates.

## Assumptions about $prog$ Programs

- $prog$ programs can **enter the MPI block** only once during their execution. To do this, they call the `MIMPI_Init` library function at the beginning and the `MIMPI_Finalize` function at the end. The MPI block is understood as the entire code segment between these calls.
- While in the MPI block, programs can execute various procedures from the `mimpi` library to communicate with other $prog$ processes.
- They can perform any operations (read, write, open, close, etc.) on files whose file descriptors are in the ranges `0,19` and `1024, ∞)` (including `STDIN`, `STDOUT`, and `STDERR`).
- They do not modify environment variables starting with the prefix `MIMPI`.
- They expect properly set arguments, i.e., the zeroth argument should be the name of the $prog$ program, and the subsequent arguments should correspond to the $args$ arguments.

## `mimpi` Library

### Auxiliary Procedures

- `void MIMPI_Init(bool enable_deadlock_detection)`

  Opens the MPI block, initializing the resources needed for the `mimpi` library.

- `void MIMPI_Finalize()`

  Closes the MPI block. All resources related to the operation of the `mimpi` library:
  - open files
  - open communication channels
  - allocated memory
  - synchronization primitives
  - etc.

  are released before this procedure ends.

- `int MIMPI_World_size()`

  Returns the number of $prog$ processes started using the `mimpirun` program (equal to the $n$ parameter passed to `mimpirun`).

- `void MIMPI_World_rank()`

  Returns a unique identifier within the group of processes started by `mimpirun` . Identifiers are consecutive natural numbers from $0$ to $n-1$.

### Point-to-Point Communication Procedures

- `MIMPI_Retcode MIMPI_Send(void const *data, int count, int destination, int tag)`

  Sends data from the `data` address, interpreting it as an array of bytes of size `count`, to the process with rank `destination`, tagging the message with `tag`.

  Executing `MIMPI_Send` for a process that has already left the MPI block immediately fails, returning the error code `MIMPI_ERROR_REMOTE_FINISHED`. Do not worry about the situation where the process for which `MIMPI_Send` was executed terminates later (after the successful completion of the `MIMPI_send` function in the sending process).

- `MIMPI_Retcode MIMPI_Recv(void *data, int count, int source, int tag)`

  Waits for a message of size (exactly) `count` and tag `tag` from the process with rank `rank` and writes its content to the `data` address (it is the caller's responsibility to ensure sufficient allocated memory). The call is blocking, i.e., it ends only after receiving the entire message.

  Executing `MIMPI_Recv` for a process that has not sent a matching message and has already left the MPI block fails, returning the error code `MIMPI_ERROR_REMOTE_FINISHED`. Similar behavior occurs even if the other process leaves the MPI block while waiting for `MIMPI_Recv`.

  - Messages can be of any (reasonable) size, in particular larger than the link buffer (`pipe`).
  - The recipient buffers incoming packets, and when `MIMPI_Recv` is called, returns the first (in terms of arrival time) message matching the `count`, `source`, and `tag` parameters.
  - The recipient processes incoming messages concurrently with performing other tasks, so as not to overflow the message sending channels. In other words, sending a large number of messages is non-blocking even if the target recipient does not process them (because they go into an ever-growing buffer).

### Group Communication Procedures

#### General Requirements

Each group communication procedure $p$ is a **synchronization point** for all processes, i.e., instructions following the $i$-th call to $p$ in any process execute **after** every instruction preceding the $i$-th call to $p$ in any other process.

If the synchronization of all processes cannot be completed because one of the processes has already left the MPI block, the `MIMPI_Barrier` call in at least one process ends with the error code `MIMPI_ERROR_REMOTE_FINISHED`. If the process in which this happens terminates in response to the error, the `MIMPI_Barrier` call ends in at least one subsequent process. Repeating this behavior leads to a situation where each process has left the barrier with an error.

#### Efficiency

Each group communication procedure $p$ is implemented efficiently. The time from the last process calling $p$ to the time $p$ completes in the last process is at most $\lceil w / 256 \rceil(3\left \lceil\log_2(n+1)-1 \right \rceil t+\epsilon)$, where:

- $n$ is the number of processes
- $t$ is the longest execution time of `chsend` related to sending a single message within the given group communication function call.
- $\epsilon$ is a small constant (on the order of milliseconds at most) that does not depend on $t$
- $w$ is the size in bytes of the message processed in the given group communication function call (for the `MIMPI_Barrier` call, assume $w=1$)

Additionally, the transmitted data is not accompanied by too much metadata. Specifically, the group functions called for data sizes less than 256 bytes will call `chsend` and `chrecv` for packets of size less than or equal to 512 bytes.

#### Available Procedures

- `MIMPI_Retcode MIMPI_Barrier()`

  Synchronizes all processes.

- `MIMPI_Retcode MIMPI_Bcast(void *data, int count, int root)`

  Sends data provided by the process with rank `root` to all other processes.

- `MIMPI_Retcode MIMPI_Reduce(const void *send_data, void *recv_data, int count, MPI_Op op, int root)`

  Collects data provided by all processes in `send_data` (treating it as an array of `uint8_t` numbers of size `count`) and performs a reduction of type `op` on elements with the same indices from the `send_data` arrays of all processes (including `root`). The result of the reduction, i.e., an array of `uint8_t` of size `count`, is written to the `recv_data` address **only** in the process with rank `root`.

  The following reduction types (values of the `enum` `MIMPI_Op`) are available:
  - `MIMPI_MAX`: maximum
  - `MIMPI_MIN`: minimum
  - `MIMPI_SUM`: sum
  - `MIMPI_PROD`: product

  Note that all the above operations on available data types are commutative and associative, and `MIMPI_Reduce` is optimized accordingly.

### Semantics of `MIMPI_Retcode`

Refer to the documentation in the `mimpi.h` code:

- documentation of `MIMPI_Retcode`,
- documentation of individual procedures returning `MIMPI_Retcode`.

### Semantics of Tags

We adopt the convention:

- `tag > 0` is intended for library users for their own needs,
- `tag = 0` means `ANY_TAG`. Its use in `MIMPI_Recv` causes matching to any tag. It should not be used in `MIMPI_Send` (the effect of its use is undefined).
- `tag < 0` is reserved for library implementers and is used for internal communication.

  In particular, this means that user programs must never directly call the `MIMPI_Send` or `MIMPI_Recv` procedures with a tag `< 0`.

## Interprocess Communication

The MPI standard is designed for computations run on supercomputers. Therefore, communication between individual processes usually takes place over a network and is slower than data exchange within a single computer.

To better simulate the environment of a real library and thus face its implementation challenges, interprocess communication is conducted **exclusively** using the channels provided in the `channel.h` library. The `channel.h` library provides the following functions for channel handling:

- `void channels_init()` - initializes the channel library
- `void channels_finalize()` - finalizes the channel library
- `int channel(int pipefd2)` - creates a channel
- `int chsend(int __fd, const void *__buf, size_t __n)` - sends a message
- `int chrecv(int __fd, void *__buf, size_t __nbytes)` - receives a message

`channel`, `chsend`, `chrecv` work similarly to `pipe`, `write`, and `read` respectively. The idea is that the only significant difference in the behavior of the functions provided by `channel.h` is that they may have significantly longer execution times than their originals. Specifically, the provided functions:

- have the same signature as the original functions
- similarly create entries in the open file table
- guarantee atomic reads and writes up to 512 bytes inclusive
- guarantee a buffer size of at least 4 KB

**NOTE:**
The following auxiliary functions must be called: `channels_init` from `MIMPI_Init`, and `channels_finalize` from `MIMPI_Finalize`.

**All** reads and writes to file descriptors returned by the `channel` function are performed using `chsend` and `chrecv`. Additionally, system functions that modify file properties like `fcntl` are never called on file descriptors returned by the `channel` function.

Remember that the guarantees provided by the `chsend` and `chrecv` functions do not imply that they will not process fewer bytes than requested. This may happen if the size exceeds the guaranteed channel buffer size or if the amount of data in the input buffer is insufficient.

## Notes

### General

- The `mimpirun` program and any functions from the `mimpi` library **do not** create named files in the file system.
- The `mimpirun` program and functions from the `mimpi` library use file descriptors in the range $20, 1023$. Make sure that file descriptors in the above range are not occupied when the `mimpirun` program starts.
- The `mimpirun` program and any functions from the `mimpi` library **do not** modify existing entries in the open file table from positions outside $20, 1023$.
- The `mimpirun` program and any functions from the `mimpi` library **do not** perform any operations on files they did not open themselves (especially on `STDIN`, `STDOUT`, and `STDERR`).
- Active or semi-active waiting is not used anywhere.
- No memory and/or other resource leaks (unclosed files, etc.).
- Assumes that corresponding $i$-th calls to group communication functions in different processes are of the same type (they are the same functions) and have the same values for the `count`, `root`, and `op` parameters (if the current function type has such a parameter).
- In case of an error in a system function, the calling program is terminated with a non-zero exit code.
