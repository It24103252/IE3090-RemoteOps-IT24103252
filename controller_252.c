#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9410

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;

    /* Check whether Agent IP was provided */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <agent_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Step 1: Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    printf("Controller TCP socket created successfully.\n");

    /* Step 2: Prepare Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    /* Convert IP address from text to binary */
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid Agent IP address: %s\n", argv[1]);
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connecting to Agent %s:%d...\n", argv[1], PORT);

    /* Step 3: Connect to Agent */
    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connected to RemoteOps Agent successfully.\n");

    /* Authentication and commands will be added later */

    close(sockfd);

    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
