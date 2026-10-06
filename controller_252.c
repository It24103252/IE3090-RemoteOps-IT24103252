#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
   PUT: Send exactly filesize bytes from local file
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
   GET: Receive exactly filesize bytes into local file
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
   Receive current multi-line command responses
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

        printf("%s\n", buffer);

        if (strcmp(buffer,
                   end_marker) == 0)
        {
            break;
        }

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
   PUT local file to Agent
   ========================================================= */

int upload_file(int sockfd,
                const char *local_path)
{
    FILE *file;

    struct stat file_info;

    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char expected[BUFFER_SIZE];

    const char *filename;


    filename =
        strrchr(local_path, '/');

    if (filename != NULL)
    {
        filename++;
    }
    else
    {
        filename = local_path;
    }


    if (filename[0] == '\0')
    {
        printf("Invalid file name.\n");
        return -1;
    }


    if (stat(local_path,
             &file_info) < 0)
    {
        perror("stat");

        printf("Cannot find local file: %s\n",
               local_path);

        return -1;
    }


    if (!S_ISREG(file_info.st_mode))
    {
        printf("PUT requires a regular file.\n");
        return -1;
    }


    if (file_info.st_size < 0)
    {
        printf("Invalid file size.\n");
        return -1;
    }


    if ((unsigned long long)file_info.st_size >
        (unsigned long long)MAX_FILE_SIZE)
    {
        printf("File is larger than the 10 MB upload limit.\n");
        return -1;
    }


    size_t filesize =
        (size_t)file_info.st_size;


    file =
        fopen(local_path, "rb");

    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }


    int command_length =
        snprintf(command,
                 sizeof(command),
                 "PUT %s %zu\n",
                 filename,
                 filesize);


    if (command_length < 0 ||
        (size_t)command_length >= sizeof(command))
    {
        printf("PUT command is too long.\n");

        fclose(file);
        return -1;
    }


    if (send_all(sockfd,
                 command,
                 (size_t)command_length) < 0)
    {
        perror("send");

        fclose(file);
        return -1;
    }


    printf("Uploading %s (%zu bytes)...\n",
           filename,
           filesize);


    if (send_file_bytes(sockfd,
                        file,
                        filesize) < 0)
    {
        printf("Failed to send complete file.\n");

        fclose(file);
        return -1;
    }


    fclose(file);


    ssize_t n =
        recv_line(sockfd,
                  response,
                  sizeof(response));


    if (n == 0)
    {
        printf("Agent disconnected before PUT response.\n");
        return -1;
    }


    if (n < 0)
    {
        perror("recv");
        return -1;
    }


    printf("%s\n", response);


    int expected_length =
        snprintf(expected,
                 sizeof(expected),
                 "OK FILE_RECEIVED %s SID:%s",
                 filename,
                 SID);


    if (expected_length < 0 ||
        (size_t)expected_length >= sizeof(expected))
    {
        return -1;
    }


    if (strcmp(response,
               expected) == 0)
    {
        printf("Upload completed successfully.\n");
        return 0;
    }


    printf("Upload was not accepted by Agent.\n");

    return -1;
}


/* =========================================================
   GET file from Agent
   ========================================================= */

int download_file(int sockfd,
                  const char *filename)
{
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char filepath[512];

    unsigned long long file_size_value;

    char sid_value[64];

    char extra;


    /* -----------------------------------------------------
       Validate local filename
       ----------------------------------------------------- */

    if (filename == NULL ||
        filename[0] == '\0')
    {
        printf("Invalid filename.\n");
        return -1;
    }


    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        printf("Invalid filename.\n");
        return -1;
    }


    /* -----------------------------------------------------
       Build GET command
       ----------------------------------------------------- */

    int command_length =
        snprintf(command,
                 sizeof(command),
                 "GET %s\n",
                 filename);


    if (command_length < 0 ||
        (size_t)command_length >= sizeof(command))
    {
        printf("GET command is too long.\n");
        return -1;
    }


    /* -----------------------------------------------------
       Send GET command
       ----------------------------------------------------- */

    if (send_all(sockfd,
                 command,
                 (size_t)command_length) < 0)
    {
        perror("send");
        return -1;
    }


    /* -----------------------------------------------------
       Receive Agent response header

       Expected success:
       OK FILE <filesize> SID:2523
       ----------------------------------------------------- */

    ssize_t n =
        recv_line(sockfd,
                  response,
                  sizeof(response));


    if (n == 0)
    {
        printf("Agent disconnected before GET response.\n");
        return -1;
    }


    if (n < 0)
    {
        perror("recv");
        return -1;
    }


    printf("%s\n", response);


    /*
     * Handle errors such as:
     * ERR 005 FILE_NOT_FOUND SID:2523
     */
    if (strncmp(response,
                "ERR ",
                4) == 0)
    {
        return -1;
    }


    int parsed =
        sscanf(response,
               "OK FILE %llu SID:%63s %c",
               &file_size_value,
               sid_value,
               &extra);


    if (parsed != 2)
    {
        printf("Invalid GET response from Agent.\n");
        return -1;
    }


    if (strcmp(sid_value,
               SID) != 0)
    {
        printf("Unexpected SID in GET response.\n");
        return -1;
    }


    if (file_size_value >
        (unsigned long long)MAX_FILE_SIZE)
    {
        printf("Agent file is larger than the supported limit.\n");
        return -1;
    }


    size_t filesize =
        (size_t)file_size_value;


    /* -----------------------------------------------------
       Build local destination:

       ./downloads/<filename>
       ----------------------------------------------------- */

    int path_length =
        snprintf(filepath,
                 sizeof(filepath),
                 "%s/%s",
                 DOWNLOAD_DIR,
                 filename);


    if (path_length < 0 ||
        (size_t)path_length >= sizeof(filepath))
    {
        printf("Download path is too long.\n");
        return -1;
    }


    /* -----------------------------------------------------
       Open local destination
       ----------------------------------------------------- */

    FILE *file =
        fopen(filepath, "wb");


    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }


    printf("Downloading %s (%zu bytes)...\n",
           filename,
           filesize);


    /* -----------------------------------------------------
       Receive exactly filesize raw bytes
       ----------------------------------------------------- */

    if (receive_file_bytes(sockfd,
                           file,
                           filesize) < 0)
    {
        fclose(file);

        remove(filepath);

        printf("Download failed before complete file arrived.\n");

        return -1;
    }


    if (fclose(file) != 0)
    {
        remove(filepath);

        printf("Failed to save downloaded file.\n");

        return -1;
    }


    printf("Download completed successfully.\n");

    printf("Saved as: %s\n",
           filepath);


    return 0;
}


