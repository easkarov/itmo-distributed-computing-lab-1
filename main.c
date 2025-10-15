#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>              /* Definition of O_* constants */
#include <unistd.h>
#include "ipc.h"
#include "pa1.h"
#include <time.h>
#include <sys/wait.h>    // для waitpid

#define EVENTS_LOG_FILE_NAME "events.log"

typedef struct {
    int inputStream;
    int outputStream;
} PipeInputOutputStream2;

typedef struct {
    local_id id;
    int8_t count;
    pid_t pid;
    pid_t ppid;
} ProcessInfo;

PipeInputOutputStream2 streams[256][256];
ProcessInfo process_info;

FILE *events_log_file;

void close_fds(int curId, int x) {
    for (int i = 0; i <= x; i++) {
        if (i == curId) {
            continue;
        }
        for (int j = 0; j <= x; j++) {
            close(streams[i][j].outputStream);
            close(streams[i][j].inputStream);
        }
    }
}

int send(void *self, local_id dst, const Message *msg) {
    const ssize_t written = write(streams[process_info.id][dst].outputStream, msg, sizeof(Message));
    if (written < sizeof(Message)) {
        return 1;
    }

    return 0;
}

int send_multicast(void *self, const Message *msg) {
    for (int8_t neighId = 0; neighId <= process_info.count; neighId++) {
        if (process_info.id != neighId) {
            send(self, neighId, msg);
        }
    }

    return 0;
}

int receive(void *self, local_id from, Message *msg) {
    ssize_t count = read(streams[process_info.id][from].inputStream, msg, sizeof(Message));
    if (count < sizeof(Message)) {
        return 1;
    }

    return 0;
}

int receive_multicast(void *self, Message *msg) {
    for (int8_t neighId = 0; neighId <= process_info.count; neighId++) {
        if (process_info.id != neighId && neighId != PARENT_ID) {
            receive(self, neighId, msg);
        }
    }

    return 0;
}


int child() {
    Message startMessage = {
        .s_header = {
            .s_magic = MESSAGE_MAGIC,
            .s_type = STARTED,
            .s_local_time = (short) (process_info.id * 10 + 1),
        }
    };
    sprintf(startMessage.s_payload, log_started_fmt, process_info.id, process_info.pid, process_info.ppid);
    startMessage.s_header.s_payload_len = strlen(startMessage.s_payload) + 1;

    printf(log_started_fmt, process_info.id, process_info.pid, process_info.ppid);
    fprintf(events_log_file, log_started_fmt, process_info.id, process_info.pid, process_info.ppid);

    close_fds(process_info.id, process_info.count);
    send_multicast(0, &startMessage);

    receive_multicast(0, &startMessage);

    printf(log_received_all_started_fmt, process_info.id);
    fprintf(events_log_file, startMessage.s_payload, log_received_all_started_fmt, process_info.id);


    Message doneMessage = {
            .s_header = {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = DONE,
                    .s_local_time = (short) (process_info.id * 10 + 1),
            },
    };

    sprintf(doneMessage.s_payload, log_done_fmt, process_info.id);
    doneMessage.s_header.s_payload_len = strlen(doneMessage.s_payload) + 1;

    // working...
    printf(log_done_fmt, process_info.id);
    fprintf(events_log_file, log_done_fmt, process_info.id);

    send_multicast(0, &doneMessage);

    receive_multicast(0, &doneMessage);

    printf(log_received_all_done_fmt, process_info.id);
    fprintf(events_log_file, log_received_all_done_fmt, process_info.id);

    return 0;
}


int main(int argc, char *argv[]) {
    if (argc <= 1) {
        printf("error <1\n");
        return 1;
    }

    char *arg = NULL;
    int8_t x = 0;
    for (int i = 1; i < argc; i++) {
        arg = strtok(argv[i], " ");
        if (strcmp(arg, "-p") == 0) {
            x = atoi(argv[i + 1]);
        }
    }
    if (x <= 0) {
        printf("error NULL\n");
        return 1;
    }

    events_log_file = fopen(EVENTS_LOG_FILE_NAME, "w");

    if (events_log_file == NULL){
        printf("error opening events log file!");
        return 1;
    }

    for (int curId = 0; curId <= x; curId++) {
        for (int neighbourId = curId + 1; neighbourId <= x; neighbourId++) {
            if (neighbourId == curId) {
                continue;
            }
            if (streams[curId][neighbourId].inputStream != 0 || streams[curId][neighbourId].outputStream != 0){
                continue;
            }
            int fds[2];
            if (pipe(fds)) {
                printf("error while piping 1\n");
                return 1;
            }

            streams[curId][neighbourId].inputStream = fds[0];
            streams[neighbourId][curId].outputStream = fds[1];

            if (pipe(fds)) {
                printf("error while piping 2\n");
                return 1;
            }

            streams[curId][neighbourId].outputStream = fds[1];
            streams[neighbourId][curId].inputStream = fds[0];
        }
    }
    process_info.id = 0;
    process_info.count = x;
    close_fds(0, x);

    pid_t pids[MAX_PROCESS_ID + 1];

    for (int curId = 1; curId <= x; curId++) {
        const pid_t childPid = fork();
        if (childPid == -1) {
            printf("error while forking\n");
            return 1;
        }
        if (childPid == 0) {
            process_info = (ProcessInfo){
                .id = curId,
                .count = x,
                .pid = getpid(),
                .ppid = getppid()
            };
            return child();
        }
        pids[curId - 1] = childPid;
    }

    Message startMessage;
    Message doneMessage;

    receive_multicast(0, &startMessage);
    receive_multicast(0, &doneMessage);

    for (int curId = 0; curId <= x; curId++) {
        waitpid(pids[curId], NULL, 0);
    }

    return 0;
}
