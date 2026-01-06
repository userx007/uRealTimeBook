# Synchronizing between separate processes in Linux

## Semaphores

Semaphores are one of the most common synchronization primitives. Linux supports two types:

**System V semaphores** - These are older but still widely used. They support semaphore sets and complex operations. Processes access them through a key, and they persist until explicitly removed or the system reboots.

**POSIX semaphores** - These come in two flavors: named semaphores (accessed via filesystem paths) and unnamed semaphores (placed in shared memory). POSIX semaphores are generally simpler and more modern than System V.

## Shared Memory with Locks

Processes can use shared memory segments combined with synchronization primitives. For example, you might place a mutex or semaphore in shared memory that both processes can access. This requires careful setup since the synchronization primitive needs to be marked as process-shared (using `PTHREAD_PROCESS_SHARED` attribute for pthread mutexes).

## File Locks

**Advisory locks** via `flock()` or `fcntl()` allow processes to coordinate access to resources. These are particularly useful when synchronizing around file access. With `fcntl()`, you can lock specific byte ranges within files, enabling fine-grained control.

**Mandatory locks** are also possible but less commonly used due to portability concerns.

## Message Queues

Both System V and POSIX message queues can serve as synchronization mechanisms. A process can block waiting for a message, effectively synchronizing with the sender.

## Pipes and FIFOs

Named pipes (FIFOs) allow processes to synchronize by blocking on reads when no data is available. This is simpler than semaphores for basic producer-consumer patterns.

## Futexes

Futexes (fast userspace mutexes) are Linux-specific low-level primitives that combine userspace atomic operations with kernel-level blocking. They're what pthread mutexes and other higher-level primitives are built on, and can be used in shared memory for efficient process synchronization.

## Lock Files

A simple traditional approach where processes create and check for the existence of specific files to coordinate access to shared resources.

The choice depends on your needs: semaphores for counting resources, mutexes for mutual exclusion, message queues for data passing with synchronization, or file locks when coordinating around file access. POSIX primitives are generally preferred over System V for new code due to simpler APIs and better portability.


# Comprehensive examples for all major synchronization mechanisms

**1. POSIX Named Semaphores** - Two processes take turns entering a critical section using a semaphore. Clean, portable API.

**2. System V Semaphores** - Similar mutex behavior but using the older System V API. More complex but supports semaphore sets.

**3. Shared Memory with Pthread Mutex** - Processes share memory and use a process-shared mutex to increment a counter safely. Great for sharing complex data structures.

**4. File Locks (flock)** - Processes coordinate using file locks. Simple and effective when working with files.

**5. POSIX Message Queues** - Processes exchange messages back and forth. Combines synchronization with data passing.

**6. Named Pipes (FIFOs)** - Producer-consumer pattern where one process writes data and the other reads. Blocking reads provide synchronization.

**7. Futexes** - Low-level synchronization using Linux futexes. Fast but complex - usually you'd use higher-level primitives built on these.

**8. Lock Files** - Simple file existence check for mutual exclusion. Easy to implement but has limitations (no priority, potential stale locks).

## Key Differences:

- **Semaphores/Mutexes**: Best for protecting critical sections
- **Message Queues/FIFOs**: Good when you need to pass data AND synchronize
- **Shared Memory + Mutex**: Best performance for complex shared data
- **File Locks**: Natural choice for file-based coordination
- **Lock Files**: Simplest but least robust

