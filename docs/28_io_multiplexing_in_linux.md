# I/O Multiplexing in Linux

I/O multiplexing is a crucial technique that allows a single process to monitor multiple file descriptors (sockets, pipes, files, etc.) simultaneously, waiting for any of them to become ready for I/O operations. This is essential for building high-performance network servers and applications that handle concurrent connections efficiently.

## The Problem I/O Multiplexing Solves

Imagine you're building a chat server that needs to handle 10,000 simultaneous client connections. Without I/O multiplexing, you'd need either:
- **10,000 threads** (one per connection) - expensive in terms of memory and context switching
- **Blocking I/O with one thread** - you'd be stuck waiting on one client while others are ready

I/O multiplexing lets one thread monitor all 10,000 connections and respond immediately when any client sends data.

## epoll: The Modern Linux Standard

**epoll** is Linux's scalable I/O event notification mechanism, designed to handle tens of thousands of file descriptors efficiently. It has O(1) performance for most operations, unlike the older select/poll which are O(n).

### How epoll Works

epoll uses three main system calls:

1. **epoll_create/epoll_create1**: Creates an epoll instance
2. **epoll_ctl**: Adds, modifies, or removes file descriptors from monitoring
3. **epoll_wait**: Waits for events on monitored file descriptors

### Practical epoll Example: Simple Echo Server

```c
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_EVENTS 64
#define PORT 8080

// Make socket non-blocking
int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd, epoll_fd, nfds;
    struct epoll_event ev, events[MAX_EVENTS];
    struct sockaddr_in addr;
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(1);
    }
    
    // Allow address reuse
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    // Bind to port
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }
    
    // Listen for connections
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        exit(1);
    }
    
    make_socket_non_blocking(listen_fd);
    
    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    // Add listening socket to epoll
    ev.events = EPOLLIN; // Monitor for read events
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl: listen_fd");
        exit(1);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Main event loop
    while (1) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(1);
        }
        
        // Process all ready file descriptors
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == listen_fd) {
                // New connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
                
                if (conn_fd == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept");
                    }
                    continue;
                }
                
                make_socket_non_blocking(conn_fd);
                
                // Add new connection to epoll
                ev.events = EPOLLIN | EPOLLET; // Edge-triggered mode
                ev.data.fd = conn_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                    perror("epoll_ctl: conn_fd");
                    close(conn_fd);
                    continue;
                }
                
                printf("New connection: fd=%d\n", conn_fd);
            } else {
                // Data available on existing connection
                char buf[512];
                ssize_t count = read(events[i].data.fd, buf, sizeof(buf));
                
                if (count <= 0) {
                    if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        // Connection closed or error
                        printf("Closing connection: fd=%d\n", events[i].data.fd);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                        close(events[i].data.fd);
                    }
                } else {
                    // Echo data back
                    write(events[i].data.fd, buf, count);
                }
            }
        }
    }
    
    close(listen_fd);
    close(epoll_fd);
    return 0;
}
```

### epoll Triggering Modes

epoll supports two triggering modes:

**Level-Triggered (default)**: epoll_wait returns whenever data is available. If you don't read all data, the next epoll_wait will immediately return again.

**Edge-Triggered (EPOLLET)**: epoll_wait returns only when state changes (new data arrives). You must read all available data in one go. More efficient but requires careful programming.

```c
// Edge-triggered example: must read all data
ev.events = EPOLLIN | EPOLLET;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);

// In event loop, drain the socket completely
while (1) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // All data read
            break;
        }
        // Handle error
        break;
    }
    process_data(buf, n);
}
```

## pselect: select with Better Signal Handling

**pselect** is an improvement over the traditional `select()` system call. It provides nanosecond-precision timeouts and atomically handles signal masking.

### Why pselect?

The classic problem with `select()` is the race condition between checking signals and waiting for I/O. pselect solves this by atomically unmasking signals only during the wait.

### pselect Example: Handling Signals Safely

```c
#include <sys/select.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

volatile sig_atomic_t got_signal = 0;

void signal_handler(int sig) {
    got_signal = 1;
}

int main() {
    int fd = STDIN_FILENO;
    fd_set readfds;
    struct timespec timeout;
    sigset_t sigmask, orig_sigmask;
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    
    // Block SIGINT during normal execution
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigprocmask(SIG_BLOCK, &sigmask, &orig_sigmask);
    
    // Empty mask for pselect (will receive SIGINT during wait)
    sigemptyset(&sigmask);
    
    while (!got_signal) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        // Wait up to 2.5 seconds
        timeout.tv_sec = 2;
        timeout.tv_nsec = 500000000; // 500 milliseconds
        
        // pselect atomically unblocks signals during wait
        int ret = pselect(fd + 1, &readfds, NULL, NULL, &timeout, &sigmask);
        
        if (ret == -1) {
            if (errno == EINTR) {
                printf("Interrupted by signal\n");
                continue;
            }
            perror("pselect");
            break;
        } else if (ret == 0) {
            printf("Timeout\n");
        } else {
            if (FD_ISSET(fd, &readfds)) {
                char buf[256];
                ssize_t n = read(fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = '\0';
                    printf("Read: %s", buf);
                }
            }
        }
    }
    
    printf("Exiting due to signal\n");
    return 0;
}
```

