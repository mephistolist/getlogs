/* This code is to quickly find access logs for common web services in the event a Linux server
   is under heavy load. You may compile with the following:
   cc -O3 -pipe -march=native -flto -pthread -Wall -Wextra getlogs.c -o getlogs
*/
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_PATH 256
#define MAX_THREADS 8
#define QUEUE_SIZE 256
#define MAX_LOG_FILES 100

typedef struct {
    char path[MAX_PATH];
    int valid;
} work_item_t;

typedef struct {
    work_item_t queue[QUEUE_SIZE];
    int head, tail;
    sem_t empty, full, mutex;
} thread_pool_t;

/* Set to track unique log files */
typedef struct {
    char *log_files[MAX_LOG_FILES];
    int count;
    pthread_mutex_t mutex;
} log_set_t;

thread_pool_t pool;
pthread_t workers[MAX_THREADS];
log_set_t log_set = { .count = 0, .mutex = PTHREAD_MUTEX_INITIALIZER };
int verbose_mode = 0;  // Set to 1 to see what's being checked

void init_log_set(void) {
    log_set.count = 0;
    pthread_mutex_init(&log_set.mutex, NULL);
}

/* Add log file to set if not already there */
int add_log_file(const char *log_path) {
    pthread_mutex_lock(&log_set.mutex);
    
    // Check if already in set
    for (int i = 0; i < log_set.count; i++) {
        if (log_set.log_files[i] && strcmp(log_set.log_files[i], log_path) == 0) {
            pthread_mutex_unlock(&log_set.mutex);
            return 0; // Already exists
        }
    }
    
    // Add to set if space available
    if (log_set.count < MAX_LOG_FILES) {
        log_set.log_files[log_set.count] = strdup(log_path);
        if (log_set.log_files[log_set.count]) {
            log_set.count++;
            pthread_mutex_unlock(&log_set.mutex);
            return 1; // Added successfully
        }
    }
    
    pthread_mutex_unlock(&log_set.mutex);
    return 0; // Failed to add
}

void print_unique_logs(void) {
    pthread_mutex_lock(&log_set.mutex);
    for (int i = 0; i < log_set.count; i++) {
        printf("%s\n", log_set.log_files[i]);
    }
    pthread_mutex_unlock(&log_set.mutex);
}

void cleanup_log_set(void) {
    pthread_mutex_lock(&log_set.mutex);
    for (int i = 0; i < log_set.count; i++) {
        free(log_set.log_files[i]);
    }
    log_set.count = 0;
    pthread_mutex_unlock(&log_set.mutex);
    pthread_mutex_destroy(&log_set.mutex);
}

void init_thread_pool(thread_pool_t *pool) {
    pool->head = 0;
    pool->tail = 0;
    sem_init(&pool->empty, 0, QUEUE_SIZE);
    sem_init(&pool->full, 0, 0);
    sem_init(&pool->mutex, 0, 1);
}

void submit_work(thread_pool_t *pool, const char *path) {
    sem_wait(&pool->empty);
    sem_wait(&pool->mutex);
    
    strncpy(pool->queue[pool->tail].path, path, MAX_PATH - 1);
    pool->queue[pool->tail].path[MAX_PATH - 1] = '\0';
    pool->queue[pool->tail].valid = 1;
    pool->tail = (pool->tail + 1) % QUEUE_SIZE;
    
    sem_post(&pool->mutex);
    sem_post(&pool->full);
}

void stop_thread_pool(thread_pool_t *pool) {
    /* Send stop signals to all workers */
    for (int i = 0; i < MAX_THREADS; i++) {
        sem_wait(&pool->empty);
        sem_wait(&pool->mutex);
        
        pool->queue[pool->tail].valid = 0;
        pool->tail = (pool->tail + 1) % QUEUE_SIZE;
        
        sem_post(&pool->mutex);
        sem_post(&pool->full);
    }
    
    /* Wait for all workers to finish */
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(workers[i], NULL);
    }
    
    sem_destroy(&pool->empty);
    sem_destroy(&pool->full);
    sem_destroy(&pool->mutex);
}

