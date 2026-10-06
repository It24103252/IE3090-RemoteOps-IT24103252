#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

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

#define AUTH_TOKEN "OPS-3252"

#define SID "2523"

#define BUFFER_SIZE 1024

#define MAX_FILE_SIZE (10 * 1024 * 1024)

#define DOWNLOAD_DIR "./downloads"


/* =========================================================
   Receive one newline-terminated TCP line
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
   Send exact raw file bytes for PUT
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
   Receive exact raw file bytes for GET
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
   Receive multi-line command response
   ========================================================= */

int receive_command_response(int sockfd,
                             const char *end_marker)
{
    char buffer[BUFFER_SIZE];


    while (1)
    {
        ssize_t n =
            recv_line(sockfd,
                      buffer,
                      sizeof(buffer));


        if (n == 0)
        {
            printf("Agent closed the connection.\n");

            return -1;
        }


        if (n < 0)
        {
            perror("recv");

            return -1;
        }


        printf("%s\n",
               buffer);


        if (strcmp(buffer,
                   end_marker) == 0)
        {
            break;
        }


        /*
         * Error responses are single-line responses,
         * so do not continue waiting for END marker.
         */
        if (strncmp(buffer,
                    "ERR ",
                    4) == 0)
        {
            break;
        }
    }


    return 0;
}


/* =========================================================
   PUT upload
   ========================================================= */

void upload_file(int sockfd,
                 const char *filename)
{
    FILE *file;

    struct stat file_info;

    char command[BUFFER_SIZE];

    char response[BUFFER_SIZE];


    if (stat(filename,
             &file_info) < 0)
    {
        printf("Local file not found: %s\n",
               filename);

        return;
    }


    if (!S_ISREG(file_info.st_mode))
    {
        printf("Not a regular file: %s\n",
               filename);

        return;
    }


    if (file_info.st_size < 0)
    {
        printf("Unable to determine file size.\n");

        return;
    }


    size_t filesize =
        (size_t)file_info.st_size;


    if (filesize > MAX_FILE_SIZE)
    {
        printf("File is too large. Maximum size is 10 MB.\n");

        return;
    }


    file =
        fopen(filename,
              "rb");


    if (file == NULL)
    {
        perror("fopen");

        return;
    }


    /*
     * Only send the base filename.
     * Do not send a local path to the Agent.
     */
    const char *base_filename =
        strrchr(filename,
                '/');


    if (base_filename != NULL)
    {
        base_filename++;
    }

    else
    {
        base_filename =
            filename;
    }


    int command_length =
        snprintf(command,
                 sizeof(command),
                 "PUT %s %zu\n",
                 base_filename,
                 filesize);


    if (command_length < 0 ||
        (size_t)command_length >= sizeof(command))
    {
        printf("PUT command is too long.\n");

        fclose(file);

        return;
    }


    if (send_all(sockfd,
                 command,
                 (size_t)command_length) < 0)
    {
        perror("send");

        fclose(file);

        return;
    }


    printf("Uploading %s (%zu bytes)...\n",
           base_filename,
           filesize);


    if (send_file_bytes(sockfd,
                        file,
                        filesize) < 0)
    {
        printf("File upload failed.\n");

        fclose(file);

        return;
    }


    fclose(file);


    ssize_t n =
        recv_line(sockfd,
                  response,
                  sizeof(response));


    if (n == 0)
    {
        printf("Agent closed the connection.\n");

        return;
    }


    if (n < 0)
    {
        perror("recv");

        return;
    }


    printf("%s\n",
           response);


    if (strncmp(response,
                "OK FILE_RECEIVED ",
                17) == 0)
    {
        printf("Upload completed successfully.\n");
    }
}


/* =========================================================
   GET download
   ========================================================= */

