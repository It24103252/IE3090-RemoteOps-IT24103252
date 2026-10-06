#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* =========================================================
   Personalized RemoteOps values
   ========================================================= */

#define PORT 9410
#define AUTH_TOKEN "OPS-3252"
#define SID "2523"
#define BUFFER_SIZE 1024


/* =========================================================
   Receive one newline-terminated protocol line
   ========================================================= */

ssize_t recv_line(int sockfd, char *buffer, size_t size)
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

        /*
         * n == 0 means the other side closed
         * the TCP connection.
         */
        if (n == 0)
        {
            if (total == 0)
            {
                return 0;
            }

            break;
        }

        /*
         * n < 0 means recv() failed.
         */
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        /*
         * Newline marks the end of one
         * protocol line.
         */
        if (ch == '\n')
        {
            break;
        }

        /*
         * Ignore carriage return.
         */
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
   Receive a multi-line response

   Used by:
       SYSINFO
       LISTPROC
       EXEC
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

        /*
         * Agent disconnected.
         */
        if (n == 0)
        {
            printf("Agent closed the connection.\n");

            return -1;
        }

        /*
         * recv() failed.
         */
        if (n < 0)
        {
            perror("recv");

            return -1;
        }

        /*
         * Display every line received
         * from the Agent.
         */
        printf("%s\n",
               buffer);


        /*
         * Stop when expected END marker
         * is received.
         */
        if (strcmp(buffer,
                   end_marker) == 0)
        {
            break;
        }


        /*
         * If Agent returns an error,
         * there will be no END marker.
         *
         * Therefore stop reading and
         * return to RemoteOps prompt.
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
   Main Controller
   ========================================================= */

int main(int argc, char *argv[])
{
    int sockfd;

    struct sockaddr_in agent_addr;

    char buffer[BUFFER_SIZE];

    ssize_t bytes_received;


    /* =====================================================
       STEP 1: Check command-line argument
       ===================================================== */

    if (argc != 2)
    {
        fprintf(stderr,
                "Usage: %s <agent-ip>\n",
                argv[0]);

        return EXIT_FAILURE;
    }


    /* =====================================================
       STEP 2: Create TCP socket
       ===================================================== */

    sockfd = socket(AF_INET,
                    SOCK_STREAM,
                    0);

    if (sockfd < 0)
    {
        perror("socket");

        return EXIT_FAILURE;
    }


    printf("Controller TCP socket created successfully.\n");


    /* =====================================================
       STEP 3: Prepare Agent address
       ===================================================== */

    memset(&agent_addr,
           0,
           sizeof(agent_addr));


    agent_addr.sin_family = AF_INET;

    agent_addr.sin_port = htons(PORT);


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


    /* =====================================================
       STEP 4: Connect to Agent
       ===================================================== */

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
       STEP 5: Send AUTH command
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


    /* =====================================================
       STEP 6: Receive AUTH response
       ===================================================== */

    bytes_received =
        recv_line(sockfd,
                  buffer,
                  sizeof(buffer));


    if (bytes_received == 0)
    {
        fprintf(stderr,
                "Agent closed connection without AUTH response.\n");

        close(sockfd);

        return EXIT_FAILURE;
    }


    if (bytes_received < 0)
    {
        perror("recv");

        close(sockfd);

        return EXIT_FAILURE;
    }


    printf("Agent response: %s\n",
           buffer);


    /*
     * Verify personalized successful
     * authentication response.
     */
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
       STEP 7: Interactive authenticated command loop
       ===================================================== */

    while (1)
    {
        printf("RemoteOps> ");

        fflush(stdout);


        /*
         * Read command from keyboard.
         */
        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL)
        {
            printf("\nInput closed.\n");

            break;
        }


        /*
         * Remove newline / carriage return
         * entered by the user.
         */
        buffer[strcspn(buffer, "\r\n")] = '\0';


        /*
         * Ignore empty command.
         */
        if (strlen(buffer) == 0)
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


            if (receive_command_response(
                    sockfd,
                    "END SYSINFO SID:" SID) < 0)
            {
                break;
            }
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
            char command[BUFFER_SIZE + 2];

            char end_marker[BUFFER_SIZE];

            char exec_name[32];


            /*
             * Example:
             *
             * buffer = "EXEC DATE"
             *
             * buffer + 5 points to:
             *
             * "DATE"
             */
            snprintf(exec_name,
                     sizeof(exec_name),
                     "%.31s",
                     buffer + 5);


            /*
             * Add protocol newline.
             *
             * "EXEC DATE"
             *
             * becomes:
             *
             * "EXEC DATE\n"
             */
            snprintf(command,
                     sizeof(command),
                     "%s\n",
                     buffer);


            /*
             * Send EXEC command to Agent.
             */
            if (send_all(sockfd,
                         command,
                         strlen(command)) < 0)
            {
                perror("send");

                break;
            }


            /*
             * Create expected END marker.
             *
             * Example:
             *
             * END EXEC DATE SID:2523
             */
            snprintf(end_marker,
                     sizeof(end_marker),
                     "END EXEC %s SID:%s",
                     exec_name,
                     SID);


            /*
             * Receive all EXEC output until
             * END marker or ERR response.
             */
            if (receive_command_response(
                    sockfd,
                    end_marker) < 0)
            {
                break;
            }
        }


        /* =================================================
           Temporary EXIT
           ================================================= */

        else if (strcmp(buffer,
                        "EXIT") == 0)
        {
            /*
             * EXIT currently closes the
             * Controller locally.
             *
             * We will implement the assignment's
             * proper graceful QUIT protocol later.
             */

            printf("Closing Controller.\n");

            break;
        }


        /* =================================================
           Other commands
           ================================================= */

        else
        {
            char command[BUFFER_SIZE + 2];


            /*
             * Add newline before sending.
             */
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


            /*
             * Unknown commands currently produce
             * a single-line Agent response.
             */
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


    /* =====================================================
       STEP 8: Close Controller socket
       ===================================================== */

    close(sockfd);


    printf("Controller connection closed.\n");


    return EXIT_SUCCESS;
}
