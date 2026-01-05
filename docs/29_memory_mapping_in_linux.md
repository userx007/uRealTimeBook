# Memory Mapping in Linux

Memory mapping is a powerful mechanism in Linux that allows processes to map files or devices into their address space, enabling efficient file I/O and inter-process communication. Instead of using traditional read/write system calls, memory-mapped regions can be accessed directly like regular memory.

## Core Concepts

When you use `mmap()` to create a memory mapping, the kernel creates a virtual memory area (VMA) in your process's address space. This region can be backed by:
- A file on disk (file-backed mapping)
- Anonymous memory (not backed by any file)
- Shared memory objects

## Key System Calls

### munmap - Unmapping Memory

The `munmap()` system call removes a memory mapping, releasing the virtual address space back to the system.

**Signature:** `int munmap(void *addr, size_t length)`

**Example:**
```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("data.txt", O_RDONLY);
    size_t size = 4096;
    
    // Map file into memory
    void *mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    // Use the mapped memory
    printf("First bytes: %c%c%c\n", 
           ((char*)mapped)[0], 
           ((char*)mapped)[1], 
           ((char*)mapped)[2]);
    
    // Unmap when done
    if (munmap(mapped, size) == -1) {
        perror("munmap");
        return 1;
    }
    
    close(fd);
    return 0;
}
```

### mprotect - Changing Memory Protection

The `mprotect()` system call modifies access permissions on a memory region, useful for implementing security features or just-in-time compilation.

**Signature:** `int mprotect(void *addr, size_t len, int prot)`

Protection flags include `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, and `PROT_NONE`.

**Example:**
```c
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

int main() {
    size_t size = 4096;
    
    // Create writable anonymous mapping
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    // Write some data
    strcpy((char*)mem, "Hello, World!");
    printf("Data: %s\n", (char*)mem);
    
    // Make it read-only
    if (mprotect(mem, size, PROT_READ) == -1) {
        perror("mprotect");
        return 1;
    }
    
    // This would now cause a segmentation fault:
    // strcpy((char*)mem, "New data");
    
    munmap(mem, size);
    return 0;
}
```

**Practical use case:** JIT compilers allocate memory as writable to generate machine code, then use `mprotect()` to make it executable but not writable, preventing code injection attacks.

### msync - Synchronizing Memory with Files

When you have a file-backed mapping created with `MAP_SHARED`, changes might not be immediately written to disk. The `msync()` system call forces synchronization.

**Signature:** `int msync(void *addr, size_t length, int flags)`

Flags include `MS_SYNC` (synchronous), `MS_ASYNC` (asynchronous), and `MS_INVALIDATE`.

**Example:**
```c
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main() {
    int fd = open("output.txt", O_RDWR | O_CREAT, 0644);
    ftruncate(fd, 4096);
    
    // Map file with shared mapping
    void *mapped = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    
    // Modify the mapped region
    strcpy((char*)mapped, "Important data that must be saved");
    
    // Force write to disk
    if (msync(mapped, 4096, MS_SYNC) == -1) {
        perror("msync");
        return 1;
    }
    
    munmap(mapped, 4096);
    close(fd);
    return 0;
}
```

### mremap - Resizing Memory Mappings

The `mremap()` system call allows you to expand or shrink an existing mapping, which is more efficient than unmapping and remapping.

**Signature:** `void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ...)`

**Example:**
```c
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

int main() {
    size_t initial_size = 4096;
    size_t new_size = 8192;
    
    // Create initial mapping
    void *mem = mmap(NULL, initial_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    strcpy((char*)mem, "Initial data");
    
    // Expand the mapping
    void *new_mem = mremap(mem, initial_size, new_size, MREMAP_MAYMOVE);
    if (new_mem == MAP_FAILED) {
        perror("mremap");
        return 1;
    }
    
    printf("Old data still present: %s\n", (char*)new_mem);
    strcpy((char*)new_mem + 4096, "New space available!");
    
    munmap(new_mem, new_size);
    return 0;
}
```

### madvise - Memory Usage Hints

The `madvise()` system call provides hints to the kernel about how you plan to use a memory region, allowing for optimizations.

**Signature:** `int madvise(void *addr, size_t length, int advice)`

Common advice values include `MADV_SEQUENTIAL`, `MADV_RANDOM`, `MADV_WILLNEED`, `MADV_DONTNEED`.

**Example:**
```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    int fd = open("large_file.dat", O_RDONLY);
    size_t size = 1024 * 1024 * 100; // 100 MB
    
    void *mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    // Tell kernel we'll read sequentially
    madvise(mapped, size, MADV_SEQUENTIAL);
    
    // Process the file sequentially
    for (size_t i = 0; i < size; i += 4096) {
        // Access page
        volatile char x = ((char*)mapped)[i];
    }
    
    // Tell kernel we don't need this data anymore
    madvise(mapped, size, MADV_DONTNEED);
    
    munmap(mapped, size);
    close(fd);
    return 0;
}
```

**Use case:** `MADV_DONTNEED` is particularly useful for freeing memory in long-running processes without unmapping the region, as the kernel can reclaim physical pages while keeping the virtual mapping intact.

### mlock/munlock - Preventing Memory Swapping

These system calls lock pages in physical memory, preventing them from being swapped to disk. This is crucial for security-sensitive data like encryption keys.

**Signature:** `int mlock(const void *addr, size_t len)` and `int munlock(const void *addr, size_t len)`

**Example:**
```c
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