void download_file(int sockfd,
                   const char *filename)
{
    char command[BUFFER_SIZE];

    char response[BUFFER_SIZE];

    char download_path[512];

    unsigned long long file_size_value;

    char received_sid[64];


    int command_length =
        snprintf(command,
                 sizeof(command),
                 "GET %s\n",
                 filename);


    if (command_length < 0 ||
        (size_t)command_length >= sizeof(command))
    {
        printf("GET command is too long.\n");

        return;
    }


    if (send_all(sockfd,
                 command,
                 (size_t)command_length) < 0)
    {
        perror("send");

        return;
    }


    ssize_t n =
        recv_line(sockfd,
                  response,
                  sizeof(response));


    if (n == 0)
    {
        printf("Agent closed the connection.\n");

        return;
    }


    if (n < 0)
    {
        perror("recv");

        return;
    }


    printf("%s\n",
           response);


    if (strncmp(response,
                "ERR ",
                4) == 0)
    {
        return;
    }


    int parsed =
        sscanf(response,
               "OK FILE %llu SID:%63s",
               &file_size_value,
               received_sid);


    if (parsed != 2)
    {
        printf("Invalid GET response from Agent.\n");

        return;
    }


    if (strcmp(received_sid,
               SID) != 0)
    {
        printf("SID mismatch in GET response.\n");

        return;
    }


    if (file_size_value >
        (unsigned long long)MAX_FILE_SIZE)
    {
        printf("Agent file is larger than allowed maximum.\n");

        return;
    }


    size_t filesize =
        (size_t)file_size_value;


    /*
     * Create downloads directory if necessary.
     */
    if (mkdir(DOWNLOAD_DIR,
              0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir downloads");

        return;
    }


    int path_length =
        snprintf(download_path,
                 sizeof(download_path),
                 "%s/%s",
                 DOWNLOAD_DIR,
                 filename);


    if (path_length < 0 ||
        (size_t)path_length >= sizeof(download_path))
    {
        printf("Download path is too long.\n");

        return;
    }


    FILE *file =
        fopen(download_path,
              "wb");


    if (file == NULL)
    {
        perror("fopen");

        return;
    }


    printf("Downloading %s (%zu bytes)...\n",
           filename,
           filesize);


    if (receive_file_bytes(sockfd,
                           file,
                           filesize) < 0)
    {
        printf("Download failed.\n");

        fclose(file);

        remove(download_path);

        return;
    }


    if (fclose(file) != 0)
    {
        printf("Failed to close downloaded file correctly.\n");

        remove(download_path);

        return;
    }


    printf("Download completed successfully.\n");

    printf("Saved as: %s\n",
           download_path);
}


/* =========================================================
   UDP listener child
   ========================================================= */

void run_udp_listener(int udp_fd)
{
    char buffer[BUFFER_SIZE];


    while (1)
    {
        struct sockaddr_in sender_addr;

        socklen_t sender_len =
            sizeof(sender_addr);


        ssize_t n =
            recvfrom(udp_fd,
                     buffer,
                     sizeof(buffer) - 1,
                     0,
                     (struct sockaddr *)&sender_addr,
                     &sender_len);


        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }


            perror("UDP recvfrom");

            break;
        }


        buffer[n] = '\0';


        printf("\n[UDP MONITOR] %s\n",
               buffer);


        printf("RemoteOps> ");


        fflush(stdout);
    }


    close(udp_fd);


    exit(EXIT_SUCCESS);
}


/* =========================================================
   MAIN
   ========================================================= */

