# File Monitoring in Linux

File monitoring is a crucial capability in Linux systems that allows applications to watch for changes in files and directories without constantly polling the filesystem. Linux provides two primary mechanisms for this: **inotify** and **fanotify**. These APIs enable efficient, event-driven monitoring of filesystem activities.

## inotify - File System Event Monitoring

**inotify** (inode notify) is a Linux kernel subsystem that monitors filesystem events and reports them to applications. It's designed for monitoring individual files or directories and is widely used in applications like file managers, backup tools, and development environments.

### Key System Calls

1. **inotify_init()** / **inotify_init1()** - Initialize an inotify instance
2. **inotify_add_watch()** - Add a file or directory to watch
3. **inotify_rm_watch()** - Remove a watch
4. **read()** - Read events from the inotify file descriptor

### How inotify Works

When you create an inotify instance, you get a file descriptor that you can use with standard I/O operations. You then add "watches" for specific paths, specifying which events you're interested in. When those events occur, the kernel queues event structures that you can read from the file descriptor.

### Common Event Types

- **IN_ACCESS** - File was accessed (read)
- **IN_MODIFY** - File was modified (write)
- **IN_ATTRIB** - Metadata changed (permissions, timestamps, etc.)
- **IN_CLOSE_WRITE** - File opened for writing was closed
- **IN_CLOSE_NOWRITE** - File not opened for writing was closed
- **IN_OPEN** - File was opened
- **IN_MOVED_FROM** / **IN_MOVED_TO** - File moved from/to watched directory
- **IN_CREATE** - File/directory created in watched directory
- **IN_DELETE** - File/directory deleted from watched directory
- **IN_DELETE_SELF** - Watched file/directory was deleted

### Practical Example: Monitoring a Directory

Here's a complete C example that monitors a directory for file creation and modification:

```c
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

int main(int argc, char **argv) {
    int fd, wd;
    char buffer[EVENT_BUF_LEN];
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        exit(1);
    }
    
    // Initialize inotify
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        exit(1);
    }
    
    // Add watch for directory
    wd = inotify_add_watch(fd, argv[1], 
                          IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd < 0) {
        perror("inotify_add_watch");
        exit(1);
    }
    
    printf("Watching directory: %s\n", argv[1]);
    
    // Event loop
    while (1) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            perror("read");
            exit(1);
        }
        
        int i = 0;
        while (i < length) {
            struct inotify_event *event = 
                (struct inotify_event *)&buffer[i];
            
            if (event->len) {
                if (event->mask & IN_CREATE) {
                    printf("File created: %s\n", event->name);
                }
                if (event->mask & IN_MODIFY) {
                    printf("File modified: %s\n", event->name);
                }
                if (event->mask & IN_DELETE) {
                    printf("File deleted: %s\n", event->name);
                }
            }
            
            i += EVENT_SIZE + event->len;
        }
    }
    
    // Cleanup
    inotify_rm_watch(fd, wd);
    close(fd);
    
    return 0;
}
```

### Python Example with inotify

Python's `inotify_simple` library provides an easier interface:

```python
from inotify_simple import INotify, flags

inotify = INotify()
watch_flags = flags.CREATE | flags.MODIFY | flags.DELETE
wd = inotify.add_watch('/path/to/watch', watch_flags)

print("Watching for changes...")
while True:
    for event in inotify.read():
        for flag in flags.from_mask(event.mask):
            print(f'{flag}: {event.name}')
```

### inotify Limitations

inotify has some important limitations you should be aware of:

- **Watches are not recursive** - You must explicitly add watches for subdirectories
- **Resource limits** - System limits on number of watches (check `/proc/sys/fs/inotify/max_user_watches`)
- **No network filesystem support** - Doesn't work reliably on NFS, CIFS, etc.
- **Queue overflow** - Events can be lost if the queue fills up (IN_Q_OVERFLOW event)

## fanotify - File Access Notification

**fanotify** is a more powerful and flexible monitoring system introduced in Linux 2.6.36. Unlike inotify, fanotify can monitor entire mount points and can make permission decisions, allowing or denying file access in real-time.

### Key Features

fanotify provides capabilities that inotify doesn't, including monitoring entire filesystems, receiving events before operations complete (permission events), and getting file descriptors to affected files rather than just pathnames.

### System Calls

1. **fanotify_init()** - Initialize fanotify
2. **fanotify_mark()** - Add, remove, or modify marks
3. **read()** - Read events
4. **write()** - Respond to permission events

### Event Types

- **FAN_ACCESS** - File was accessed
- **FAN_MODIFY** - File was modified
- **FAN_CLOSE_WRITE** - Writable file closed
- **FAN_CLOSE_NOWRITE** - Read-only file closed
- **FAN_OPEN** - File was opened
- **FAN_OPEN_EXEC** - File was opened for execution
- **FAN_ATTRIB** - File attributes changed
- **FAN_CREATE** - File/directory created
- **FAN_DELETE** - File/directory deleted
- **FAN_MOVE** - File/directory moved

### Permission Events

- **FAN_OPEN_PERM** - Permission to open requested
- **FAN_ACCESS_PERM** - Permission to access requested
- **FAN_OPEN_EXEC_PERM** - Permission to execute requested