### Key Differences: select vs pselect

```c
// select: microsecond precision
struct timeval tv = {.tv_sec = 1, .tv_usec = 500000};
select(maxfd + 1, &readfds, NULL, NULL, &tv);

// pselect: nanosecond precision + signal masking
struct timespec ts = {.tv_sec = 1, .tv_nsec = 500000000};
pselect(maxfd + 1, &readfds, NULL, NULL, &ts, &sigmask);
```

## ppoll: poll with Signal Masking

**ppoll** is to `poll()` what pselect is to `select()`. It adds signal masking capabilities to the poll interface.

### ppoll Example: Multi-client Server with Signal Handling

```c
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define MAX_CLIENTS 100
#define PORT 8081

volatile sig_atomic_t shutdown_requested = 0;

void sigint_handler(int sig) {
    shutdown_requested = 1;
}

int main() {
    struct pollfd fds[MAX_CLIENTS + 1];
    int listen_fd, nfds = 1;
    struct sockaddr_in addr;
    sigset_t sigmask, orig_sigmask;
    struct timespec timeout;
    
    // Setup signal handling
    signal(SIGINT, sigint_handler);
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigprocmask(SIG_BLOCK, &sigmask, &orig_sigmask);
    sigemptyset(&sigmask); // Empty for ppoll
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 10);
    
    // Initialize poll array
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    printf("Server listening on port %d (Ctrl+C to stop)\n", PORT);
    
    while (!shutdown_requested) {
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;
        
        // ppoll with signal mask
        int ret = ppoll(fds, nfds, &timeout, &sigmask);
        
        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("ppoll");
            break;
        }
        
        if (ret == 0) continue; // Timeout
        
        // Check for new connections
        if (fds[0].revents & POLLIN) {
            int conn_fd = accept(listen_fd, NULL, NULL);
            if (conn_fd != -1 && nfds < MAX_CLIENTS + 1) {
                fds[nfds].fd = conn_fd;
                fds[nfds].events = POLLIN;
                nfds++;
                printf("New connection (total: %d)\n", nfds - 1);
            }
        }
        
        // Check existing connections
        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                char buf[256];
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                if (n <= 0) {
                    close(fds[i].fd);
                    fds[i] = fds[nfds - 1];
                    nfds--;
                    i--;
                    printf("Connection closed (total: %d)\n", nfds - 1);
                } else {
                    write(fds[i].fd, buf, n);
                }
            }
        }
    }
    
    printf("\nShutting down gracefully...\n");
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    return 0;
}
```

## io_uring: The Future of Linux I/O

**io_uring** is a revolutionary asynchronous I/O interface introduced in Linux 5.1. It provides true asynchronous I/O with minimal context switching and exceptional performance.

### How io_uring Works

io_uring uses two ring buffers shared between kernel and userspace:
- **Submission Queue (SQ)**: Application submits I/O requests
- **Completion Queue (CQ)**: Kernel posts completion events

This design minimizes system calls and enables batching operations.

### io_uring Example: Asynchronous File Reading

```c
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_DEPTH 32
#define BLOCK_SIZE 4096

struct io_data {
    int file_fd;
    off_t offset;
    size_t size;
    struct iovec iov;
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    
    struct io_uring ring;
    int ret;
    
    // Initialize io_uring with queue depth of 32
    ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
        return 1;
    }
    
    // Open file
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        io_uring_queue_exit(&ring);
        return 1;
    }
    
    // Get file size
    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    printf("Reading file: %s (%ld bytes)\n", argv[1], file_size);
    
    // Submit multiple read requests
    int blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    struct io_data *data_array = calloc(blocks, sizeof(struct io_data));
    
    for (int i = 0; i < blocks; i++) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            fprintf(stderr, "Could not get SQE\n");
            break;
        }
        
        // Prepare buffer
        data_array[i].file_fd = fd;
        data_array[i].offset = i * BLOCK_SIZE;
        data_array[i].size = BLOCK_SIZE;
        data_array[i].iov.iov_base = malloc(BLOCK_SIZE);
        data_array[i].iov.iov_len = BLOCK_SIZE;
        
        // Setup read operation
        io_uring_prep_readv(sqe, fd, &data_array[i].iov, 1, data_array[i].offset);
        io_uring_sqe_set_data(sqe, &data_array[i]);
    }
    
    // Submit all requests at once
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
        goto cleanup;
    }
    
    printf("Submitted %d read requests\n", ret);
    
    // Wait for completions
    size_t total_read = 0;
    for (int i = 0; i < blocks; i++) {
        struct io_uring_cqe *cqe;
        
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            break;
        }
        
        struct io_data *data = (struct io_data *)io_uring_cqe_get_data(cqe);
        
        if (cqe->res < 0) {
            fprintf(stderr, "Read failed: %s\n", strerror(-cqe->res));
        } else {
            total_read += cqe->res;
            // Process data here (e.g., write to stdout, checksum, etc.)
            // write(STDOUT_FILENO, data->iov.iov_base, cqe->res);
        }
        
        io_uring_cqe_seen(&ring, cqe);
        free(data->iov.iov_base);
    }
    
    printf("Total bytes read: %zu\n", total_read);
    
cleanup:
    free(data_array);
    close(fd);
    io_uring_queue_exit(&ring);
    return 0;
}
```

