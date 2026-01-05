# Linux special system calls 

**I/O Multiplexing:**
- epoll (epoll_create, epoll_ctl, epoll_wait) - more scalable alternative to poll/select for monitoring multiple file descriptors
- pselect - like select but with better signal handling and nanosecond timeout precision
- ppoll - like poll but with signal masking capabilities
- io_uring (io_uring_setup, io_uring_enter) - modern asynchronous I/O interface with high performance

**Memory Mapping:**
- munmap - unmaps memory previously mapped with mmap
- mprotect - changes protection on a region of memory
- msync - synchronizes a mapped region with the underlying file
- mremap - expands or shrinks an existing memory mapping
- madvise - gives advice about memory usage patterns
- mlock/munlock - locks/unlocks memory to prevent swapping
- mincore - determines whether pages are resident in memory
- memfd_create - creates anonymous file for memory mapping

**Direct I/O and Zero-Copy:**
- sendfile - efficiently copies data between file descriptors without user-space buffering
- splice - moves data between pipes and files without copying to user space
- tee - duplicates pipe content without consuming it
- vmsplice - maps user-space memory into a pipe

**Asynchronous I/O:**
- aio_read/aio_write - POSIX asynchronous I/O operations
- aio_suspend - waits for asynchronous I/O operations
- io_submit/io_getevents - Linux native AIO interface

**File Monitoring:**
- inotify (inotify_init, inotify_add_watch) - monitors filesystem events
- fanotify - file access notification for monitoring and permission decisions

These functions are commonly used for high-performance server applications, system programming, and scenarios requiring efficient resource management.