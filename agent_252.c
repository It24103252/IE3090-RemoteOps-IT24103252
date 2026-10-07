#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>


/* =========================================================
   Personalized RemoteOps values
   ========================================================= */

#define PORT 9410
#define BACKLOG 10

#define AUTH_TOKEN "OPS-3252"
#define SID "2523"

#define BUFFER_SIZE 1024

#define MAX_FILE_SIZE (10 * 1024 * 1024)

#define STORAGE_DIR "./agentfiles/IT24103252"

#define LOG_FILE "remoteops_IT24103252.log"

#define MONITOR_INTERVAL 5


/* =========================================================
   System statistics structure
   ========================================================= */

typedef struct
{
    double cpu_load;
    unsigned long mem_used_mb;
    unsigned long uptime_sec;

} SystemStats;


/* =========================================================
   Part 11 - Logging
   ========================================================= */

void write_log(const char *message)
{
    FILE *log_file;

    time_t current_time;

    struct tm time_info;

    char timestamp[64];


    current_time = time(NULL);


    if (localtime_r(&current_time,
                    &time_info) == NULL)
    {
        return;
    }


    if (strftime(timestamp,
                 sizeof(timestamp),
                 "%Y-%m-%d %H:%M:%S",
                 &time_info) == 0)
    {
        return;
    }


    log_file = fopen(LOG_FILE, "a");


    if (log_file == NULL)
    {
        perror("log fopen");

        return;
    }


    fprintf(log_file,
            "[%s] %s\n",
            timestamp,
            message);


    fclose(log_file);
}


/* =========================================================
   Receive one newline-terminated protocol line
   ========================================================= */

ssize_t recv_line(int sockfd,
                  char *buffer,
                  size_t size)
{
    size_t total = 0;


    if (size == 0)
    {
        return -1;
    }


    while (total < size - 1)
    {
        char ch;


        ssize_t n =
            recv(sockfd,
                 &ch,
                 1,
                 0);


        if (n == 0)
        {
            if (total == 0)
            {
                return 0;
            }

            break;
        }


        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }


        if (ch == '\n')
        {
            break;
        }


        if (ch != '\r')
        {
            buffer[total++] = ch;
        }
    }


    buffer[total] = '\0';


    return (ssize_t)total;
}


/* =========================================================
   Send all bytes
   ========================================================= */

int send_all(int sockfd,
             const char *data,
             size_t length)
{
    size_t total_sent = 0;


    while (total_sent < length)
    {
        ssize_t n =
            send(sockfd,
                 data + total_sent,
                 length - total_sent,
                 0);


        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }


        if (n == 0)
        {
            return -1;
        }


        total_sent += (size_t)n;
    }


    return 0;
}


/* =========================================================
   PUT - Receive exact raw bytes
   ========================================================= */

int receive_file_bytes(int sockfd,
                       FILE *file,
                       size_t filesize)
{
    char buffer[4096];

    size_t total_received = 0;


    while (total_received < filesize)
    {
        size_t remaining =
            filesize - total_received;


        size_t chunk_size =
            remaining < sizeof(buffer)
            ? remaining
            : sizeof(buffer);


        ssize_t n =
            recv(sockfd,
                 buffer,
                 chunk_size,
                 0);


        if (n == 0)
        {
            return -1;
        }


        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }


        size_t written =
            fwrite(buffer,
                   1,
                   (size_t)n,
                   file);


        if (written != (size_t)n)
        {
            return -1;
        }


        total_received += (size_t)n;
    }


    return 0;
}


/* =========================================================
   GET - Send exact raw bytes
   ========================================================= */

int send_file_bytes(int sockfd,
                    FILE *file,
                    size_t filesize)
{
    char buffer[4096];

    size_t total_sent = 0;


    while (total_sent < filesize)
    {
        size_t remaining =
            filesize - total_sent;


        size_t chunk_size =
            remaining < sizeof(buffer)
            ? remaining
            : sizeof(buffer);


        size_t bytes_read =
            fread(buffer,
                  1,
                  chunk_size,
                  file);


        if (bytes_read == 0)
        {
            return -1;
        }


        if (send_all(sockfd,
                     buffer,
                     bytes_read) < 0)
        {
            return -1;
        }


        total_sent += bytes_read;
    }


    return 0;
}