### io_uring Network Server Example

```c
#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_DEPTH 256
#define BUF_SIZE 4096
#define PORT 8082

enum {
    EVENT_TYPE_ACCEPT,
    EVENT_TYPE_READ,
    EVENT_TYPE_WRITE,
};

struct conn_info {
    int fd;
    int type;
    char buf[BUF_SIZE];
    size_t len;
};

void add_accept_request(struct io_uring *ring, int listen_fd, 
                       struct sockaddr_in *client_addr, socklen_t *client_len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    struct conn_info *info = malloc(sizeof(struct conn_info));
    
    info->fd = listen_fd;
    info->type = EVENT_TYPE_ACCEPT;
    
    io_uring_prep_accept(sqe, listen_fd, (struct sockaddr *)client_addr, 
                         client_len, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_read_request(struct io_uring *ring, int client_fd) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    struct conn_info *info = malloc(sizeof(struct conn_info));
    
    info->fd = client_fd;
    info->type = EVENT_TYPE_READ;
    
    io_uring_prep_recv(sqe, client_fd, info->buf, BUF_SIZE, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_write_request(struct io_uring *ring, int client_fd, char *buf, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    struct conn_info *info = malloc(sizeof(struct conn_info));
    
    info->fd = client_fd;
    info->type = EVENT_TYPE_WRITE;
    memcpy(info->buf, buf, len);
    info->len = len;
    
    io_uring_prep_send(sqe, client_fd, info->buf, len, 0);
    io_uring_sqe_set_data(sqe, info);
}

int main() {
    struct io_uring ring;
    struct sockaddr_in srv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int listen_fd;
    
    // Create io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }
    
    // Create and setup listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(PORT);
    
    bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    listen(listen_fd, SOMAXCONN);
    
    printf("Server listening on port %d using io_uring\n", PORT);
    
    // Add initial accept request
    add_accept_request(&ring, listen_fd, &client_addr, &client_len);
    
    // Event loop
    while (1) {
        io_uring_submit(&ring);
        
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            break;
        }
        
        struct conn_info *info = (struct conn_info *)io_uring_cqe_get_data(cqe);
        int result = cqe->res;
        
        if (info->type == EVENT_TYPE_ACCEPT) {
            if (result >= 0) {
                printf("Accepted connection: fd=%d\n", result);
                add_read_request(&ring, result);
                add_accept_request(&ring, listen_fd, &client_addr, &client_len);
            }
        } else if (info->type == EVENT_TYPE_READ) {
            if (result > 0) {
                // Echo back the data
                add_write_request(&ring, info->fd, info->buf, result);
            } else {
                printf("Connection closed: fd=%d\n", info->fd);
                close(info->fd);
            }
        } else if (info->type == EVENT_TYPE_WRITE) {
            if (result >= 0) {
                // Write completed, read more
                add_read_request(&ring, info->fd);
            } else {
                close(info->fd);
            }
        }
        
        io_uring_cqe_seen(&ring, cqe);
        free(info);
    }
    
    io_uring_queue_exit(&ring);
    close(listen_fd);
    return 0;
}
```

## Performance Comparison

For handling many concurrent connections, here's a rough performance comparison:

**select/poll**: O(n) complexity, limited to 1024 file descriptors (select), significant overhead with many connections

**pselect/ppoll**: Same complexity as select/poll but with better signal handling and timeout precision

**epoll**: O(1) for add/remove/modify operations, scales to hundreds of thousands of connections efficiently

**io_uring**: Lowest overhead, true async I/O, best performance especially for file I/O and high-throughput scenarios. Can handle millions of operations per second with minimal CPU usage.

## When to Use Each

**Use epoll when**: Building high-performance network servers, handling thousands of concurrent connections, need edge-triggered notifications

**Use pselect/ppoll when**: Need precise signal handling, working with relatively few file descriptors, portability across POSIX systems matters

**Use io_uring when**: Maximum performance is critical, doing heavy file I/O, need true asynchronous operations, working with modern Linux kernels (5.1+)

The trend in modern Linux systems is clearly toward io_uring for new high-performance applications, while epoll remains the solid choice for network servers that need broad compatibility.