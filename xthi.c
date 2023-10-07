/*
 * xthi - alternative implementation for the School of Physics & Astronomy
 *
 * Full details: https://git.ecdf.ed.ac.uk/dmckain/xthi
 */
#ifdef __linux__
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
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
#ifdef CUDA
#include <cuda_runtime.h>
#endif
#ifdef HIP
#include <hip/hip_runtime.h>
#endif

#define HOSTNAME_MAX_LENGTH 64 // Max hostname length before truncation
#define RECORD_SIZE 512 // Max per-thread/process record size
#define RECORD_WORDS 7 // Number of words in each record
#define DEV_ID_LENGTH 15
#define MAX_DEVICES 16

// Brief usage instructions
const char *usage =
  "Enhanced version of Cray's wee xthi \"where am I running?\" parallel code.\n"
  "\n"
  "Usage:\n"
  "     xthi [cpu_chew_seconds] [--map-gpu-by-rank]\n"
  "*or* xthi.nompi [cpu_chew_seconds] [--map-gpu-by-rank]\n"
  "\n"
  "Full details: https://git.ecdf.ed.ac.uk/dmckain/xthi\n";

void do_xthi(long chew_cpu_secs, int flag_gpu_by_rank);
void output_records(const char *records, int count, const char **heads);
void update_widths(size_t *widths, const char *record);
void format_record(const char *record, const size_t *sizes, const char **heads);
void chew_cpu(long chew_cpu_secs);
int parse_args(int argc, char *argv[], long *chew_cpu_secs, int *flag_gpu_by_rank);
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

#ifdef CUDA
static const int is_cuda = 1;
#else
static const int is_cuda = 0;
#endif
#ifdef HIP
static const int is_hip = 1;
#else
static const int is_hip = 0;
#endif

int main(int argc, char *argv[]) {
  int exit_code = EXIT_SUCCESS;
  long chew_cpu_secs = 0L;
  int flag_gpu_by_rank = 0;
#ifndef NO_MPI
  MPI_Init(&argc, &argv);
#endif

  if (parse_args(argc, argv, &chew_cpu_secs, &flag_gpu_by_rank)) {
  // Command line args are good => do xthi work
    do_xthi(chew_cpu_secs, flag_gpu_by_rank);
  }
  else {
    // Bad args => return failure
    exit_code = EXIT_FAILURE;
  }

#ifndef NO_MPI
  MPI_Finalize();
#endif
  return exit_code;
}

#ifdef CUDA
void query_devices(char *gpu_ids, int buflen, int mpi_rank, int flag_gpu_by_rank) {
  int dev, deviceCount = 0;
  flag_gpu_by_rank = flag_gpu_by_rank && mpi_rank >= 0; /* flag disabled when mpi_rank < 0 (no MPI) */
  char gpu_id[DEV_ID_LENGTH];
  gpu_id[0] = 0;
  cudaGetDeviceCount(&deviceCount);
  if(deviceCount) {
    for (dev=0; dev<deviceCount; ++dev) {
      if ( flag_gpu_by_rank && mpi_rank != dev ) {
	continue; /* when mapping by rank, skip unless dev == rank */
      }
      cudaSetDevice(dev);
      // gpu_id [domain]:[bus]:[device].[function]
      cudaDeviceGetPCIBusId(gpu_id, DEV_ID_LENGTH, dev);
      char *beg = strstr(gpu_id,":");
      beg -= 2; // two digits before :
      char *end = strstr(beg,".");
      strncat(gpu_ids,beg,end-beg);
      if( ! flag_gpu_by_rank && dev < deviceCount-1)
	strncat(gpu_ids,";",buflen);
    }
    if ( flag_gpu_by_rank && mpi_rank >= deviceCount ) strncat(gpu_ids,"None",buflen);
  } else {
    strncat(gpu_ids,"None",buflen);
  }
}
#endif
#ifdef HIP
void query_devices(char *gpu_ids, int buflen, int mpi_rank, int flag_gpu_by_rank) {
  int dev, deviceCount = 0;
  flag_gpu_by_rank = flag_gpu_by_rank && mpi_rank >= 0; /* flag disabled when mpi_rank < 0 (no MPI) */
  char gpu_id[DEV_ID_LENGTH];
  gpu_id[0] = 0;
  hipGetDeviceCount(&deviceCount);
  if(deviceCount) {
    for (dev=0; dev<deviceCount; ++dev) {
      if ( flag_gpu_by_rank && mpi_rank != dev ) {
	continue; /* when mapping by rank, skip unless dev == rank */
      }
      hipSetDevice(dev);
      hipDeviceGetPCIBusId(gpu_id, DEV_ID_LENGTH, dev);
      char *beg = strstr(gpu_id,":");
      beg -= 2; // two digits before :
      char *end = strstr(beg,".");
      strncat(gpu_ids,beg,end-beg);
      if( ! flag_gpu_by_rank && dev < deviceCount-1)
	strncat(gpu_ids,";",buflen);
    }
    if ( flag_gpu_by_rank && mpi_rank >= deviceCount ) strncat(gpu_ids,"None",buflen);
  } else {
    strncat(gpu_ids,"None",buflen);
  }
}
#endif

