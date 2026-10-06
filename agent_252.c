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

#define PORT 9410
#define BACKLOG 10

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
       STEP 4: Bind socket to personalised TCP port 9410
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

    printf("RemoteOps Agent listening on port %d...\n", PORT);


    /* =====================================================
       STEP 6: Prevent zombie child processes
       ===================================================== */

    signal(SIGCHLD, SIG_IGN);


    /* =====================================================
       STEP 7: Continuously accept Controller connections
       ===================================================== */

    while (1)
    {
        client_len = sizeof(client_addr);

        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_len);

        if (client_fd < 0)
        {
            /*
             * If accept() was interrupted by a signal,
             * simply try again.
             */

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
           STEP 8: Create child process for this Controller
           ================================================= */

        pid_t pid = fork();


        if (pid < 0)
        {
            /*
             * fork() failed.
             * Parent closes this Controller connection
             * and continues waiting for another one.
             */

            perror("fork");

            close(client_fd);

            continue;
        }


        if (pid == 0)
        {
            /* =============================================
               CHILD PROCESS
               ============================================= */

            /*
             * Child does not need the listening socket.
             * The parent is responsible for accepting
             * new Controller connections.
             */

            close(server_fd);


            printf("[Child %d] Handling Controller %s:%d\n",
                   getpid(),
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));


            /*
             * TEMPORARY:
             *
             * Keep the child alive for 30 seconds so that
             * multiple simultaneous connections can be
             * demonstrated.
             *
             * This will later be replaced with the actual
             * AUTH and command-processing logic.
             */

            sleep(60);


            printf("[Child %d] Controller disconnected.\n",
                   getpid());


            /*
             * Close this Controller's connected socket.
             */

            close(client_fd);


            /*
             * End only the child process.
             */

            exit(EXIT_SUCCESS);
        }
        else
        {
            /* =============================================
               PARENT PROCESS
               ============================================= */

            /*
             * The child handles this Controller.
             *
             * Therefore the parent closes its copy of the
             * connected socket and immediately returns to
             * accept() to wait for another Controller.
             */

            close(client_fd);
        }
    }


    /*
     * Normally unreachable because the Agent runs
     * continuously until it is manually terminated.
     */

    close(server_fd);

    return EXIT_SUCCESS;
}
