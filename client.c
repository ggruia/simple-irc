#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEST_PORT 55555

int main()
{
    struct sockaddr_in dest_addr;
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	char server_ip[50];
    printf("Enter server address:\n");
    fgets(server_ip, sizeof(server_ip), stdin);
    server_ip[strcspn(server_ip, "\n")] = 0;

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    if (inet_pton(AF_INET, server_ip, &dest_addr.sin_addr) <= 0) {
        perror("invalid address");
        return 1;
    }
    memset(dest_addr.sin_zero, '\0', sizeof(dest_addr.sin_zero));

	if (connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("Established connection to server!\n");
	printf("You can now chat (type '/exit' to disconnect)\n");

    fd_set read_fds;
    char msg[1000];
    char server_msg[1000];

    while (1) {
        // Initialize the file descriptor set
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds); // Monitor stdin
        FD_SET(sockfd, &read_fds); // Monitor server socket

        // Wait for activity on either stdin or socket
        int max_fd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("select");
            break;
        }

        // Check for activity on stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            fgets(msg, sizeof(msg), stdin);
            msg[strcspn(msg, "\n")] = 0;

            // Send the message to the server
            int bytes_written = write(sockfd, msg, strlen(msg));
            if (bytes_written < 0) {
                perror("write");
                break;
            }

            // Exit if the user sends the exit signal
            if (strncmp(msg, "/exit", 5) == 0) {
                printf("Exiting...\n");
                break;
            }
        }

        // Check for activity on the server socket
        if (FD_ISSET(sockfd, &read_fds)) {
            int bytes_read = read(sockfd, server_msg, sizeof(server_msg) - 1);
            if (bytes_read == 0) {
                // Server closed the connection
                printf("\nServer has closed the connection.\n");
                break;
            } else if (bytes_read < 0) {
                perror("read");
                break;
            }

            server_msg[bytes_read] = '\0';
            printf("%s\n", server_msg);
        }
    }

    close(sockfd);
    printf("Client CLOSED!\n");
    return 0;
}