# Asynchronous I/O in Linux

Asynchronous I/O (AIO) allows programs to initiate I/O operations and continue executing other tasks while the I/O completes in the background. This is crucial for high-performance applications that need to handle multiple I/O operations efficiently without blocking.

Linux provides two main AIO interfaces: POSIX AIO and Linux native AIO (libaio). Let me explain each with practical examples.

## POSIX Asynchronous I/O

The POSIX AIO interface provides a standardized way to perform asynchronous file operations across Unix-like systems.

### aio_read - Asynchronous Read

The `aio_read()` function initiates an asynchronous read operation. Instead of blocking until data is available, it returns immediately and the read happens in the background.

```c
#include <aio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main() {
    int fd;
    struct aiocb cb;
    char buffer[1024];
    
    // Open file for reading
    fd = open("example.txt", O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    
    // Initialize aiocb structure
    memset(&cb, 0, sizeof(struct aiocb));
    cb.aio_fildes = fd;           // File descriptor
    cb.aio_buf = buffer;          // Buffer to read into
    cb.aio_nbytes = sizeof(buffer); // Number of bytes to read
    cb.aio_offset = 0;            // Offset in file
    
    // Initiate asynchronous read
    if (aio_read(&cb) == -1) {
        perror("aio_read");
        close(fd);
        return 1;
    }
    
    printf("Read operation initiated, doing other work...\n");
    
    // Simulate doing other work while I/O happens
    for (int i = 0; i < 3; i++) {
        printf("Working... %d\n", i);
        sleep(1);
    }
    
    // Wait for the operation to complete
    while (aio_error(&cb) == EINPROGRESS) {
        printf("Still waiting for I/O...\n");
        usleep(100000);
    }
    
    // Check result
    ssize_t ret = aio_return(&cb);
    if (ret > 0) {
        printf("Read %zd bytes: %.*s\n", ret, (int)ret, buffer);
    } else {
        printf("Read failed\n");
    }
    
    close(fd);
    return 0;
}
```

### aio_write - Asynchronous Write

Similarly, `aio_write()` performs non-blocking write operations.

```c
#include <aio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

int main() {
    int fd;
    struct aiocb cb;
    const char *data = "Hello, Asynchronous World!\n";
    
    fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    
    memset(&cb, 0, sizeof(struct aiocb));
    cb.aio_fildes = fd;
    cb.aio_buf = (void *)data;
    cb.aio_nbytes = strlen(data);
    cb.aio_offset = 0;
    
    if (aio_write(&cb) == -1) {
        perror("aio_write");
        close(fd);
        return 1;
    }
    
    printf("Write initiated, continuing with other tasks...\n");
    
    // Wait for completion
    while (aio_error(&cb) == EINPROGRESS);
    
    if (aio_return(&cb) > 0) {
        printf("Write completed successfully\n");
    }
    
    close(fd);
    return 0;
}
```

### aio_suspend - Waiting for Multiple Operations

`aio_suspend()` allows you to wait for one or more asynchronous operations to complete, similar to `select()` or `poll()` for regular I/O.

```c
#include <aio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main() {
    struct aiocb cb1, cb2, cb3;
    struct aiocb *cblist[3];
    char buf1[512], buf2[512], buf3[512];
    int fd1, fd2, fd3;
    
    // Open multiple files
    fd1 = open("file1.txt", O_RDONLY);
    fd2 = open("file2.txt", O_RDONLY);
    fd3 = open("file3.txt", O_RDONLY);
    
    // Initialize multiple read operations
    memset(&cb1, 0, sizeof(struct aiocb));
    cb1.aio_fildes = fd1;
    cb1.aio_buf = buf1;
    cb1.aio_nbytes = sizeof(buf1);
    
    memset(&cb2, 0, sizeof(struct aiocb));
    cb2.aio_fildes = fd2;
    cb2.aio_buf = buf2;
    cb2.aio_nbytes = sizeof(buf2);
    
    memset(&cb3, 0, sizeof(struct aiocb));
    cb3.aio_fildes = fd3;
    cb3.aio_buf = buf3;
    cb3.aio_nbytes = sizeof(buf3);
    
    // Start all reads
    aio_read(&cb1);
    aio_read(&cb2);
    aio_read(&cb3);
    
    // Prepare list for aio_suspend
    cblist[0] = &cb1;
    cblist[1] = &cb2;
    cblist[2] = &cb3;
    
    // Wait for at least one operation to complete
    struct timespec timeout = {.tv_sec = 5, .tv_nsec = 0};
    
    printf("Waiting for at least one I/O to complete...\n");
    if (aio_suspend((const struct aiocb **)cblist, 3, &timeout) == 0) {
        // Check which operations completed
        if (aio_error(&cb1) != EINPROGRESS) {
            printf("File 1 read completed: %zd bytes\n", aio_return(&cb1));
        }
        if (aio_error(&cb2) != EINPROGRESS) {
            printf("File 2 read completed: %zd bytes\n", aio_return(&cb2));
        }
        if (aio_error(&cb3) != EINPROGRESS) {
            printf("File 3 read completed: %zd bytes\n", aio_return(&cb3));
        }
    } else {
        printf("Timeout or error occurred\n");
    }
    
    close(fd1);
    close(fd2);
    close(fd3);
    return 0;
}
```