```c
/*
 * Linux Inter-Process Synchronization Examples
 * Compile: gcc -o program program.c -lpthread -lrt
 */

// ============================================================================
// 1. POSIX NAMED SEMAPHORES
// ============================================================================

/* Process 1: posix_sem_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main() {
    sem_t *sem = sem_open("/my_semaphore", O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(1);
    }

    for (int i = 0; i < 5; i++) {
        sem_wait(sem);  // Lock
        printf("Process 1: Critical section iteration %d\n", i);
        sleep(1);
        sem_post(sem);  // Unlock
        sleep(1);
    }

    sem_close(sem);
    sem_unlink("/my_semaphore");
    return 0;
}

/* Process 2: posix_sem_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    sleep(1);  // Let process 1 create the semaphore
    sem_t *sem = sem_open("/my_semaphore", 0);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(1);
    }

    for (int i = 0; i < 5; i++) {
        sem_wait(sem);
        printf("Process 2: Critical section iteration %d\n", i);
        sleep(1);
        sem_post(sem);
        sleep(1);
    }

    sem_close(sem);
    return 0;
}


// ============================================================================
// 2. SYSTEM V SEMAPHORES
// ============================================================================

/* Process 1: sysv_sem_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void sem_lock(int semid) {
    struct sembuf op = {0, -1, 0};
    semop(semid, &op, 1);
}

void sem_unlock(int semid) {
    struct sembuf op = {0, 1, 0};
    semop(semid, &op, 1);
}

int main() {
    key_t key = ftok("/tmp", 'S');
    int semid = semget(key, 1, IPC_CREAT | 0666);
    
    union semun arg;
    arg.val = 1;
    semctl(semid, 0, SETVAL, arg);

    for (int i = 0; i < 5; i++) {
        sem_lock(semid);
        printf("Process 1 (SysV): Critical section %d\n", i);
        sleep(1);
        sem_unlock(semid);
        sleep(1);
    }

    semctl(semid, 0, IPC_RMID);
    return 0;
}

/* Process 2: sysv_sem_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>

void sem_lock(int semid) {
    struct sembuf op = {0, -1, 0};
    semop(semid, &op, 1);
}

void sem_unlock(int semid) {
    struct sembuf op = {0, 1, 0};
    semop(semid, &op, 1);
}

int main() {
    sleep(1);
    key_t key = ftok("/tmp", 'S');
    int semid = semget(key, 1, 0666);

    for (int i = 0; i < 5; i++) {
        sem_lock(semid);
        printf("Process 2 (SysV): Critical section %d\n", i);
        sleep(1);
        sem_unlock(semid);
        sleep(1);
    }

    return 0;
}


// ============================================================================
// 3. SHARED MEMORY WITH PTHREAD MUTEX
// ============================================================================

/* Process 1: shm_mutex_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    pthread_mutex_t mutex;
    int counter;
} shared_data;

int main() {
    int shm_fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(shared_data));
    
    shared_data *data = mmap(NULL, sizeof(shared_data), 
                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&data->mutex, &attr);
    data->counter = 0;

    for (int i = 0; i < 5; i++) {
        pthread_mutex_lock(&data->mutex);
        data->counter++;
        printf("Process 1: Counter = %d\n", data->counter);
        pthread_mutex_unlock(&data->mutex);
        sleep(1);
    }

    munmap(data, sizeof(shared_data));
    close(shm_fd);
    return 0;
}

/* Process 2: shm_mutex_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    int counter;
} shared_data;

int main() {
    sleep(1);
    int shm_fd = shm_open("/my_shm", O_RDWR, 0666);
    shared_data *data = mmap(NULL, sizeof(shared_data),
                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    for (int i = 0; i < 5; i++) {
        pthread_mutex_lock(&data->mutex);
        data->counter++;
        printf("Process 2: Counter = %d\n", data->counter);
        pthread_mutex_unlock(&data->mutex);
        sleep(1);
    }

    munmap(data, sizeof(shared_data));
    close(shm_fd);
    shm_unlink("/my_shm");
    return 0;
}


// ============================================================================
// 4. FILE LOCKS (flock)
// ============================================================================

/* Process 1: flock_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    int fd = open("/tmp/lockfile", O_CREAT | O_RDWR, 0666);

    for (int i = 0; i < 5; i++) {
        flock(fd, LOCK_EX);  // Exclusive lock
        printf("Process 1: Locked, iteration %d\n", i);
        sleep(2);
        flock(fd, LOCK_UN);  // Unlock
        printf("Process 1: Unlocked\n");
        sleep(1);
    }

    close(fd);
    return 0;
}

/* Process 2: flock_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    sleep(1);
    int fd = open("/tmp/lockfile", O_RDWR);

    for (int i = 0; i < 5; i++) {
        printf("Process 2: Waiting for lock...\n");
        flock(fd, LOCK_EX);
        printf("Process 2: Locked, iteration %d\n", i);
        sleep(2);
        flock(fd, LOCK_UN);
        printf("Process 2: Unlocked\n");
        sleep(1);
    }

    close(fd);
    return 0;
}


// ============================================================================
// 5. POSIX MESSAGE QUEUES
// ============================================================================

/* Process 1: mq_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>

int main() {
    struct mq_attr attr = {0, 10, 256, 0};
    mqd_t mq = mq_open("/my_queue", O_CREAT | O_RDWR, 0644, &attr);

    char buffer[256];
    for (int i = 0; i < 5; i++) {
        snprintf(buffer, sizeof(buffer), "Message %d from Process 1", i);
        mq_send(mq, buffer, strlen(buffer) + 1, 0);
        printf("Process 1: Sent '%s'\n", buffer);
        
        mq_receive(mq, buffer, sizeof(buffer), NULL);
        printf("Process 1: Received '%s'\n", buffer);
        sleep(1);
    }

    mq_close(mq);
    mq_unlink("/my_queue");
    return 0;
}

/* Process 2: mq_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>

int main() {
    sleep(1);
    mqd_t mq = mq_open("/my_queue", O_RDWR);

    char buffer[256];
    for (int i = 0; i < 5; i++) {
        mq_receive(mq, buffer, sizeof(buffer), NULL);
        printf("Process 2: Received '%s'\n", buffer);
        
        snprintf(buffer, sizeof(buffer), "Reply %d from Process 2", i);
        mq_send(mq, buffer, strlen(buffer) + 1, 0);
        printf("Process 2: Sent '%s'\n", buffer);
    }

    mq_close(mq);
    return 0;
}


// ============================================================================
// 6. NAMED PIPES (FIFOs)
// ============================================================================

/* Process 1: fifo_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int main() {
    mkfifo("/tmp/myfifo", 0666);
    
    for (int i = 0; i < 5; i++) {
        int fd = open("/tmp/myfifo", O_WRONLY);
        char msg[100];
        snprintf(msg, sizeof(msg), "Data %d", i);
        write(fd, msg, strlen(msg) + 1);
        printf("Process 1: Wrote '%s'\n", msg);
        close(fd);
        sleep(2);
    }

    unlink("/tmp/myfifo");
    return 0;
}

/* Process 2: fifo_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    sleep(1);
    
    for (int i = 0; i < 5; i++) {
        int fd = open("/tmp/myfifo", O_RDONLY);
        char buffer[100];
        read(fd, buffer, sizeof(buffer));
        printf("Process 2: Read '%s'\n", buffer);
        close(fd);
    }

    return 0;
}


// ============================================================================
// 7. FUTEXES (Advanced, Linux-specific)
// ============================================================================

/* Process 1: futex_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

int futex_wait(int *addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

int futex_wake(int *addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, NULL, NULL, 0);
}

int main() {
    int shm_fd = shm_open("/futex_shm", O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(int));
    int *futex_var = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, 
                          MAP_SHARED, shm_fd, 0);
    *futex_var = 0;

    for (int i = 0; i < 5; i++) {
        while (__sync_val_compare_and_swap(futex_var, 0, 1) != 0) {
            futex_wait(futex_var, 1);
        }
        printf("Process 1: Critical section %d\n", i);
        sleep(1);
        *futex_var = 0;
        futex_wake(futex_var, 1);
        sleep(1);
    }

    munmap(futex_var, sizeof(int));
    close(shm_fd);
    shm_unlink("/futex_shm");
    return 0;
}

/* Process 2: futex_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

int futex_wait(int *addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

int futex_wake(int *addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, NULL, NULL, 0);
}

int main() {
    sleep(1);
    int shm_fd = shm_open("/futex_shm", O_RDWR, 0666);
    int *futex_var = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd, 0);

    for (int i = 0; i < 5; i++) {
        while (__sync_val_compare_and_swap(futex_var, 0, 1) != 0) {
            futex_wait(futex_var, 1);
        }
        printf("Process 2: Critical section %d\n", i);
        sleep(1);
        *futex_var = 0;
        futex_wake(futex_var, 1);
        sleep(1);
    }

    munmap(futex_var, sizeof(int));
    close(shm_fd);
    return 0;
}


// ============================================================================
// 8. LOCK FILES (Simple approach)
// ============================================================================

/* Process 1: lockfile_process1.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int acquire_lock() {
    while (1) {
        int fd = open("/tmp/simple.lock", O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd >= 0) {
            close(fd);
            return 1;
        }
        if (errno == EEXIST) {
            usleep(100000);  // Wait 100ms
        } else {
            return 0;
        }
    }
}

void release_lock() {
    unlink("/tmp/simple.lock");
}

int main() {
    for (int i = 0; i < 5; i++) {
        acquire_lock();
        printf("Process 1: Lock acquired, iteration %d\n", i);
        sleep(2);
        release_lock();
        printf("Process 1: Lock released\n");
        sleep(1);
    }
    return 0;
}

/* Process 2: lockfile_process2.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int acquire_lock() {
    while (1) {
        int fd = open("/tmp/simple.lock", O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd >= 0) {
            close(fd);
            return 1;
        }
        if (errno == EEXIST) {
            usleep(100000);
        } else {
            return 0;
        }
    }
}

void release_lock() {
    unlink("/tmp/simple.lock");
}

int main() {
    sleep(1);
    for (int i = 0; i < 5; i++) {
        printf("Process 2: Waiting for lock...\n");
        acquire_lock();
        printf("Process 2: Lock acquired, iteration %d\n", i);
        sleep(2);
        release_lock();
        printf("Process 2: Lock released\n");
        sleep(1);
    }
    return 0;
}


/*
 * COMPILATION AND USAGE INSTRUCTIONS:
 * 
 * 1. POSIX Semaphores:
 *    gcc posix_sem_process1.c -o p1 -lpthread
 *    gcc posix_sem_process2.c -o p2 -lpthread
 *    Run: ./p1 & ./p2
 * 
 * 2. System V Semaphores:
 *    gcc sysv_sem_process1.c -o p1
 *    gcc sysv_sem_process2.c -o p2
 *    Run: ./p1 & ./p2
 * 
 * 3. Shared Memory with Mutex:
 *    gcc shm_mutex_process1.c -o p1 -lpthread -lrt
 *    gcc shm_mutex_process2.c -o p2 -lpthread -lrt
 *    Run: ./p1 & ./p2
 * 
 * 4. File Locks:
 *    gcc flock_process1.c -o p1
 *    gcc flock_process2.c -o p2
 *    Run: ./p1 & ./p2
 * 
 * 5. POSIX Message Queues:
 *    gcc mq_process1.c -o p1 -lrt
 *    gcc mq_process2.c -o p2 -lrt
 *    Run: ./p1 & ./p2
 * 
 * 6. Named Pipes (FIFOs):
 *    gcc fifo_process1.c -o p1
 *    gcc fifo_process2.c -o p2
 *    Run: ./p1 & ./p2
 * 
 * 7. Futexes:
 *    gcc futex_process1.c -o p1 -lrt
 *    gcc futex_process2.c -o p2 -lrt
 *    Run: ./p1 & ./p2
 * 
 * 8. Lock Files:
 *    gcc lockfile_process1.c -o p1
 *    gcc lockfile_process2.c -o p2
 *    Run: ./p1 & ./p2
 */
```