int main(int argc,
         char *argv[])
{
    int sockfd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];

    ssize_t bytes_received;

    int udp_fd = -1;

    pid_t udp_listener_pid = -1;


    /* =====================================================
       Check Agent IP argument
       ===================================================== */

    if (argc != 2)
    {
        fprintf(stderr,
                "Usage: %s <agent_ip>\n",
                argv[0]);


        return EXIT_FAILURE;
    }


    /* =====================================================
       Create TCP socket
       ===================================================== */

    sockfd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (sockfd < 0)
    {
        perror("socket");

        return EXIT_FAILURE;
    }


    printf("TCP socket created successfully.\n");


    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;


    server_addr.sin_port =
        htons(PORT);


    if (inet_pton(AF_INET,
                  argv[1],
                  &server_addr.sin_addr) <= 0)
    {
        fprintf(stderr,
                "Invalid Agent IP address: %s\n",
                argv[1]);


        close(sockfd);


        return EXIT_FAILURE;
    }


    /* =====================================================
       Connect to Agent
       ===================================================== */

    printf("Connecting to RemoteOps Agent %s:%d...\n",
           argv[1],
           PORT);


    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");

        close(sockfd);


        return EXIT_FAILURE;
    }


    printf("Connected to RemoteOps Agent.\n");


    /* =====================================================
       AUTH
       ===================================================== */

    char auth_command[BUFFER_SIZE];


    int auth_length =
        snprintf(auth_command,
                 sizeof(auth_command),
                 "AUTH %s\n",
                 AUTH_TOKEN);


    if (auth_length < 0 ||
        (size_t)auth_length >= sizeof(auth_command))
    {
        close(sockfd);

        return EXIT_FAILURE;
    }


    if (send_all(sockfd,
                 auth_command,
                 (size_t)auth_length) < 0)
    {
        perror("send AUTH");

        close(sockfd);


        return EXIT_FAILURE;
    }


    bytes_received =
        recv_line(sockfd,
                  buffer,
                  sizeof(buffer));


    if (bytes_received == 0)
    {
        printf("Agent closed connection during authentication.\n");

        close(sockfd);


        return EXIT_FAILURE;
    }


    if (bytes_received < 0)
    {
        perror("recv AUTH");

        close(sockfd);


        return EXIT_FAILURE;
    }


    printf("%s\n",
           buffer);


    if (strcmp(buffer,
               "OK AUTHENTICATED SID:" SID) != 0)
    {
        printf("Authentication failed.\n");

        close(sockfd);


        return EXIT_FAILURE;
    }


    printf("Authentication successful.\n");


    /* =====================================================
       Main Controller command loop
       ===================================================== */

    while (1)
    {
        printf("RemoteOps> ");

        fflush(stdout);


        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL)
        {
            printf("\nInput closed.\n");

            break;
        }


        /*
         * Remove newline entered by the user.
         */
        buffer[strcspn(buffer,
                       "\r\n")] = '\0';


        if (buffer[0] == '\0')
        {
            continue;
        }


        /* =================================================
           SYSINFO
           ================================================= */

        if (strcmp(buffer,
                   "SYSINFO") == 0)
        {
            const char *command =
                "SYSINFO\n";


            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send");

                break;
            }


            bytes_received =
                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("Agent closed the connection.\n");

                break;
            }


            if (bytes_received < 0)
            {
                perror("recv");

                break;
            }


            printf("%s\n",
                   buffer);
        }


        /* =================================================
           LISTPROC
           ================================================= */

        else if (strcmp(buffer,
                        "LISTPROC") == 0)
        {
            const char *command =
                "LISTPROC\n";


            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send");

                break;
            }


            if (receive_command_response(
                    sockfd,
                    "END LISTPROC SID:" SID) < 0)
            {
                break;
            }
        }


        /* =================================================
           EXEC
           ================================================= */

        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            char exec_name[128];

            char extra;


            int parsed =
                sscanf(buffer + 5,
                       "%127s %c",
                       exec_name,
                       &extra);


            if (parsed != 1)
            {
                printf("Usage: EXEC <DATE|UPTIME|DISKFREE|HOSTNAME|WHOAMI>\n");

                continue;
            }


            char command[BUFFER_SIZE];


            int command_length =
                snprintf(command,
                         sizeof(command),
                         "EXEC %s\n",
                         exec_name);


            if (command_length < 0 ||
                (size_t)command_length >= sizeof(command))
            {
                printf("EXEC command is too long.\n");

                continue;
            }


            if (send_all(sockfd,
                         command,
                         (size_t)command_length) < 0)
            {
                perror("send");

                break;
            }


            char end_marker[BUFFER_SIZE];


            int marker_length =
                snprintf(end_marker,
                         sizeof(end_marker),
                         "END EXEC %s SID:%s",
                         exec_name,
                         SID);


            if (marker_length < 0 ||
                (size_t)marker_length >= sizeof(end_marker))
            {
                printf("Unable to create EXEC end marker.\n");

                break;
            }


            if (receive_command_response(
                    sockfd,
                    end_marker) < 0)
            {
                break;
            }
        }


        /* =================================================
           PUT <filename>
           ================================================= */

        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            char filename[512];

            char extra;


            int parsed =
                sscanf(buffer + 4,
                       "%511s %c",
                       filename,
                       &extra);


            if (parsed != 1)
            {
                printf("Usage: PUT <filename>\n");

                continue;
            }


            upload_file(sockfd,
                        filename);
        }


        /* =================================================
           GET <filename>
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
                printf("Usage: GET <filename>\n");

                continue;
            }


            download_file(sockfd,
                          filename);
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
                printf("Usage: MONITOR START <udp_port>\n");

                continue;
            }


            if (udp_listener_pid > 0)
            {
                printf("UDP monitoring is already active.\n");

                continue;
            }


            /* =============================================
               Create UDP socket before asking Agent to start
               ============================================= */

            udp_fd =
                socket(AF_INET,
                       SOCK_DGRAM,
                       0);


            if (udp_fd < 0)
            {
                perror("UDP socket");

                continue;
            }


            int reuse = 1;


            setsockopt(udp_fd,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &reuse,
                       sizeof(reuse));


            struct sockaddr_in udp_addr;


            memset(&udp_addr,
                   0,
                   sizeof(udp_addr));


            udp_addr.sin_family =
                AF_INET;


            udp_addr.sin_addr.s_addr =
                htonl(INADDR_ANY);


            udp_addr.sin_port =
                htons((unsigned short)udp_port);


            if (bind(udp_fd,
                     (struct sockaddr *)&udp_addr,
                     sizeof(udp_addr)) < 0)
            {
                perror("UDP bind");


                close(udp_fd);


                udp_fd = -1;


                continue;
            }


            char command[BUFFER_SIZE];


            int command_length =
                snprintf(command,
                         sizeof(command),
                         "MONITOR START %d\n",
                         udp_port);


            if (command_length < 0 ||
                (size_t)command_length >= sizeof(command))
            {
                close(udp_fd);

                udp_fd = -1;


                continue;
            }


            if (send_all(sockfd,
                         command,
                         (size_t)command_length) < 0)
            {
                perror("send");


                close(udp_fd);

                udp_fd = -1;


                break;
            }


            bytes_received =
                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("Agent closed the connection.\n");


                close(udp_fd);

                udp_fd = -1;


                break;
            }


            if (bytes_received < 0)
            {
                perror("recv");


                close(udp_fd);

                udp_fd = -1;


                break;
            }


            printf("%s\n",
                   buffer);


            if (strcmp(buffer,
                       "OK MONITOR_STARTED SID:" SID) != 0)
            {
                close(udp_fd);

                udp_fd = -1;


                continue;
            }


            /*
             * Start child process that listens for
             * periodic UDP SYSINFO datagrams.
             */
            udp_listener_pid =
                fork();


            if (udp_listener_pid < 0)
            {
                perror("fork UDP listener");


                const char *stop_command =
                    "MONITOR STOP\n";


                send_all(sockfd,
                         stop_command,
                         strlen(stop_command));


                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


                close(udp_fd);

                udp_fd = -1;


                udp_listener_pid = -1;


                continue;
            }


            else if (udp_listener_pid == 0)
            {
                /*
                 * UDP listener child does not use
                 * the TCP Controller connection.
                 */
                close(sockfd);


                run_udp_listener(udp_fd);


                exit(EXIT_SUCCESS);
            }


            else
            {
                printf("UDP monitoring listener started "
                       "on port %d (PID %d).\n",
                       udp_port,
                       udp_listener_pid);
            }
        }


        /* =================================================
           MONITOR STOP
           ================================================= */

        else if (strcmp(buffer,
                        "MONITOR STOP") == 0)
        {
            const char *command =
                "MONITOR STOP\n";


            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send");

                break;
            }


            bytes_received =
                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("Agent closed the connection.\n");

                break;
            }


            if (bytes_received < 0)
            {
                perror("recv");

                break;
            }


            printf("%s\n",
                   buffer);


            if (udp_listener_pid > 0)
            {
                kill(udp_listener_pid,
                     SIGTERM);


                waitpid(udp_listener_pid,
                        NULL,
                        0);


                udp_listener_pid = -1;
            }


            if (udp_fd >= 0)
            {
                close(udp_fd);

                udp_fd = -1;
            }
        }


        /* =================================================
           Part 11 - Graceful EXIT
           ================================================= */

        else if (strcmp(buffer,
                        "EXIT") == 0)
        {
            const char *command =
                "EXIT\n";


            /*
             * Stop the local UDP listener first.
             *
             * The Agent will stop its own monitor child
             * when it receives EXIT.
             */
            if (udp_listener_pid > 0)
            {
                kill(udp_listener_pid,
                     SIGTERM);


                waitpid(udp_listener_pid,
                        NULL,
                        0);


                udp_listener_pid = -1;
            }


            if (udp_fd >= 0)
            {
                close(udp_fd);

                udp_fd = -1;
            }


            /*
             * Send graceful EXIT to Agent.
             */
            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send EXIT");

                break;
            }


            /*
             * Wait for Agent's graceful disconnect
             * acknowledgement.
             */
            bytes_received =
                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("Agent closed the connection.\n");

                break;
            }


            if (bytes_received < 0)
            {
                perror("recv EXIT");

                break;
            }


            printf("%s\n",
                   buffer);


            if (strcmp(buffer,
                       "OK BYE SID:" SID) == 0)
            {
                printf("Graceful disconnect successful.\n");
            }

            else
            {
                printf("Unexpected EXIT response from Agent.\n");
            }


            printf("Closing Controller.\n");


            break;
        }


        /* =================================================
           Unknown local command
           Send to Agent so Agent can return protocol error.
           ================================================= */

        else
        {
            char command[BUFFER_SIZE];


            int command_length =
                snprintf(command,
                         sizeof(command),
                         "%s\n",
                         buffer);


            if (command_length < 0 ||
                (size_t)command_length >= sizeof(command))
            {
                printf("Command is too long.\n");

                continue;
            }


            if (send_all(sockfd,
                         command,
                         (size_t)command_length) < 0)
            {
                perror("send");

                break;
            }


            bytes_received =
                recv_line(sockfd,
                          buffer,
                          sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("Agent closed the connection.\n");

                break;
            }


            if (bytes_received < 0)
            {
                perror("recv");

                break;
            }


            printf("%s\n",
                   buffer);
        }
    }


    /* =====================================================
       Final cleanup

       This is mainly for abnormal/local Controller exit.
       Graceful EXIT already sets udp_listener_pid and
       udp_fd to inactive values.
       ===================================================== */

    if (udp_listener_pid > 0)
    {
        const char *stop_command =
            "MONITOR STOP\n";


        /*
         * Best-effort request to stop Agent monitoring.
         */
        if (send_all(sockfd,
                     stop_command,
                     strlen(stop_command)) == 0)
        {
            recv_line(sockfd,
                      buffer,
                      sizeof(buffer));
        }


        kill(udp_listener_pid,
             SIGTERM);


        waitpid(udp_listener_pid,
                NULL,
                0);


        udp_listener_pid = -1;
    }


    if (udp_fd >= 0)
    {
        close(udp_fd);

        udp_fd = -1;
    }


    close(sockfd);


    printf("Controller connection closed.\n");


    return EXIT_SUCCESS;
}
