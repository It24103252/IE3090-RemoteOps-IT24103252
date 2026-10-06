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

/* Personalized RemoteOps values */
#define PORT 9410
#define BACKLOG 10
#define AUTH_TOKEN "OPS-3252"
#define SID "2523"
#define BUFFER_SIZE 1024


/*
 * Receive exactly one newline-terminated protocol line.
 *
 * TCP is a byte stream, so one recv() call is not guaranteed
 * to contain one complete command.
 *
 * This function keeps receiving bytes until:
 *   1. '\n' is received,
 *   2. the connection closes,
 *   3. an error occurs, or
 *   4. the buffer becomes full.
 */
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

        ssize_t n = recv(sockfd, &ch, 1, 0);

        if (n == 0)
        {
            /* Peer closed the connection */
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

        /*
         * Ignore carriage return so both:
         *
         * \n
         *
         * and
         *
         * \r\n
         *
         * are accepted.
         */
        if (ch != '\r')
        {
            buffer[total] = ch;
            total++;
        }
    }

    buffer[total] = '\0';

    return (ssize_t)total;
}


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

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

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

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    server_addr.sin_port = htons(PORT);


    /* =====================================================
       STEP 4: Bind to personalized port 9410
       ===================================================== */

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        return EXIT_FAILURE;
    }

    printf("Agent bound to TCP port %d.\n", PORT);


    /* =====================================================
       STEP 5: Listen for Controller connections
       ===================================================== */

    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");

        close(server_fd);

        return EXIT_FAILURE;
    }

    printf("RemoteOps Agent listening on port %d...\n",
           PORT);


    /* =====================================================
       STEP 6: Prevent zombie child processes
       ===================================================== */

    signal(SIGCHLD, SIG_IGN);


    /* =====================================================
       STEP 7: Continuously accept Controllers
       ===================================================== */

    while (1)
    {
        client_len = sizeof(client_addr);

        client_fd = accept(server_fd,
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
           STEP 8: Create child for this Controller
           ================================================= */

        pid_t pid = fork();


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


            /*
             * Child handles this Controller.
             * It does not accept new Controllers.
             */
            close(server_fd);


            printf("[Child %d] Handling Controller %s:%d\n",
                   getpid(),
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));


            /* =============================================
               STEP 9: Receive first complete protocol line
               ============================================= */

            bytes_received = recv_line(client_fd,
                                       buffer,
                                       sizeof(buffer));


            if (bytes_received == 0)
            {
                printf("[Child %d] Controller disconnected before AUTH.\n",
                       getpid());

                close(client_fd);

                exit(EXIT_SUCCESS);
            }


            if (bytes_received < 0)
            {
                perror("recv");

                close(client_fd);

                exit(EXIT_FAILURE);
            }


            printf("[Child %d] Received: %s\n",
                   getpid(),
                   buffer);


            /* =============================================
               STEP 10: AUTH must be first command
               ============================================= */

            if (strcmp(buffer, "AUTH " AUTH_TOKEN) == 0)
            {
                const char *response =
                    "OK AUTHENTICATED SID:" SID "\n";


                if (send(client_fd,
                         response,
                         strlen(response),
                         0) < 0)
                {
                    perror("send");
                }
                else
                {
                    printf("[Child %d] Authentication successful.\n",
                           getpid());
                }
            }
            else
            {
                const char *response =
                    "ERR 001 AUTH_FAILED SID:" SID "\n";


                if (send(client_fd,
                         response,
                         strlen(response),
                         0) < 0)
                {
                    perror("send");
                }
                else
                {
                    printf("[Child %d] Authentication failed.\n",
                           getpid());
                }
            }


            /* =============================================
               STEP 11: Close Controller session
               ============================================= */

            close(client_fd);


            printf("[Child %d] Controller disconnected.\n",
                   getpid());


            exit(EXIT_SUCCESS);
        }
        else
        {
            /* =============================================
               PARENT PROCESS
               ============================================= */

            /*
             * Child owns this Controller connection.
             * Parent closes its copy and returns to accept().
             */
            close(client_fd);
        }
    }


    /*
     * Normally unreachable because while(1)
     * keeps the Agent running.
     */
    close(server_fd);

    return EXIT_SUCCESS;
}
