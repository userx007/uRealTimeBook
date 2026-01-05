# Direct I/O and Zero-Copy in Linux

## Overview

Direct I/O and zero-copy mechanisms are performance optimization techniques that minimize or eliminate unnecessary data copying between kernel space and user space. Traditional I/O operations involve multiple copy operations that consume CPU cycles and memory bandwidth. These mechanisms address this inefficiency by allowing data to move directly between file descriptors, sockets, or memory regions.

## The Problem with Traditional I/O

In traditional I/O operations, data typically flows through multiple buffers:

```
Disk → Kernel Buffer → User Space Buffer → Kernel Buffer → Network Socket
```

This involves:
- Reading from disk into kernel buffer (DMA copy)
- Copying from kernel buffer to user space (CPU copy)
- Copying from user space back to kernel buffer (CPU copy)
- Sending from kernel buffer to network (DMA copy)

This results in **4 context switches** and **2 unnecessary CPU copies**.

## Zero-Copy System Calls

### 1. sendfile()

`sendfile()` transfers data between two file descriptors entirely within kernel space, bypassing user space completely.

**Signature:**
```c
#include <sys/sendfile.h>

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
```

**Parameters:**
- `out_fd`: destination file descriptor (typically a socket)
- `in_fd`: source file descriptor (must support mmap, typically a regular file)
- `offset`: starting position in source file (NULL for current position)
- `count`: number of bytes to transfer

**Example: Simple HTTP File Server**

```c
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int serve_file(int client_socket, const char *filepath) {
    int file_fd;
    struct stat file_stat;
    off_t offset = 0;
    ssize_t sent_bytes;
    
    // Open the file
    file_fd = open(filepath, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }
    
    // Get file size
    if (fstat(file_fd, &file_stat) == -1) {
        perror("fstat");
        close(file_fd);
        return -1;
    }
    
    // Send HTTP headers
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: %ld\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             file_stat.st_size);
    write(client_socket, header, strlen(header));
    
    // Send file content using sendfile (zero-copy!)
    sent_bytes = sendfile(client_socket, file_fd, &offset, file_stat.st_size);
    
    if (sent_bytes == -1) {
        perror("sendfile");
        close(file_fd);
        return -1;
    }
    
    printf("Sent %ld bytes using sendfile\n", sent_bytes);
    close(file_fd);
    return 0;
}
```

**Benefits:**
- No data copying to user space
- Reduced CPU usage
- Lower memory footprint
- Fewer context switches

**Limitations:**
- Source must be a file that supports mmap
- Destination is typically a socket
- Cannot modify data in transit

### 2. splice()

`splice()` moves data between two file descriptors through a pipe buffer, allowing more flexibility than sendfile.

**Signature:**
```c
#include <fcntl.h>

ssize_t splice(int fd_in, off_t *off_in,
               int fd_out, off_t *off_out,
               size_t len, unsigned int flags);
```

**Flags:**
- `SPLICE_F_MOVE`: Attempt to move pages instead of copying
- `SPLICE_F_NONBLOCK`: Non-blocking operation
- `SPLICE_F_MORE`: More data will follow (hint for TCP)

**Example: Efficient File Copying**

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define PIPE_SIZE 65536

int copy_file_splice(const char *source, const char *dest) {
    int src_fd, dest_fd;
    int pipefd[2];
    ssize_t bytes_read, bytes_written;
    
    // Open source and destination
    src_fd = open(source, O_RDONLY);
    if (src_fd == -1) {
        perror("open source");
        return -1;
    }
    
    dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd == -1) {
        perror("open dest");
        close(src_fd);
        return -1;
    }
    
    // Create pipe
    if (pipe(pipefd) == -1) {
        perror("pipe");
        close(src_fd);
        close(dest_fd);
        return -1;
    }
    
    // Splice data: file → pipe → file
    while (1) {
        // Splice from file to pipe
        bytes_read = splice(src_fd, NULL, pipefd[1], NULL,
                           PIPE_SIZE, SPLICE_F_MOVE);
        
        if (bytes_read == -1) {
            perror("splice read");
            break;
        }
        
        if (bytes_read == 0)
            break;  // EOF
        
        // Splice from pipe to destination file
        bytes_written = splice(pipefd[0], NULL, dest_fd, NULL,
                              bytes_read, SPLICE_F_MOVE);
        
        if (bytes_written == -1) {
            perror("splice write");
            break;
        }
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    close(src_fd);
    close(dest_fd);
    
    return 0;
}
```

**Example: Network Proxy**

```c
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>

#define SPLICE_SIZE 16384