/* =========================================================
   MAIN
   ========================================================= */

int main(int argc,
         char *argv[])
{
    int sockfd;

    struct sockaddr_in agent_addr;

    char buffer[BUFFER_SIZE];

    ssize_t bytes_received;


    if (argc != 2)
    {
        fprintf(stderr,
                "Usage: %s <agent-ip>\n",
                argv[0]);

        return EXIT_FAILURE;
    }


    sockfd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }


    printf("Controller TCP socket created successfully.\n");


    memset(&agent_addr,
           0,
           sizeof(agent_addr));


    agent_addr.sin_family =
        AF_INET;

    agent_addr.sin_port =
        htons(PORT);


    if (inet_pton(AF_INET,
                  argv[1],
                  &agent_addr.sin_addr) <= 0)
    {
        fprintf(stderr,
                "Invalid Agent IPv4 address: %s\n",
                argv[1]);

        close(sockfd);
        return EXIT_FAILURE;
    }


    printf("Connecting to Agent %s:%d...\n",
           argv[1],
           PORT);


    if (connect(sockfd,
                (struct sockaddr *)&agent_addr,
                sizeof(agent_addr)) < 0)
    {
        perror("connect");

        close(sockfd);
        return EXIT_FAILURE;
    }


    printf("Connected to RemoteOps Agent successfully.\n");


    /* =====================================================
       AUTH
       ===================================================== */

    const char *auth_command =
        "AUTH " AUTH_TOKEN "\n";


    if (send_all(sockfd,
                 auth_command,
                 strlen(auth_command)) < 0)
    {
        perror("send");

        close(sockfd);
        return EXIT_FAILURE;
    }


    printf("AUTH command sent.\n");


    bytes_received =
        recv_line(sockfd,
                  buffer,
                  sizeof(buffer));


    if (bytes_received <= 0)
    {
        fprintf(stderr,
                "Agent closed connection without AUTH response.\n");

        close(sockfd);
        return EXIT_FAILURE;
    }


    printf("Agent response: %s\n",
           buffer);


    if (strcmp(buffer,
               "OK AUTHENTICATED SID:" SID) != 0)
    {
        fprintf(stderr,
                "Authentication failed.\n");

        close(sockfd);
        return EXIT_FAILURE;
    }


    printf("Authentication successful.\n");

    printf("RemoteOps session SID: %s\n\n",
           SID);


    /* =====================================================
       Interactive command loop
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


        buffer[strcspn(buffer,
                      "\r\n")] = '\0';


        if (strlen(buffer) == 0)
        {
            continue;
        }


        /* SYSINFO */

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


            if (receive_command_response(
                    sockfd,
                    "END SYSINFO SID:" SID) < 0)
            {
                break;
            }
        }


        /* LISTPROC */

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


        /* EXEC */

        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            char command[BUFFER_SIZE + 2];

            char end_marker[BUFFER_SIZE];

            char exec_name[32];


            snprintf(exec_name,
                     sizeof(exec_name),
                     "%.31s",
                     buffer + 5);


            snprintf(command,
                     sizeof(command),
                     "%s\n",
                     buffer);


            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send");
                break;
            }


            snprintf(end_marker,
                     sizeof(end_marker),
                     "END EXEC %s SID:%s",
                     exec_name,
                     SID);


            if (receive_command_response(
                    sockfd,
                    end_marker) < 0)
            {
                break;
            }
        }


        /* PUT */

        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            const char *local_path =
                buffer + 4;


            if (local_path[0] == '\0')
            {
                printf("Usage: PUT <local-file-path>\n");
                continue;
            }


            upload_file(sockfd,
                        local_path);
        }


        /* =================================================
           GET

           Usage:
           GET <filename>
           ================================================= */

        else if (strncmp(buffer,
                         "GET ",
                         4) == 0)
        {
            const char *filename =
                buffer + 4;


            if (filename[0] == '\0')
            {
                printf("Usage: GET <filename>\n");
                continue;
            }


            download_file(sockfd,
                          filename);
        }


        /* Temporary EXIT */

        else if (strcmp(buffer,
                        "EXIT") == 0)
        {
            printf("Closing Controller.\n");
            break;
        }


        /* Other commands */

        else
        {
            char command[BUFFER_SIZE + 2];


            snprintf(command,
                     sizeof(command),
                     "%s\n",
                     buffer);


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
                printf("Agent disconnected.\n");
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


    close(sockfd);

    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
