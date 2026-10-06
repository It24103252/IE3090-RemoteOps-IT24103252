#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
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

        ssize_t n = recv(sockfd,
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
        ssize_t n = send(sockfd,
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
   Receive exactly filesize raw bytes and write them to file
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

        ssize_t n = recv(sockfd,
                         buffer,
                         chunk_size,
                         0);

        /*
         * Connection closed before all expected
         * file bytes were received.
         */
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
   Validate uploaded filename
   ========================================================= */

int valid_filename(const char *filename)
{
    /*
     * Filename cannot be NULL or empty.
     */
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }

    /*
     * Do not allow ".." because it could be
     * used for directory traversal.
     */
    if (strstr(filename,
               "..") != NULL)
    {
        return 0;
    }

    /*
     * Controller should send only a filename,
     * not a Unix path.
     */
    if (strchr(filename,
               '/') != NULL)
    {
        return 0;
    }

    /*
     * Also reject Windows-style path separators.
     */
    if (strchr(filename,
               '\\') != NULL)
    {
        return 0;
    }

    return 1;
}


/* =========================================================
   Handle PUT upload
   ========================================================= */

void handle_put(int client_fd,
                const char *filename,
                size_t filesize)
{
    char filepath[512];

    char response[BUFFER_SIZE];


    /* -----------------------------------------------------
       Validate filename
       ----------------------------------------------------- */

    if (!valid_filename(filename))
    {
        const char *error_response =
            "ERR INVALID_FILENAME SID:" SID "\n";

        send_all(client_fd,
                 error_response,
                 strlen(error_response));

        return;
    }


    /* -----------------------------------------------------
       Check maximum file size
       ----------------------------------------------------- */

    if (filesize > MAX_FILE_SIZE)
    {
        const char *error_response =
            "ERR 004 FILE_TOO_LARGE SID:" SID "\n";

        send_all(client_fd,
                 error_response,
                 strlen(error_response));

        return;
    }


    /* -----------------------------------------------------
       Build personalized destination path

       Example:
       ./agentfiles/IT24103252/put_test.txt
       ----------------------------------------------------- */

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


    /* -----------------------------------------------------
       Open destination in binary-write mode
       ----------------------------------------------------- */

    FILE *file =
        fopen(filepath,
              "wb");


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


    /* -----------------------------------------------------
       Receive exactly <filesize> raw bytes
       ----------------------------------------------------- */

    if (receive_file_bytes(client_fd,
                           file,
                           filesize) < 0)
    {
        fclose(file);

        /*
         * Remove an incomplete file.
         */
        remove(filepath);


        printf("[Child %d] File transfer failed: %s\n",
               getpid(),
               filename);


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


    /* -----------------------------------------------------
       Successful PUT response
       ----------------------------------------------------- */

    snprintf(response,
             sizeof(response),
             "OK FILE_RECEIVED %s SID:%s\n",
             filename,
             SID);


    if (send_all(client_fd,
                 response,
                 strlen(response)) < 0)
    {
        perror("send");

        return;
    }


    printf("[Child %d] File received successfully: %s (%zu bytes)\n",
           getpid(),
           filename,
           filesize);
}


/* =========================================================
   SYSINFO
   ========================================================= */

void handle_sysinfo(int client_fd)
{
    FILE *fp;

    char line[BUFFER_SIZE];


    fp = popen("uname -a",
               "r");


    if (fp == NULL)
    {
        const char *response =
            "ERR SYSINFO_FAILED SID:" SID "\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return;
    }


    const char *start =
        "OK SYSINFO SID:" SID "\n";


    send_all(client_fd,
             start,
             strlen(start));


    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        send_all(client_fd,
                 line,
                 strlen(line));
    }


    pclose(fp);


    const char *end =
        "END SYSINFO SID:" SID "\n";


    send_all(client_fd,
             end,
             strlen(end));
}


/* =========================================================
   LISTPROC
   ========================================================= */

void handle_listproc(int client_fd)
{
    FILE *fp;

    char line[BUFFER_SIZE];


    fp = popen("ps -eo pid,comm",
               "r");


    if (fp == NULL)
    {
        const char *response =
            "ERR LISTPROC_FAILED SID:" SID "\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return;
    }


    const char *start =
        "OK LISTPROC SID:" SID "\n";


    send_all(client_fd,
             start,
             strlen(start));


    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        send_all(client_fd,
                 line,
                 strlen(line));
    }


    pclose(fp);


    const char *end =
        "END LISTPROC SID:" SID "\n";


    send_all(client_fd,
             end,
             strlen(end));
}


/* =========================================================
   EXEC whitelist
   ========================================================= */

