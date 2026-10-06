#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Personalized RemoteOps values */
#define PORT 9410
#define AUTH_TOKEN "OPS-3252"
#define SID "2523"
#define BUFFER_SIZE 1024


/*
 * Receive one complete newline-terminated line.
 *
 * TCP is a byte stream, therefore a complete response
 * may arrive through multiple recv() calls.
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
            buffer[total] = ch;
            total++;
        }
    }

    buffer[total] = '\0';

    return (ssize_t)total;
}


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

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");

        return EXIT_FAILURE;
    }

    printf("Controller TCP socket created successfully.\n");


    /* =====================================================
       STEP 3: Prepare Agent address
       ===================================================== */

    memset(&agent_addr, 0, sizeof(agent_addr));

    agent_addr.sin_family = AF_INET;

    agent_addr.sin_port = htons(PORT);


    /*
     * Convert Agent IPv4 address from text format
     * into binary network format.
     */
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
       STEP 5: Send AUTH as first protocol command
       ===================================================== */

    const char *auth_command =
        "AUTH " AUTH_TOKEN "\n";


    if (send(sockfd,
             auth_command,
             strlen(auth_command),
             0) < 0)
    {
        perror("send");

        close(sockfd);

        return EXIT_FAILURE;
    }


    printf("AUTH command sent.\n");


    /* =====================================================
       STEP 6: Receive Agent AUTH response
       ===================================================== */

    bytes_received = recv_line(sockfd,
                               buffer,
                               sizeof(buffer));


    if (bytes_received == 0)
    {
        fprintf(stderr,
                "Agent closed connection without a response.\n");

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


    /* =====================================================
       STEP 7: Check authentication result
       ===================================================== */

    if (strcmp(buffer,
               "OK AUTHENTICATED SID:" SID) == 0)
    {
        printf("Authentication successful.\n");
        printf("RemoteOps session SID: %s\n",
               SID);
    }
    else
    {
        fprintf(stderr,
                "Authentication failed or unexpected response.\n");

        close(sockfd);

        return EXIT_FAILURE;
    }


    /* =====================================================
       STEP 8: Close connection
       ===================================================== */

    close(sockfd);

    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