/* Main xthi work - the fun stuff lives here! */
void do_xthi(long chew_cpu_secs, int flag_gpu_by_rank) {
  int mpi_rank = -1;
  int mpi_local_rank = -1;
  int num_threads = -1;
  char *thread_data = NULL;
#ifndef NO_MPI
  int mpi_size;
  int mpi_local_nrank;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
  // determine the node-local MPI rank and number of ranks
  MPI_Comm node_comm;
  MPI_Comm_split_type(MPI_COMM_WORLD,MPI_COMM_TYPE_SHARED,mpi_rank,MPI_INFO_NULL,&node_comm);
  MPI_Comm_rank(node_comm,&mpi_local_rank);
  MPI_Comm_size(node_comm,&mpi_local_nrank);
  printf("%d/%d %d/%d\n",mpi_rank,mpi_size,mpi_local_rank,mpi_local_nrank);
#endif

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
  thread_data = (char*) malloc(sizeof(char) * omp_get_max_threads() * RECORD_SIZE);
  memset(thread_data,0,omp_get_max_threads() * RECORD_SIZE);
#pragma omp parallel default(none) shared(hostname_buf, mpi_rank, mpi_local_rank, thread_data, num_threads, flag_gpu_by_rank)
  {
    // Let each thread do a short CPU chew
    chew_cpu(0);
    
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
#if defined(CUDA) || defined(HIP)
    const int dbuflen = MAX_DEVICES*(DEV_ID_LENGTH+1)+1;
    char gpu_ids[dbuflen];
    gpu_ids[0] = 0;
    query_devices(gpu_ids,dbuflen,mpi_local_rank,flag_gpu_by_rank);
#else
    char *gpu_ids = (char*) NULL;
#endif
    #pragma omp critical 
    {
      // Record as a space-separated substring for easy MPI comms
      snprintf(thread_data + (thread_num * RECORD_SIZE), RECORD_SIZE,
             "%s %d %d %d %d %.50s %.128s",
	     hostname_buf, mpi_rank, thread_num, cpu, numa_node, cpu_affinity_buf, gpu_ids);
    }
  }

  // Work out which headings to include
  const char *heads[RECORD_WORDS] = {
    "Host",
    is_mpi ? "MPI-Rank" : NULL,
    num_threads > 1 ? "OMP-Thread" : NULL,
    is_linux ? "CPU" : NULL,
    is_linux ? "NUMA-Node" : NULL,
    is_linux ? "CPU-Affinity" : NULL,
    ( is_cuda || is_hip ) ? "GPU-IDs" : NULL,
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
    char *all_data = (char*) malloc(sizeof(char) * RECORD_SIZE * all_size);
    
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

  // Tidy up
  free(thread_data);
}


/* Chews CPU for roughly (i.e. at least) the given number of seconds */
void chew_cpu(const long chew_cpu_secs) {
  time_t start, end;
  time(&start);
  volatile int count = 0;
  do {
    for (int i=0; i<100000; ++i)
      ;
    ++count;
    time(&end);
  } while (difftime(end, start) < chew_cpu_secs);
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
  fflush(stdout);
}

/* Helper to check and update the current field widths for the given record */
void update_widths(size_t *widths, const char *record) {
  int cur_word = 0; // Current word index
  size_t cur_len = 0; // Length of current word
  int j;
  for ( j=0; j<RECORD_SIZE; ++j)
    {
      if (record[j] == ' ' || record[j] == 0) {
	if (cur_len > widths[cur_word]) {
	  widths[cur_word] = cur_len;
	}
	++cur_word;
	cur_len = 0;
      }
      else {
	++cur_len;
      }
      if (record[j] == 0) break;
    }
}

/* Formats the given record */
void format_record(const char *record, const size_t *sizes, const char **heads) {
  int breaks[RECORD_WORDS+1];
  // find whitespace breaks separating records
  breaks[0] = -1; // start of record
  int bindx = 1;
  int j;
  for ( j=0; j<RECORD_SIZE; ++j)
    {
      if (record[j] == ' ' || record[j] == 0)
	{
	  breaks[bindx++] = j;
	}
      if (bindx == RECORD_WORDS+1)
	break;
    }
  // print located records
  int it;
  for (it=0; it < RECORD_WORDS; ++it)
    {
      int k;
      if (heads[it] ) {
	// print the field
	printf("%s=", heads[it]);
	int startc = breaks[it]+1;
	int endc = breaks[it+1];
	int ws = sizes[it] - (endc - startc);
	for (k=0; k<ws; ++k)
	  putchar(' '); // right justify
	for (k=startc; k<endc; ++k )
	  putchar(record[k]);
	putchar(' ');
      }
    }
  printf("\n");
}

/* Parses command line arguments.
 *
 * Returns 1 if all good, 0 otherwise.
 */
int parse_args(int argc, char *argv[], long *chew_cpu_secs, int *flag_gpu_by_rank) {
  /* parse option flags */
  *chew_cpu_secs = 0L; /* default */
  *flag_gpu_by_rank = 0; /* default */
  int arg;
  for (arg=1; arg<argc; ++arg) {
    if ( strcmp(argv[arg],"--help") == 0 || strcmp(argv[arg],"-h") == 0 ) {
      fputs(usage, stderr);
      return 0;
    }
    if ( strcmp(argv[arg],"--map-gpu-by-rank") == 0 ) {
      *flag_gpu_by_rank = 1;
      continue;
    }
    if ( isdigit(argv[arg][0]) ) { /* numeric => chew_cpu_secs */ 
      char **endptr = &argv[arg];
      *chew_cpu_secs = strtol(argv[arg], endptr, 10);
      if (**endptr != '\0') {
	fputs(usage, stderr);
	return 0;
      }
      continue;
    }
    /* else, it's an unknown arg */
    fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], argv[arg]);
    fputs(usage, stderr);
    return 0;
  }
  return 1;
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