void handle_exec(int client_fd,
                 const char *exec_name)
{
    const char *system_command = NULL;


    if (strcmp(exec_name,
               "DATE") == 0)
    {
        system_command = "date";
    }

    else if (strcmp(exec_name,
                    "UPTIME") == 0)
    {
        system_command = "uptime";
    }

    else if (strcmp(exec_name,
                    "DISKFREE") == 0)
    {
        system_command = "df -h";
    }

    else if (strcmp(exec_name,
                    "HOSTNAME") == 0)
    {
        system_command = "hostname";
    }

    else if (strcmp(exec_name,
                    "WHOAMI") == 0)
    {
        system_command = "whoami";
    }

    else
    {
        const char *response =
            "ERR EXEC_NOT_ALLOWED SID:" SID "\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return;
    }


    FILE *fp =
        popen(system_command,
              "r");


    if (fp == NULL)
    {
        const char *response =
            "ERR EXEC_FAILED SID:" SID "\n";

        send_all(client_fd,
                 response,
                 strlen(response));

        return;
    }


    char response[BUFFER_SIZE];


    snprintf(response,
             sizeof(response),
             "OK EXEC %s SID:%s\n",
             exec_name,
             SID);


    send_all(client_fd,
             response,
             strlen(response));


    char line[BUFFER_SIZE];


    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        send_all(client_fd,
                 line,
                 strlen(line));
    }


    pclose(fp);


    snprintf(response,
             sizeof(response),
             "END EXEC %s SID:%s\n",
             exec_name,
             SID);


    send_all(client_fd,
             response,
             strlen(response));
}


/* =========================================================
   Main Agent
   ========================================================= */

int main(void)
{
    int server_fd;

    int client_fd;

    int opt = 1;


    struct sockaddr_in server_addr;

    struct sockaddr_in client_addr;


    socklen_t client_len;


    /* =====================================================
       STEP 1: Create TCP socket
       ===================================================== */

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


    /* =====================================================
       STEP 2: Allow address reuse
       ===================================================== */

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


    /* =====================================================
       STEP 3: Prepare server address
       ===================================================== */

    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;


    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);


    server_addr.sin_port =
        htons(PORT);


    /* =====================================================
       STEP 4: Bind
       ===================================================== */

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


    /* =====================================================
       STEP 5: Listen
       ===================================================== */

    if (listen(server_fd,
               BACKLOG) < 0)
    {
        perror("listen");

        close(server_fd);

        return EXIT_FAILURE;
    }


    printf("RemoteOps Agent listening on port %d...\n",
           PORT);


    /*
     * Prevent zombie child processes.
     */
    signal(SIGCHLD,
           SIG_IGN);


    /* =====================================================
       STEP 6: Accept Controllers
       ===================================================== */

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


        /* =================================================
           STEP 7: Fork
           ================================================= */

        pid_t pid =
            fork();


        if (pid < 0)
        {
            perror("fork");

            close(client_fd);

            continue;
        }


        if (pid == 0)
        {
            /* =============================================
               CHILD PROCESS
               ============================================= */

            char buffer[BUFFER_SIZE];

            ssize_t bytes_received;

            int authenticated = 0;


            close(server_fd);


            printf("[Child %d] Handling Controller %s:%d\n",
                   getpid(),
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));


            /* =============================================
               STEP 8: AUTH
               ============================================= */

            bytes_received =
                recv_line(client_fd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received <= 0)
            {
                printf("[Child %d] Controller disconnected before AUTH.\n",
                       getpid());

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
                    perror("send");

                    close(client_fd);

                    exit(EXIT_FAILURE);
                }


                authenticated = 1;


                printf("[Child %d] Authentication successful.\n",
                       getpid());
            }

            else
            {
                const char *response =
                    "ERR 001 AUTH_FAILED SID:" SID "\n";


                send_all(client_fd,
                         response,
                         strlen(response));


                printf("[Child %d] Authentication failed.\n",
                       getpid());


                close(client_fd);

                exit(EXIT_SUCCESS);
            }


            /* =============================================
               STEP 9: Authenticated command loop
               ============================================= */

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

                    break;
                }


                if (bytes_received < 0)
                {
                    perror("recv");

                    break;
                }


                printf("[Child %d] Command: %s\n",
                       getpid(),
                       buffer);


                /* =========================================
                   SYSINFO
                   ========================================= */

                if (strcmp(buffer,
                           "SYSINFO") == 0)
                {
                    handle_sysinfo(client_fd);
                }


                /* =========================================
                   LISTPROC
                   ========================================= */

                else if (strcmp(buffer,
                                "LISTPROC") == 0)
                {
                    handle_listproc(client_fd);
                }


                /* =========================================
                   EXEC
                   ========================================= */

                else if (strncmp(buffer,
                                 "EXEC ",
                                 5) == 0)
                {
                    handle_exec(client_fd,
                                buffer + 5);
                }


                /* =========================================
                   PUT

                   Expected command:
                   PUT <filename> <filesize>
                   ========================================= */

                else if (strncmp(buffer,
                                 "PUT ",
                                 4) == 0)
                {
                    char filename[256];

                    unsigned long long file_size_value;

                    char extra;


                    /*
                     * Require exactly:
                     *
                     * filename
                     * filesize
                     *
                     * The %c detects unexpected extra text.
                     */
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


                /* =========================================
                   Unknown command
                   ========================================= */

                else
                {
                    const char *response =
                        "ERR UNKNOWN_COMMAND SID:" SID "\n";


                    if (send_all(client_fd,
                                 response,
                                 strlen(response)) < 0)
                    {
                        perror("send");

                        break;
                    }
                }
            }


            close(client_fd);

            exit(EXIT_SUCCESS);
        }

        else
        {
            /* =============================================
               PARENT PROCESS
               ============================================= */

            close(client_fd);
        }
    }


    close(server_fd);

    return EXIT_SUCCESS;
}