## Linux Native AIO (libaio)

The Linux-specific AIO interface provides better performance for certain workloads, particularly with direct I/O and databases. It's more efficient than POSIX AIO but less portable.

### io_submit - Submit I/O Requests

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <libaio.h>

#define QUEUE_DEPTH 4

int main() {
    io_context_t ctx = 0;
    struct iocb cb;
    struct iocb *cbs[1];
    char buffer[4096] __attribute__((aligned(512)));
    int fd;
    
    // Initialize AIO context
    if (io_setup(QUEUE_DEPTH, &ctx) < 0) {
        perror("io_setup");
        return 1;
    }
    
    // Open file with O_DIRECT for true async I/O
    fd = open("data.bin", O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open");
        io_destroy(ctx);
        return 1;
    }
    
    // Prepare I/O control block
    memset(&cb, 0, sizeof(cb));
    cb.aio_fildes = fd;
    cb.aio_lio_opcode = IOCB_CMD_PREAD;  // Read operation
    cb.aio_buf = (uint64_t)buffer;
    cb.aio_offset = 0;
    cb.aio_nbytes = 4096;
    
    cbs[0] = &cb;
    
    // Submit the I/O request
    int ret = io_submit(ctx, 1, cbs);
    if (ret != 1) {
        perror("io_submit");
        close(fd);
        io_destroy(ctx);
        return 1;
    }
    
    printf("I/O request submitted, request can do other work now\n");
    
    // Process results (see io_getevents below)
    struct io_event events[1];
    ret = io_getevents(ctx, 1, 1, events, NULL);
    
    if (ret == 1) {
        printf("Read completed: %ld bytes\n", events[0].res);
    }
    
    close(fd);
    io_destroy(ctx);
    return 0;
}
```

### io_getevents - Retrieve Completed Operations

This function waits for and retrieves completion events from submitted I/O operations.

```c
#include <stdio.h>
#include <stdlib.h>
#include <libaio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_DEPTH 8
#define NUM_REQUESTS 5

int main() {
    io_context_t ctx = 0;
    struct iocb *cbs[NUM_REQUESTS];
    struct io_event events[NUM_REQUESTS];
    char *buffers[NUM_REQUESTS];
    int fd;
    
    // Setup AIO context
    io_setup(QUEUE_DEPTH, &ctx);
    
    fd = open("large_file.dat", O_RDONLY | O_DIRECT);
    
    // Prepare multiple I/O requests
    for (int i = 0; i < NUM_REQUESTS; i++) {
        // Allocate aligned buffers for O_DIRECT
        posix_memalign((void **)&buffers[i], 512, 4096);
        
        cbs[i] = malloc(sizeof(struct iocb));
        memset(cbs[i], 0, sizeof(struct iocb));
        
        cbs[i]->aio_fildes = fd;
        cbs[i]->aio_lio_opcode = IOCB_CMD_PREAD;
        cbs[i]->aio_buf = (uint64_t)buffers[i];
        cbs[i]->aio_offset = i * 4096;  // Read different chunks
        cbs[i]->aio_nbytes = 4096;
        cbs[i]->aio_data = i;  // User data to identify request
    }
    
    // Submit all requests at once
    int submitted = io_submit(ctx, NUM_REQUESTS, cbs);
    printf("Submitted %d I/O requests\n", submitted);
    
    // Retrieve completed events
    int completed = 0;
    while (completed < submitted) {
        struct timespec timeout = {.tv_sec = 1, .tv_nsec = 0};
        
        // Get up to NUM_REQUESTS events, wait for at least 1
        int n = io_getevents(ctx, 1, NUM_REQUESTS, events, &timeout);
        
        if (n > 0) {
            printf("Got %d completion events\n", n);
            
            for (int i = 0; i < n; i++) {
                struct io_event *ev = &events[i];
                int req_id = ev->data;  // Our user data
                ssize_t result = ev->res;  // Number of bytes or error
                
                if (result > 0) {
                    printf("Request %d completed: read %ld bytes\n", 
                           req_id, result);
                } else {
                    printf("Request %d failed with error %ld\n", 
                           req_id, result);
                }
            }
            completed += n;
        }
    }
    
    // Cleanup
    for (int i = 0; i < NUM_REQUESTS; i++) {
        free(buffers[i]);
        free(cbs[i]);
    }
    close(fd);
    io_destroy(ctx);
    
    return 0;
}
```

## Key Differences Between POSIX AIO and Linux Native AIO

**POSIX AIO:**
- Portable across Unix-like systems
- May use thread pools internally (not true async on all systems)
- Works with buffered I/O
- Simpler API but potentially less efficient

**Linux Native AIO:**
- Linux-specific, not portable
- True asynchronous I/O at kernel level
- Best performance with O_DIRECT (unbuffered I/O)
- More complex but more efficient for high-performance applications
- Used by databases like PostgreSQL and MySQL

## Compilation

To compile POSIX AIO examples:
```bash
gcc example.c -o example -lrt
```

To compile Linux native AIO examples:
```bash
gcc example.c -o example -laio
```

Asynchronous I/O is particularly valuable for servers, databases, and applications that need to handle many concurrent I/O operations without dedicating a thread to each one. It allows the CPU to remain productive while waiting for disk or network operations to complete.