/* =========================================================
   Validate filename
   ========================================================= */

int valid_filename(const char *filename)
{
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }


    if (strstr(filename, "..") != NULL)
    {
        return 0;
    }


    if (strchr(filename, '/') != NULL)
    {
        return 0;
    }


    if (strchr(filename, '\\') != NULL)
    {
        return 0;
    }


    return 1;
}


/* =========================================================
   PUT handler
   ========================================================= */

void handle_put(int client_fd,
                const char *filename,
                size_t filesize)
{
    char filepath[512];

    char response[BUFFER_SIZE];


    if (!valid_filename(filename))
    {
        const char *error_response =
            "ERR INVALID_FILENAME SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    if (filesize > MAX_FILE_SIZE)
    {
        const char *error_response =
            "ERR 004 FILE_TOO_LARGE SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    int path_length =
        snprintf(filepath,
                 sizeof(filepath),
                 "%s/%s",
                 STORAGE_DIR,
                 filename);


    if (path_length < 0 ||
        (size_t)path_length >= sizeof(filepath))
    {
        const char *error_response =
            "ERR INVALID_FILENAME SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    FILE *file = fopen(filepath, "wb");


    if (file == NULL)
    {
        perror("fopen");


        const char *error_response =
            "ERR FILE_WRITE_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    printf("[Child %d] Receiving file %s (%zu bytes)\n",
           getpid(),
           filename,
           filesize);


    if (receive_file_bytes(client_fd,
                           file,
                           filesize) < 0)
    {
        fclose(file);

        remove(filepath);


        const char *error_response =
            "ERR FILE_RECEIVE_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    if (fclose(file) != 0)
    {
        remove(filepath);


        const char *error_response =
            "ERR FILE_WRITE_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    snprintf(response,
             sizeof(response),
             "OK FILE_RECEIVED %s SID:%s\n",
             filename,
             SID);


    send_all(client_fd,
             response,
             strlen(response));


    printf("[Child %d] File received successfully: %s (%zu bytes)\n",
           getpid(),
           filename,
           filesize);
}


/* =========================================================
   GET handler
   ========================================================= */

void handle_get(int client_fd,
                const char *filename)
{
    char filepath[512];

    char response[BUFFER_SIZE];

    struct stat file_info;


    if (!valid_filename(filename))
    {
        const char *error_response =
            "ERR INVALID_FILENAME SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    int path_length =
        snprintf(filepath,
                 sizeof(filepath),
                 "%s/%s",
                 STORAGE_DIR,
                 filename);


    if (path_length < 0 ||
        (size_t)path_length >= sizeof(filepath))
    {
        const char *error_response =
            "ERR INVALID_FILENAME SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    if (stat(filepath,
             &file_info) < 0)
    {
        const char *error_response =
            "ERR 005 FILE_NOT_FOUND SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    if (!S_ISREG(file_info.st_mode))
    {
        const char *error_response =
            "ERR 005 FILE_NOT_FOUND SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    if (file_info.st_size < 0)
    {
        const char *error_response =
            "ERR FILE_READ_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    size_t filesize =
        (size_t)file_info.st_size;


    FILE *file = fopen(filepath, "rb");


    if (file == NULL)
    {
        const char *error_response =
            "ERR FILE_READ_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    int response_length =
        snprintf(response,
                 sizeof(response),
                 "OK FILE_SEND %s %zu SID:%s\n",
                 filename,
                 filesize,
                 SID);


    if (response_length < 0 ||
        (size_t)response_length >= sizeof(response))
    {
        fclose(file);

        return;
    }


    if (send_all(client_fd,
                 response,
                 (size_t)response_length) < 0)
    {
        fclose(file);

        return;
    }


    printf("[Child %d] Sending file %s (%zu bytes)\n",
           getpid(),
           filename,
           filesize);


    if (send_file_bytes(client_fd,
                        file,
                        filesize) < 0)
    {
        fclose(file);


        printf("[Child %d] Failed to send file: %s\n",
               getpid(),
               filename);


        return;
    }


    fclose(file);


    printf("[Child %d] File sent successfully: %s (%zu bytes)\n",
           getpid(),
           filename,
           filesize);
}


/* =========================================================
   Read Linux system statistics
   ========================================================= */

int get_system_stats(SystemStats *stats)
{
    FILE *file;

    double load = 0.0;

    double uptime = 0.0;

    unsigned long mem_total_kb = 0;

    unsigned long mem_available_kb = 0;

    char line[256];


    if (stats == NULL)
    {
        return -1;
    }


    file = fopen("/proc/loadavg", "r");


    if (file == NULL)
    {
        return -1;
    }


    if (fscanf(file,
               "%lf",
               &load) != 1)
    {
        fclose(file);

        return -1;
    }


    fclose(file);


    file = fopen("/proc/meminfo", "r");


    if (file == NULL)
    {
        return -1;
    }


    while (fgets(line,
                 sizeof(line),
                 file) != NULL)
    {
        if (sscanf(line,
                   "MemTotal: %lu kB",
                   &mem_total_kb) == 1)
        {
            continue;
        }


        if (sscanf(line,
                   "MemAvailable: %lu kB",
                   &mem_available_kb) == 1)
        {
            continue;
        }
    }


    fclose(file);


    if (mem_total_kb == 0 ||
        mem_available_kb > mem_total_kb)
    {
        return -1;
    }


    file = fopen("/proc/uptime", "r");


    if (file == NULL)
    {
        return -1;
    }


    if (fscanf(file,
               "%lf",
               &uptime) != 1)
    {
        fclose(file);

        return -1;
    }


    fclose(file);


    stats->cpu_load = load;


    stats->mem_used_mb =
        (mem_total_kb -
         mem_available_kb) / 1024;


    stats->uptime_sec =
        (unsigned long)uptime;


    return 0;
}


/* =========================================================
   SYSINFO
   ========================================================= */

void handle_sysinfo(int client_fd)
{
    SystemStats stats;

    char response[BUFFER_SIZE];


    if (get_system_stats(&stats) < 0)
    {
        const char *error_response =
            "ERR SYSINFO_FAILED SID:" SID "\n";


        send_all(client_fd,
                 error_response,
                 strlen(error_response));


        return;
    }


    int response_length =
        snprintf(response,
                 sizeof(response),
                 "OK SYSINFO %.2f %lu %lu SID:%s\n",
                 stats.cpu_load,
                 stats.mem_used_mb,
                 stats.uptime_sec,
                 SID);


    if (response_length < 0 ||
        (size_t)response_length >= sizeof(response))
    {
        return;
    }


    send_all(client_fd,
             response,
             (size_t)response_length);
}


/* =========================================================
   UDP periodic monitoring
   ========================================================= */

void run_udp_monitor(struct in_addr controller_ip,
                     int udp_port)
{
    int udp_fd;

    struct sockaddr_in udp_addr;

    char message[BUFFER_SIZE];


    udp_fd =
        socket(AF_INET,
               SOCK_DGRAM,
               0);


    if (udp_fd < 0)
    {
        perror("UDP socket");

        exit(EXIT_FAILURE);
    }


    memset(&udp_addr,
           0,
           sizeof(udp_addr));


    udp_addr.sin_family =
        AF_INET;


    udp_addr.sin_addr =
        controller_ip;


    udp_addr.sin_port =
        htons((unsigned short)udp_port);


    printf("[Monitor %d] UDP monitoring started to %s:%d\n",
           getpid(),
           inet_ntoa(controller_ip),
           udp_port);


    while (1)
    {
        SystemStats stats;


        if (get_system_stats(&stats) == 0)
        {
            int message_length =
                snprintf(message,
                         sizeof(message),
                         "SYSINFO %.2f %lu %lu SID:%s",
                         stats.cpu_load,
                         stats.mem_used_mb,
                         stats.uptime_sec,
                         SID);


            if (message_length > 0 &&
                (size_t)message_length < sizeof(message))
            {
                if (sendto(udp_fd,
                           message,
                           (size_t)message_length,
                           0,
                           (struct sockaddr *)&udp_addr,
                           sizeof(udp_addr)) < 0)
                {
                    perror("UDP sendto");
                }
            }
        }


        sleep(MONITOR_INTERVAL);
    }
}


/* =========================================================
   LISTPROC
   ========================================================= */

void handle_listproc(int client_fd)
{
    FILE *fp;
    char line[256];
    char process_list[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    size_t used = 0;

    process_list[0] = '\0';

    fp = popen("ps -eo pid=,comm=", "r");

    if (fp == NULL)
    {
        const char *error_response =
            "ERR LISTPROC_FAILED SID:" SID "\n";

        send_all(client_fd,
                 error_response,
                 strlen(error_response));
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
        {
            continue;
        }

        size_t line_length = strlen(line);

        if (used + line_length + 2 >= sizeof(process_list) - 32)
        {
            break;
        }

        if (used > 0)
        {
            process_list[used++] = ',';
            process_list[used++] = ' ';
            process_list[used] = '\0';
        }

        memcpy(process_list + used, line, line_length + 1);
        used += line_length;
    }

    pclose(fp);

    int response_length =
        snprintf(response,
                 sizeof(response),
                 "OK PROCS %s SID:%s\n",
                 process_list,
                 SID);

    if (response_length < 0 ||
        (size_t)response_length >= sizeof(response))
    {
        const char *error_response =
            "ERR LISTPROC_FAILED SID:" SID "\n";

        send_all(client_fd,
                 error_response,
                 strlen(error_response));
        return;
    }

    send_all(client_fd,
             response,
             (size_t)response_length);
}


/* =========================================================
   Restricted EXEC
   ========================================================= */

void handle_exec(int client_fd,
                 const char *exec_name)
{
    const char *system_command = NULL;

    if (strcmp(exec_name, "DATE") == 0)
        system_command = "date";
    else if (strcmp(exec_name, "UPTIME") == 0)
        system_command = "uptime";
    else if (strcmp(exec_name, "DISKFREE") == 0)
        system_command = "df -h";
    else if (strcmp(exec_name, "HOSTNAME") == 0)
        system_command = "hostname";
    else if (strcmp(exec_name, "WHOAMI") == 0)
        system_command = "whoami";
    else
    {
        const char *response =
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SID "\n";

        send_all(client_fd, response, strlen(response));
        return;
    }

    FILE *fp = popen(system_command, "r");

    if (fp == NULL)
    {
        const char *response =
            "ERR EXEC_FAILED SID:" SID "\n";

        send_all(client_fd, response, strlen(response));
        return;
    }

    char line[256];
    char output[BUFFER_SIZE];
    size_t used = 0;

    output[0] = '\0';

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0')
            continue;

        size_t line_length = strlen(line);

        if (used + line_length + 3 >= sizeof(output) - 32)
            break;

        if (used > 0)
        {
            output[used++] = ' ';
            output[used++] = '|';
            output[used++] = ' ';
            output[used] = '\0';
        }

        memcpy(output + used, line, line_length + 1);
        used += line_length;
    }

    pclose(fp);

    char response[BUFFER_SIZE];

    int response_length =
        snprintf(response,
                 sizeof(response),
                 "OK EXEC_RESULT %s SID:%s\n",
                 output,
                 SID);

    if (response_length < 0 ||
        (size_t)response_length >= sizeof(response))
    {
        const char *error_response =
            "ERR EXEC_FAILED SID:" SID "\n";

        send_all(client_fd,
                 error_response,
                 strlen(error_response));
        return;
    }

    send_all(client_fd,
             response,
             (size_t)response_length);
}


/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    int server_fd;

    int client_fd;

    int opt = 1;


    struct sockaddr_in server_addr;

    struct sockaddr_in client_addr;


    socklen_t client_len;


    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (server_fd < 0)
    {
        perror("socket");

        return EXIT_FAILURE;
    }


    printf("TCP socket created successfully.\n");


    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        return EXIT_FAILURE;
    }


    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;


    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);


    server_addr.sin_port =
        htons(PORT);


    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        return EXIT_FAILURE;
    }


    printf("Agent bound to TCP port %d.\n",
           PORT);


    if (listen(server_fd,
               BACKLOG) < 0)
    {
        perror("listen");

        close(server_fd);

        return EXIT_FAILURE;
    }


    printf("RemoteOps Agent listening on port %d...\n",
           PORT);


    signal(SIGCHLD, SIG_IGN);


    while (1)
    {
        client_len =
            sizeof(client_addr);


        client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);


        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }


            perror("accept");

            continue;
        }


        printf("Controller connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));


        pid_t pid = fork();


        if (pid < 0)
        {
            perror("fork");

            close(client_fd);

            continue;
        }


        /* =================================================
           Controller-session child
           ================================================= */

        if (pid == 0)
        {
            char buffer[BUFFER_SIZE];

            ssize_t bytes_received;

            int authenticated = 0;

            pid_t monitor_pid = -1;


            /*
             * Main Agent ignores SIGCHLD.
             * Session child restores default behaviour
             * because it must wait for its UDP monitor child.
             */
            signal(SIGCHLD, SIG_DFL);


            close(server_fd);


            printf("[Child %d] Handling Controller %s:%d\n",
                   getpid(),
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));


            /* =================================================
               AUTH
               ================================================= */

            bytes_received =
                recv_line(client_fd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received <= 0)
            {
                close(client_fd);

                exit(EXIT_SUCCESS);
            }


            printf("[Child %d] Received: %s\n",
                   getpid(),
                   buffer);


            if (strcmp(buffer,
                       "AUTH " AUTH_TOKEN) == 0)
            {
                const char *response =
                    "OK AUTHENTICATED SID:" SID "\n";


                if (send_all(client_fd,
                             response,
                             strlen(response)) < 0)
                {
                    close(client_fd);

                    exit(EXIT_FAILURE);
                }


                authenticated = 1;


                printf("[Child %d] Authentication successful.\n",
                       getpid());


                /*
                 * Part 11 Change 1:
                 * Log successful authentication.
                 */
                char auth_log[256];


                snprintf(auth_log,
                         sizeof(auth_log),
                         "Authentication successful: %s",
                         inet_ntoa(client_addr.sin_addr));


                write_log(auth_log);
            }

            else
            {
                const char *response =
                    "ERR 001 AUTH_FAILED SID:" SID "\n";


                send_all(client_fd,
                         response,
                         strlen(response));


                /*
                 * Log failed authentication.
                 */
                char auth_fail_log[256];


                snprintf(auth_fail_log,
                         sizeof(auth_fail_log),
                         "Authentication failed: %s",
                         inet_ntoa(client_addr.sin_addr));


                write_log(auth_fail_log);


                close(client_fd);

                exit(EXIT_SUCCESS);
            }


            /* =================================================
               Authenticated command loop
               ================================================= */

            while (authenticated)
            {
                bytes_received =
                    recv_line(client_fd,
                              buffer,
                              sizeof(buffer));


                if (bytes_received == 0)
                {
                    printf("[Child %d] Controller disconnected.\n",
                           getpid());


                    char disconnect_log[256];


                    snprintf(disconnect_log,
                             sizeof(disconnect_log),
                             "Controller disconnected unexpectedly: %s",
                             inet_ntoa(client_addr.sin_addr));


                    write_log(disconnect_log);


                    break;
                }


                if (bytes_received < 0)
                {
                    perror("recv");


                    char recv_error_log[256];


                    snprintf(recv_error_log,
                             sizeof(recv_error_log),
                             "Controller receive error: %s",
                             inet_ntoa(client_addr.sin_addr));


                    write_log(recv_error_log);


                    break;
                }


                printf("[Child %d] Command: %s\n",
                       getpid(),
                       buffer);


                /*
                 * Part 11 Change 2:
                 * Log every authenticated command.
                 */
                char log_message[BUFFER_SIZE + 32];


                snprintf(log_message,
                         sizeof(log_message),
                         "Command from %s: %s",
                         inet_ntoa(client_addr.sin_addr),
                         buffer);


                write_log(log_message);


                /* =================================================
                   SYSINFO
                   ================================================= */

                if (strcmp(buffer,
                           "SYSINFO") == 0)
                {
                    handle_sysinfo(client_fd);
                }


                /* =================================================
                   LISTPROC
                   ================================================= */

                else if (strcmp(buffer,
                                "LISTPROC") == 0)
                {
                    handle_listproc(client_fd);
                }


                /* =================================================
                   EXEC
                   ================================================= */

                else if (strncmp(buffer,
                                 "EXEC ",
                                 5) == 0)
                {
                    handle_exec(client_fd,
                                buffer + 5);
                }


                /* =================================================
                   PUT
                   ================================================= */

                else if (strncmp(buffer,
                                 "PUT ",
                                 4) == 0)
                {
                    char filename[256];

                    unsigned long long file_size_value;

                    char extra;


                    int parsed =
                        sscanf(buffer + 4,
                               "%255s %llu %c",
                               filename,
                               &file_size_value,
                               &extra);


                    if (parsed != 2)
                    {
                        const char *response =
                            "ERR INVALID_PUT_FORMAT SID:" SID "\n";


                        send_all(client_fd,
                                 response,
                                 strlen(response));
                    }


                    else if (file_size_value >
                             (unsigned long long)MAX_FILE_SIZE)
                    {
                        const char *response =
                            "ERR 004 FILE_TOO_LARGE SID:" SID "\n";


                        send_all(client_fd,
                                 response,
                                 strlen(response));
                    }


                    else
                    {
                        handle_put(client_fd,
                                   filename,
                                   (size_t)file_size_value);
                    }
                }


                /* =================================================
                   GET
                   ================================================= */

                else if (strncmp(buffer,
                                 "GET ",
                                 4) == 0)
                {
                    char filename[256];

                    char extra;


                    int parsed =
                        sscanf(buffer + 4,
                               "%255s %c",
                               filename,
                               &extra);


                    if (parsed != 1)
                    {
                        const char *response =
                            "ERR INVALID_GET_FORMAT SID:" SID "\n";


                        send_all(client_fd,
                                 response,
                                 strlen(response));
                    }


                    else
                    {
                        handle_get(client_fd,
                                   filename);
                    }
                }


                /* =================================================
                   MONITOR START <udp_port>
                   ================================================= */

                else if (strncmp(buffer,
                                 "MONITOR START ",
                                 14) == 0)
                {
                    int udp_port;

                    char extra;


                    int parsed =
                        sscanf(buffer + 14,
                               "%d %c",
                               &udp_port,
                               &extra);


                    if (parsed != 1 ||
                        udp_port < 1 ||
                        udp_port > 65535)
                    {
                        const char *response =
                            "ERR INVALID_UDP_PORT SID:" SID "\n";


                        send_all(client_fd,
                                 response,
                                 strlen(response));
                    }


                    else if (monitor_pid > 0)
                    {
                        const char *response =
                            "ERR MONITOR_ALREADY_RUNNING SID:" SID "\n";


                        send_all(client_fd,
                                 response,
                                 strlen(response));
                    }


                    else
                    {
                        monitor_pid = fork();


                        if (monitor_pid < 0)
                        {
                            perror("monitor fork");


                            const char *response =
                                "ERR MONITOR_START_FAILED SID:" SID "\n";


                            send_all(client_fd,
                                     response,
                                     strlen(response));


                            monitor_pid = -1;
                        }


                        else if (monitor_pid == 0)
                        {
                            close(client_fd);


                            run_udp_monitor(client_addr.sin_addr,
                                            udp_port);


                            exit(EXIT_SUCCESS);
                        }


                        else
                        {
                            const char *response =
                                "OK MONITOR_STARTED SID:" SID "\n";


                            if (send_all(client_fd,
                                         response,
                                         strlen(response)) < 0)
                            {
                                kill(monitor_pid,
                                     SIGTERM);


                                waitpid(monitor_pid,
                                        NULL,
                                        0);


                                monitor_pid = -1;


                                break;
                            }


                            printf("[Child %d] UDP monitoring started "
                                   "for Controller %s on UDP port %d "
                                   "(monitor PID %d).\n",
                                   getpid(),
                                   inet_ntoa(client_addr.sin_addr),
                                   udp_port,
                                   monitor_pid);
                        }
                    }
                }


                /* =================================================
                   MONITOR STOP
                   ================================================= */

                else if (strcmp(buffer,
                                "MONITOR STOP") == 0)
                {
                    if (monitor_pid > 0)
                    {
                        pid_t old_monitor_pid =
                            monitor_pid;


                        kill(monitor_pid,
                             SIGTERM);


                        waitpid(monitor_pid,
                                NULL,
                                0);


                        monitor_pid = -1;


                        printf("[Child %d] UDP monitor PID %d stopped.\n",
                               getpid(),
                               old_monitor_pid);
                    }


                    const char *response =
                        "OK MONITOR_STOPPED SID:" SID "\n";


                    if (send_all(client_fd,
                                 response,
                                 strlen(response)) < 0)
                    {
                        break;
                    }
                }


                /* =================================================
                   Part 11 Change 3:
                   Graceful QUIT
                   ================================================= */

                else if (strcmp(buffer,
                                "QUIT") == 0)
                {
                    /*
                     * Stop UDP monitor first if it is
                     * still running.
                     */
                    if (monitor_pid > 0)
                    {
                        pid_t old_monitor_pid =
                            monitor_pid;


                        kill(monitor_pid,
                             SIGTERM);


                        waitpid(monitor_pid,
                                NULL,
                                0);


                        monitor_pid = -1;


                        printf("[Child %d] UDP monitor PID %d stopped "
                               "during graceful QUIT.\n",
                               getpid(),
                               old_monitor_pid);
                    }


                    /*
                     * Send graceful disconnect response.
                     */
                    const char *response =
                        "OK BYE SID:" SID "\n";


                    send_all(client_fd,
                             response,
                             strlen(response));


                    /*
                     * Log graceful disconnect.
                     */
                    char exit_log[256];


                    snprintf(exit_log,
                             sizeof(exit_log),
                             "Graceful disconnect: %s",
                             inet_ntoa(client_addr.sin_addr));


                    write_log(exit_log);


                    printf("[Child %d] Graceful Controller disconnect.\n",
                           getpid());


                    authenticated = 0;


                    break;
                }


                /* =================================================
                   Unknown command
                   ================================================= */

                else
                {
                    const char *response =
                        "ERR UNKNOWN_COMMAND SID:" SID "\n";


                    if (send_all(client_fd,
                                 response,
                                 strlen(response)) < 0)
                    {
                        break;
                    }
                }
            }


            /* =================================================
               Cleanup if monitoring is still running
               ================================================= */

            if (monitor_pid > 0)
            {
                kill(monitor_pid,
                     SIGTERM);


                waitpid(monitor_pid,
                        NULL,
                        0);


                monitor_pid = -1;
            }


            close(client_fd);


            exit(EXIT_SUCCESS);
        }


        /* =================================================
           Main Agent parent
           ================================================= */

        else
        {
            close(client_fd);
        }
    }


    close(server_fd);


    return EXIT_SUCCESS;
}
