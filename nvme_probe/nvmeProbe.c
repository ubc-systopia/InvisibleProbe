#include "utils.h"

void run_probe(void);

// CPU clock frequency in Hz (obtainable via `sudo dmesg | grep -i tsc`)
#define CPU_HZ 4700000000.0
#define NUM_READ_OPS 0x800000    // Number of read operations
#define LOG_FILE "records.bin"
#define LBA_BLOCKS 8
#define BUFFER_SIZE (LBA_BLOCKS * 512) // 4KB buffer size
#define READ_START_LBA 0x10000
#define CMD_QUEUE_LIMIT 256

struct io_sequence {
    struct ns_entry *ns_entry;
    char *buffer;
    unsigned using_cmb_io;
    uint64_t completed_io_count;
};

static void handle_read_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
    struct io_sequence *sequence = arg;
    sequence->completed_io_count++;

    if (spdk_nvme_cpl_is_error(completion)) {
        spdk_nvme_qpair_print_completion(sequence->ns_entry->qpair, (struct spdk_nvme_cpl *)completion);
        fprintf(stderr, "I/O error: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
        fprintf(stderr, "Read I/O failed, aborting.\n");
    }
}

void run_probe(void) {
    uint64_t *timestamps = spdk_zmalloc(sizeof(uint64_t) * NUM_READ_OPS, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
    memset(timestamps, 0, sizeof(uint64_t) * NUM_READ_OPS);
    volatile uint64_t ops_in_progress = 0;

    struct ns_entry *ns_entry = g_namespaces;
    ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, NULL, 0);
    if (!ns_entry->qpair) {
        fprintf(stderr, "Error: Failed to allocate I/O queue pair.\n");
        return;
    }

    struct io_sequence sequence = {
        .ns_entry = ns_entry,
        .buffer = spdk_zmalloc(BUFFER_SIZE, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA),
        .completed_io_count = 0
    };
    sequence.buffer[0] = 1;

	// initial read command to discard first time extra-effects.
    int rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, sequence.buffer,
                                   READ_START_LBA, LBA_BLOCKS, handle_read_complete, &sequence, 0);
    if (rc != 0) {
        fprintf(stderr, "Error: Failed to start read I/O.\n");
        exit(1);
    }

    uint64_t completed_ops = 0;
    uint64_t remaining_ops = NUM_READ_OPS;

    while (remaining_ops > 0) {
        while (sequence.completed_io_count == completed_ops) {
            spdk_nvme_qpair_process_completions(ns_entry->qpair, 1);
        }

        timestamps[ops_in_progress++] = rdtscp();
        remaining_ops--;
        completed_ops++;

        while (ops_in_progress < CMD_QUEUE_LIMIT && remaining_ops > 0) {
            rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, sequence.buffer,
                                       READ_START_LBA, LBA_BLOCKS, handle_read_complete, &sequence, 0);
            if (rc != 0) {
                fprintf(stderr, "Error: Failed to start read I/O.\n");
                exit(1);
            }
            ops_in_progress++;
        }
    }

    while (ops_in_progress-- > 0) {
        while (sequence.completed_io_count == completed_ops) {
            spdk_nvme_qpair_process_completions(ns_entry->qpair, 1);
        }
        completed_ops++;
    }

    spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);
    printf("Detection complete. Starting analysis...\n");

    FILE *file = fopen(LOG_FILE, "wb");
    fwrite(timestamps, sizeof(uint64_t), NUM_READ_OPS, file);
    fclose(file);

    uint64_t total_time = timestamps[NUM_READ_OPS - 1] - timestamps[0];
    double total_seconds = total_time / CPU_HZ;
    double avg_time_per_op = total_seconds / (NUM_READ_OPS - 1);
    double iops = (NUM_READ_OPS - 1) / total_seconds;
    double throughput_gb = ((NUM_READ_OPS - 1) * LBA_BLOCKS / 2) / total_seconds / 1024 / 1024;

    printf("Average operation time: %.3e seconds\n", avg_time_per_op);
    printf("IOPS: %.3f\n", iops);
    printf("Throughput: %.3f GB/s\n", throughput_gb);
}

int main(int argc, char **argv) {
    struct spdk_env_opts opts;
    spdk_env_opts_init(&opts);
    opts.name = "hello_world";
    opts.shm_id = 0;

    if (spdk_env_init(&opts) < 0) {
        fprintf(stderr, "Error: Failed to initialize SPDK environment.\n");
        return 1;
    }

    printf("Initializing NVMe controllers...\n");
    if (spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL) != 0) {
        fprintf(stderr, "Error: Failed to probe NVMe controllers.\n");
        cleanup();
        return 1;
    }

    if (!g_controllers) {
        fprintf(stderr, "Error: No NVMe controllers found.\n");
        cleanup();
        return 1;
    }

    printf("Initialization complete.\n");
    run_probe();
    cleanup();
    return 0;
}