int main() {
    size_t size = 4096;
    
    // Allocate memory for sensitive data
    void *secure_mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    // Lock it in RAM to prevent swapping
    if (mlock(secure_mem, size) == -1) {
        perror("mlock");
        return 1;
    }
    
    // Store sensitive data (e.g., encryption key)
    strcpy((char*)secure_mem, "super_secret_key_12345");
    
    // Use the key...
    
    // Clear the memory before unlocking
    memset(secure_mem, 0, size);
    
    munlock(secure_mem, size);
    munmap(secure_mem, size);
    return 0;
}
```

**Note:** Locking memory requires appropriate privileges (CAP_IPC_LOCK capability) and is limited by RLIMIT_MEMLOCK resource limit.

### mincore - Checking Page Residency

The `mincore()` system call determines which pages of a mapping are currently resident in physical memory, useful for understanding and optimizing memory access patterns.

**Signature:** `int mincore(void *addr, size_t length, unsigned char *vec)`

**Example:**
```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    int fd = open("large_file.dat", O_RDONLY);
    size_t size = 1024 * 1024; // 1 MB
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t num_pages = (size + page_size - 1) / page_size;
    
    void *mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    // Allocate vector to store residency info
    unsigned char *vec = calloc(num_pages, 1);
    
    // Check which pages are in memory
    if (mincore(mapped, size, vec) == -1) {
        perror("mincore");
        return 1;
    }
    
    // Count resident pages
    int resident = 0;
    for (size_t i = 0; i < num_pages; i++) {
        if (vec[i] & 1) resident++;
    }
    
    printf("%d of %zu pages are resident in memory\n", resident, num_pages);
    
    free(vec);
    munmap(mapped, size);
    close(fd);
    return 0;
}
```

### memfd_create - Anonymous File Descriptors

The `memfd_create()` system call creates an anonymous file that exists only in RAM, providing a file descriptor that can be used with `mmap()` for shared memory between processes.

**Signature:** `int memfd_create(const char *name, unsigned int flags)`

**Example:**
```c
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main() {
    // Create anonymous file in memory
    int fd = syscall(SYS_memfd_create, "shared_buffer", 0);
    if (fd == -1) {
        perror("memfd_create");
        return 1;
    }
    
    // Set size
    size_t size = 4096;
    ftruncate(fd, size);
    
    // Map it
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    
    strcpy((char*)mem, "Data in anonymous memory file");
    
    // Fork and share with child process
    pid_t pid = fork();
    if (pid == 0) {
        // Child can access the same memory
        printf("Child reads: %s\n", (char*)mem);
        strcpy((char*)mem, "Modified by child");
        return 0;
    } else {
        wait(NULL);
        printf("Parent sees: %s\n", (char*)mem);
    }
    
    munmap(mem, size);
    close(fd);
    return 0;
}
```

**Advantages over traditional shared memory:** `memfd_create()` doesn't require a filesystem namespace, supports sealing (via F_ADD_SEALS) to make the memory immutable, and integrates cleanly with file descriptor passing mechanisms.

## Common Use Cases

**Database systems** use memory mapping for efficient file access, with `madvise()` for read-ahead optimization and `msync()` for durability guarantees.

**Multimedia applications** map video or audio files and use `mlock()` to prevent playback interruptions from swapping.

**Security applications** use `mlock()` for sensitive data and `mprotect()` to implement W^X (write XOR execute) policies.

**Inter-process communication** leverages `memfd_create()` or file-backed `MAP_SHARED` mappings for zero-copy data sharing.

These system calls provide fine-grained control over memory management, enabling applications to optimize performance, ensure security, and implement sophisticated memory access patterns.