### Practical Example: Monitoring Mount Point

Here's a C example that monitors all file opens on a mount point:

```c
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/fanotify.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

int main(int argc, char **argv) {
    int fan_fd;
    char path[PATH_MAX];
    ssize_t len;
    char buf[4096];
    struct fanotify_event_metadata *metadata;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mount_point>\n", argv[0]);
        exit(1);
    }
    
    // Initialize fanotify (requires CAP_SYS_ADMIN)
    fan_fd = fanotify_init(FAN_CLASS_NOTIF | FAN_CLOEXEC,
                          O_RDONLY | O_LARGEFILE);
    if (fan_fd < 0) {
        perror("fanotify_init (may need root privileges)");
        exit(1);
    }
    
    // Mark mount point to monitor
    if (fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
                     FAN_OPEN | FAN_CLOSE_WRITE,
                     AT_FDCWD, argv[1]) < 0) {
        perror("fanotify_mark");
        exit(1);
    }
    
    printf("Monitoring mount point: %s\n", argv[1]);
    printf("Waiting for events...\n");
    
    while (1) {
        len = read(fan_fd, buf, sizeof(buf));
        if (len < 0) {
            perror("read");
            exit(1);
        }
        
        metadata = (struct fanotify_event_metadata *)buf;
        
        while (FAN_EVENT_OK(metadata, len)) {
            if (metadata->vers != FANOTIFY_METADATA_VERSION) {
                fprintf(stderr, "Mismatch of fanotify metadata version\n");
                exit(1);
            }
            
            // Get path from file descriptor
            snprintf(path, sizeof(path), "/proc/self/fd/%d", 
                    metadata->fd);
            char filepath[PATH_MAX];
            ssize_t path_len = readlink(path, filepath, 
                                       sizeof(filepath) - 1);
            if (path_len > 0) {
                filepath[path_len] = '\0';
                
                if (metadata->mask & FAN_OPEN) {
                    printf("File opened: %s (PID: %d)\n", 
                          filepath, metadata->pid);
                }
                if (metadata->mask & FAN_CLOSE_WRITE) {
                    printf("File closed after write: %s (PID: %d)\n",
                          filepath, metadata->pid);
                }
            }
            
            close(metadata->fd);
            metadata = FAN_EVENT_NEXT(metadata, len);
        }
    }
    
    close(fan_fd);
    return 0;
}
```

### Example: Permission Enforcement

fanotify can be used to implement security policies by blocking file access:

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/fanotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main() {
    int fan_fd;
    struct fanotify_response response;
    
    // Initialize with permission class
    fan_fd = fanotify_init(FAN_CLASS_CONTENT | FAN_CLOEXEC,
                          O_RDONLY | O_LARGEFILE);
    
    // Mark directory for permission events
    fanotify_mark(fan_fd, FAN_MARK_ADD,
                 FAN_OPEN_PERM | FAN_OPEN_EXEC_PERM,
                 AT_FDCWD, "/sensitive/directory");
    
    while (1) {
        char buf[4096];
        ssize_t len = read(fan_fd, buf, sizeof(buf));
        struct fanotify_event_metadata *metadata = 
            (struct fanotify_event_metadata *)buf;
        
        while (FAN_EVENT_OK(metadata, len)) {
            if (metadata->mask & FAN_OPEN_PERM) {
                // Check if we should allow access
                // (simplified - would check against policy)
                response.fd = metadata->fd;
                response.response = FAN_ALLOW; // or FAN_DENY
                
                write(fan_fd, &response, sizeof(response));
            }
            
            close(metadata->fd);
            metadata = FAN_EVENT_NEXT(metadata, len);
        }
    }
    
    return 0;
}
```

## Comparison: inotify vs fanotify

**Use inotify when:**
- Monitoring specific files or directories
- Building desktop applications (file managers, editors)
- Implementing directory synchronization
- Don't need root privileges
- Want simpler API

**Use fanotify when:**
- Need to monitor entire filesystems or mount points
- Implementing security/antivirus scanning
- Need permission control (allow/deny operations)
- Need to know which process triggered events
- Working on system-level tools (requires root)

## Real-World Use Cases

**inotify applications** include file synchronization tools like Dropbox and Syncthing, IDEs that auto-reload files when changed externally, backup utilities that track changes, and build systems that recompile on source changes.

**fanotify applications** include antivirus scanners that check files before execution, hierarchical storage management systems, filesystem indexers for desktop search, and security monitoring tools that audit file access.

## Best Practices

When using these APIs, make sure to handle the IN_Q_OVERFLOW/FAN_Q_OVERFLOW events to detect lost events, use select/poll/epoll for efficient event waiting, set appropriate buffer sizes for read operations, and properly handle cleanup when watches are removed. Remember that recursive directory watching with inotify requires manual traversal, and fanotify operations typically require elevated privileges (CAP_SYS_ADMIN).

Both inotify and fanotify are powerful tools for building responsive, efficient applications that need to react to filesystem changes in real-time. Choose the right tool based on your specific monitoring needs and privilege requirements.