int proxy_splice(int client_fd, int server_fd) {
    int pipefd[2];
    ssize_t bytes;
    
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    // Forward: client → pipe → server
    while ((bytes = splice(client_fd, NULL, pipefd[1], NULL,
                          SPLICE_SIZE, SPLICE_F_MOVE | SPLICE_F_NONBLOCK)) > 0) {
        
        if (splice(pipefd[0], NULL, server_fd, NULL,
                  bytes, SPLICE_F_MOVE) == -1) {
            perror("splice to server");
            break;
        }
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}
```

### 3. tee()

`tee()` duplicates data from one pipe to another without consuming the original data, similar to the `tee` command.

**Signature:**
```c
#include <fcntl.h>

ssize_t tee(int fd_in, int fd_out, size_t len, unsigned int flags);
```

**Example: Logging Network Traffic**

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define BUFFER_SIZE 8192

int log_and_forward(int input_pipe[2], int output_pipe[2], int log_fd) {
    ssize_t bytes_teed, bytes_spliced;
    
    while (1) {
        // Duplicate data from input_pipe to output_pipe
        bytes_teed = tee(input_pipe[0], output_pipe[1], 
                        BUFFER_SIZE, SPLICE_F_NONBLOCK);
        
        if (bytes_teed <= 0)
            break;
        
        // Now splice the original data to log file
        bytes_spliced = splice(input_pipe[0], NULL, log_fd, NULL,
                              bytes_teed, SPLICE_F_MOVE);
        
        if (bytes_spliced == -1) {
            perror("splice to log");
            break;
        }
        
        printf("Logged %ld bytes\n", bytes_spliced);
    }
    
    return 0;
}
```

**Example: Broadcast to Multiple Outputs**

```c
#include <fcntl.h>
#include <stdio.h>

int broadcast_data(int source_pipe[2], int dest1_pipe[2], 
                   int dest2_pipe[2], size_t len) {
    ssize_t bytes;
    
    // Duplicate to first destination
    bytes = tee(source_pipe[0], dest1_pipe[1], len, 0);
    if (bytes == -1) {
        perror("tee to dest1");
        return -1;
    }
    
    // Duplicate to second destination (original data still in source_pipe)
    bytes = tee(source_pipe[0], dest2_pipe[1], len, 0);
    if (bytes == -1) {
        perror("tee to dest2");
        return -1;
    }
    
    // Consume original data if needed
    char discard[8192];
    read(source_pipe[0], discard, len);
    
    return bytes;
}
```

### 4. vmsplice()

`vmsplice()` maps user-space memory directly into a pipe without copying, enabling true zero-copy from user space to kernel space.

**Signature:**
```c
#include <fcntl.h>
#include <sys/uio.h>

ssize_t vmsplice(int fd, const struct iovec *iov,
                 unsigned long nr_segs, unsigned int flags);
```

**Flags:**
- `SPLICE_F_GIFT`: Pages are a "gift" to the kernel (user must not modify them)

**Example: Zero-Copy Network Transmission**

```c
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int send_data_zerocopy(int socket_fd, void *data, size_t len) {
    int pipefd[2];
    struct iovec iov;
    ssize_t bytes;
    
    // Create pipe
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    // Setup iovec for user data
    iov.iov_base = data;
    iov.iov_len = len;
    
    // Map user memory into pipe (zero-copy!)
    bytes = vmsplice(pipefd[1], &iov, 1, SPLICE_F_GIFT);
    if (bytes == -1) {
        perror("vmsplice");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    printf("Mapped %ld bytes into pipe\n", bytes);
    
    // Splice from pipe to socket
    bytes = splice(pipefd[0], NULL, socket_fd, NULL,
                  len, SPLICE_F_MOVE);
    
    if (bytes == -1) {
        perror("splice to socket");
    }
    
    close(pipefd[0]);
    close(pipefd[1]);
    
    // Important: Don't modify 'data' after SPLICE_F_GIFT!
    return bytes;
}
```

**Example: Efficient Message Queue**

```c
#include <fcntl.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int pipefd[2];
} zerocopy_queue_t;

zerocopy_queue_t* queue_create() {
    zerocopy_queue_t *queue = malloc(sizeof(zerocopy_queue_t));
    if (pipe(queue->pipefd) == -1) {
        perror("pipe");
        free(queue);
        return NULL;
    }
    return queue;
}

int queue_send(zerocopy_queue_t *queue, void *msg, size_t len) {
    struct iovec iov = {
        .iov_base = msg,
        .iov_len = len
    };
    
    ssize_t bytes = vmsplice(queue->pipefd[1], &iov, 1, 0);
    if (bytes == -1) {
        perror("vmsplice");
        return -1;
    }
    
    return bytes;
}

int queue_receive(zerocopy_queue_t *queue, void *buf, size_t len) {
    return read(queue->pipefd[0], buf, len);
}

void queue_destroy(zerocopy_queue_t *queue) {
    close(queue->pipefd[0]);
    close(queue->pipefd[1]);
    free(queue);
}
```

## Performance Comparison

**Traditional read/write approach:**
```c
char buffer[8192];
while ((n = read(src_fd, buffer, sizeof(buffer))) > 0) {
    write(dest_fd, buffer, n);
}
// Multiple copies: disk→kernel→user→kernel→disk
```

**Zero-copy approach:**
```c
sendfile(dest_fd, src_fd, NULL, file_size);
// Direct: disk→kernel→disk (no user-space copy)
```

**Performance gains:**
- 40-60% reduction in CPU usage for large file transfers
- 2x-3x throughput improvement for network operations
- Significant memory bandwidth savings

## Use Cases

**sendfile:**
- Web servers serving static files
- FTP servers
- Media streaming
- File downloads

**splice:**
- Network proxies
- Data pipelines
- Log forwarding
- Protocol translators

**tee:**
- Network packet capture with forwarding
- Logging systems
- Data replication
- Debugging tools

**vmsplice:**
- High-performance message passing
- Custom network protocols
- Memory-mapped I/O optimization
- Real-time data streaming

## Considerations

**Limitations:**
- Not all file descriptors support these operations
- May require large pipe buffers for optimal performance
- Error handling is more complex than traditional I/O
- Platform-specific behavior (primarily Linux)

**Best practices:**
- Check return values carefully
- Handle partial transfers in loops
- Use appropriate flags for your use case
- Consider fallback to traditional I/O
- Test with realistic workloads

These zero-copy mechanisms are powerful tools for building high-performance I/O-intensive applications, particularly in scenarios involving large data transfers, network services, and real-time processing.