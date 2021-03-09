#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <omp.h>
#ifndef NO_MPI
#include <mpi.h>
#endif
#ifdef __linux__
#include <sched.h>
#include <numa.h>
#endif

#define HOSTNAME_MAX_LENGTH 64 // Max hostname length before truncation
#define RECORD_SIZE 128 // Max per-thread/process record size
#define RECORD_WORDS 6 // Number of words in each record

void output_records(const char *records, int count, const char **heads);
void update_widths(size_t *widths, const char *record);
void format_record(const char *record, const size_t *sizes, const char **heads);
void chew_cpu(int duration_secs);
#ifdef __linux__
char *cpuset_to_cstr(cpu_set_t *mask, char *str);
#endif

#ifdef __linux__
static const int is_linux = 1;
#else
static const int is_linux = 0;
#endif

#ifndef NO_MPI
static const int is_mpi = 1;
#else
static const int is_mpi = 0;
#endif


int main(int argc, char *argv[]) {
    int mpi_rank = -1;
    int num_threads = -1;
    int chew_cpu_secs = 0;
    int exit_code = EXIT_FAILURE;
    char *thread_data = NULL;

#ifndef NO_MPI
    int mpi_size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
#endif

    // Read in command line args
    // NOTE: Currently just CPU chew duration, but might become more exciting later
    if (argc>2) {
        fprintf(stderr, "Usage: xthi [cpu_chew_seconds]\n");
        goto EXIT;
    }
    if (argc==2) {
        chew_cpu_secs = atoi(argv[1]);
        if (chew_cpu_secs < 0) {
            fprintf(stderr, "CPU chew time must be positive\n");
            goto EXIT;
        }
    }

    // Get short part of hostname
    // NB: gethostname() doesn't necessarily null terminate on truncation, so add explicit terminator
    char hostname_buf[HOSTNAME_MAX_LENGTH + 1];
    hostname_buf[HOSTNAME_MAX_LENGTH] = '\0';
    gethostname(hostname_buf, HOSTNAME_MAX_LENGTH);
    char *dot_ptr = strchr(hostname_buf, '.');
    if (dot_ptr != NULL)
        *dot_ptr = '\0';

    // Launch OpenMP threads and gather data
    // Each thread will store RECORD_SIZE characters, stored as a flat single array
    thread_data = malloc(sizeof(char) * omp_get_max_threads() * RECORD_SIZE);
    #pragma omp parallel default(none) shared(hostname_buf, mpi_rank, thread_data, num_threads)
    {
        #pragma omp master
        num_threads = omp_get_num_threads();

        // Gather thread-specific info
        int thread_num = omp_get_thread_num();

        // Gather additional Linux-specific info (if available)
#ifdef __linux__
        int cpu = sched_getcpu();
        int numa_node = numa_node_of_cpu(cpu);
        char cpu_affinity_buf[7 * CPU_SETSIZE];

        cpu_set_t coremask;
        memset(cpu_affinity_buf, 0, sizeof(cpu_affinity_buf));
        sched_getaffinity(0, sizeof(coremask), &coremask);
        cpuset_to_cstr(&coremask, cpu_affinity_buf);
#else
        int cpu = -1;
        int numa_node = -1;
        char *cpu_affinity_buf = "-";
#endif

        // Record as a space-separated substring for easy MPI comms
        snprintf(thread_data + (thread_num * RECORD_SIZE), RECORD_SIZE,
                 "%s %d %d %d %d %.50s",
                 hostname_buf, mpi_rank, thread_num, cpu, numa_node, cpu_affinity_buf);
    }

    // Work out which headings to include
    const char *heads[RECORD_WORDS] = {
            "Host",
            is_mpi ? "MPI Rank" : NULL,
            num_threads > 1 ? "OMP Thread" : NULL,
            is_linux ? "CPU" : NULL,
            is_linux ? "NUMA Node" : NULL,
            is_linux ? "CPU Affinity" : NULL,
    };

    // Aggregate and output result
#ifdef NO_MPI
    output_records(thread_data, num_threads, heads);
#else
    // MPI tasks aggregate data back to manager process (mpi_rank==0)
    if (mpi_rank != 0) {
        // This is a worker - send data to manager
        MPI_Ssend(thread_data, RECORD_SIZE * num_threads, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    }
    else {
        // This is the manager - gather data from each worker
        int all_size = num_threads * mpi_size;
        char *all_data = malloc(sizeof(char) * RECORD_SIZE * all_size);

        // Copy manager thread's data into all data
        memcpy(all_data, thread_data, num_threads * RECORD_SIZE);

        // Then receive from each MPI task and each thread
        for (int j=1; j<mpi_size; ++j) {
            MPI_Recv(all_data + RECORD_SIZE * num_threads * j,
                     RECORD_SIZE * num_threads, MPI_CHAR,
                     j, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        output_records(all_data, all_size, heads);

        // Tidy up manager-specific stuff
        free(all_data);
    }
#endif

    // Maybe chew CPU for a bit
    if (chew_cpu_secs > 0) {
        #pragma omp parallel default(none) shared(chew_cpu_secs)
        {
            chew_cpu(chew_cpu_secs);
        }
    }

    // If we got here then everything is good
    exit_code = EXIT_SUCCESS;

    // Tidy up and exit.
    // I've done this as a goto (normally yuck!) since this includes
    // some conditional MPI stuff
    EXIT:
    if (thread_data)
        free(thread_data);
#ifndef NO_MPI
    MPI_Finalize();
#endif
    return exit_code;
}

/* Formats a CPU affinity mask in a nice way
 *
 * As with the original xthi code, this was taken from from:
 * util-linux-2.13-pre7/schedutils/taskset.c
 *
 * However this function is no longer included in
 * https://github.com/karelzak/util-linux/
 */
#ifdef __linux__
char *cpuset_to_cstr(cpu_set_t *mask, char *str) {
    char *ptr = str;
    int i, j, entry_made = 0;
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) {
            int run = 0;
            entry_made = 1;
            for (j = i + 1; j < CPU_SETSIZE; j++) {
                if (CPU_ISSET(j, mask)) run++;
                else break;
            }
            if (!run)
                sprintf(ptr, "%d,", i);
            else if (run == 1) {
                sprintf(ptr, "%d,%d,", i, i + 1);
                i++;
            } else {
                sprintf(ptr, "%d-%d,", i, i + run);
                i += run;
            }
            while (*ptr != 0) ptr++;
        }
    }
    ptr -= entry_made;
    *ptr = 0;
    return str;
}
#endif

/* Chews CPU for (at least) the given number of seconds */
void chew_cpu(const int duration_secs) {
    time_t start, end;
    time(&start);
    volatile int count = 0;
    do {
        for (int i=0; i<100000; ++i)
            ;
        ++count;
        time(&end);
    } while (difftime(end, start) < duration_secs);
}

/* Outputs all of the accumulated data in a reasonably nice formatted fashion
 *
 * Params:
 * records: flattened string-delimited data, as gathered in main()
 * count: number of records within the data array
 * heads: headings to output, a NULL suppresses a particular record
 */
void output_records(const char *records, int count, const char **heads) {
    // Calculate widths for formatting
    size_t widths[RECORD_WORDS] = { 0 };
    for (int k=0; k < count; ++k) {
        update_widths(widths, records + RECORD_SIZE * k);
    }

    // Output formatted messages
    for (int k=0; k < count; ++k) {
        format_record(records + RECORD_SIZE * k, widths, heads);
    }
}

/* Helper to check and update the current field widths for the given record */
void update_widths(size_t *widths, const char *record) {
    int cur_word = 0; // Current word index
    size_t cur_len = 0; // Length of current word
    do {
        if (*record == '\0' || *record == ' ') {
            // End of word / record
            assert(cur_word < RECORD_WORDS);
            if (cur_len > widths[cur_word]) {
                widths[cur_word] = cur_len;
            }
            ++cur_word;
            cur_len = 0;
        }
        else {
            // Inside a word
            ++cur_len;
        }
    } while (*record++);
}

/* Formats the given record */
void format_record(const char *record, const size_t *sizes, const char **heads) {
    int cur_word = 0; // Current word index
    const char *wordstart = record; // Start of current word
    do {
        if (*record == '\0' || *record == ' ') {
            // End of word/record
            assert((size_t) cur_word < sizeof(sizes));
            assert((size_t) cur_word < sizeof(heads));
            if (heads[cur_word]) {
                printf("%s=", heads[cur_word]);
                for (size_t j = 0; j < sizes[cur_word] - (record - wordstart); ++j) {
                    putchar(' ');
                }
                while (wordstart < record) {
                    putchar(*wordstart++);
                }
                if (*record == ' ') {
                    printf("  ");
                }
            }
            wordstart = record + 1;
            ++cur_word;
        }
    } while (*record++);
    putchar('\n');
}