void *worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    const char *match = "access";
    
    while (1) {
        sem_wait(&pool->full);
        sem_wait(&pool->mutex);
        
        work_item_t work = pool->queue[pool->head];
        pool->head = (pool->head + 1) % QUEUE_SIZE;
        
        sem_post(&pool->mutex);
        sem_post(&pool->empty);
        
        if (!work.valid) break; /* Exit signal */
        
        /* Get symlink size */
        struct stat sb;
        if (lstat(work.path, &sb) == -1) {
            if (verbose_mode) fprintf(stderr, "Failed to stat: %s\n", work.path);
            continue;
        }
        
        if (!S_ISLNK(sb.st_mode)) {
            if (verbose_mode) fprintf(stderr, "Not a symlink: %s\n", work.path);
            continue;
        }
        
        // Allocate buffer and read link
        char *linkname = malloc(sb.st_size + 1);
        if (!linkname) {
            continue;
        }
        
        ssize_t r = readlink(work.path, linkname, sb.st_size + 1);
        if (r == -1) {
            if (verbose_mode) fprintf(stderr, "Failed to readlink: %s\n", work.path);
            free(linkname);
            continue;
        }
        
        linkname[r] = '\0';
        
        if (verbose_mode) {
            fprintf(stderr, "Checking: %s -> %s\n", work.path, linkname);
        }
        
        if (strstr(linkname, match)) {
            if (verbose_mode) {
                fprintf(stderr, "MATCH: %s contains 'access'\n", linkname);
            }
            add_log_file(linkname);
        } else if (verbose_mode) {
            fprintf(stderr, "NO MATCH: %s does not contain 'access'\n", linkname);
        }
        
        free(linkname);
    }
    
    return NULL;
}

void start_thread_pool(thread_pool_t *pool) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, pool) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
}

void findpid(const char *filename) {
    char pid[16] = {0};
    char procdir[MAX_PATH];
    FILE *file = fopen(filename, "r");
    
    if (!file) return;
    
    fgets(pid, sizeof(pid), file);
    char *newline = strchr(pid, '\n');
    if (newline) *newline = '\0';
    fclose(file);
    
    snprintf(procdir, sizeof(procdir), "/proc/%s/fd/", pid);
    
    if (verbose_mode) {
        fprintf(stderr, "Scanning directory: %s\n", procdir);
    }
    
    DIR *dp = opendir(procdir);
    if (!dp) {
        perror("opendir");
        return;
    }
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dp)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        
        char path[MAX_PATH];
        int n = snprintf(path, sizeof(path), "%s%s", procdir, entry->d_name);
        if (n < 0 || n >= MAX_PATH) {
            continue;
        }
        
        if (verbose_mode) {
            fprintf(stderr, "Found FD: %s\n", entry->d_name);
        }
        
        submit_work(&pool, path);
        count++;
    }
    
    closedir(dp);
    
    if (verbose_mode && count > 0) {
        fprintf(stderr, "Submitted %d file descriptors for PID %s\n", count, pid);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose_mode = 1;
        fprintf(stderr, "Verbose mode enabled\n");
    }
    
    /* Initialize thread pool */
    init_thread_pool(&pool);
    
    /* Initialize log set */
    init_log_set();
    
    /* Start threads */
    start_thread_pool(&pool);
    
    const char *pid_files[] = {
        "/var/run/apache2/apache2.pid",
        "/var/run/httpd/httpd.pid", 
        "/var/run/nginx.pid",
        "/var/run/lighttpd.pid",
        "/run/apache2/apache2.pid",
        "/run/httpd/httpd.pid",
        "/run/nginx.pid",
        NULL
    };
    
    int found = 0;
    for (int i = 0; pid_files[i] != NULL; i++) {
        if (access(pid_files[i], R_OK) == 0) {
            if (verbose_mode) {
                fprintf(stderr, "Found PID file: %s\n", pid_files[i]);
            }
            found = 1;
            findpid(pid_files[i]);
        }
    }
    
    /* Wait for all worker threads to finish */
    stop_thread_pool(&pool);
    
    if (found) {
        print_unique_logs();
    } else {
        printf(
            "Not finding any of the usual suspects...\n"
            "Try: netstat -naltp | awk '/:80|:443|:8080/ && /LISTEN/'\n"
            "Or check running processes: ps aux | grep -E '(apache|httpd|nginx|lighttpd)'\n"
        );
    }
    
    cleanup_log_set();
    
    return 0;
}
