#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define PORT 55555
#define BACKLOG 5
#define MAX_CLIENTS 10

int client_sockets[MAX_CLIENTS];
pthread_mutex_t client_lock;

// Broadcast a client's message to all other clients
void broadcast_client_message(const char *msg, int sender_fd) {
    pthread_mutex_lock(&client_lock);  // Lock to avoid race conditions

    // Prepare the message with the client ID
    char client_message[1100];
    snprintf(client_message, sizeof(client_message), "Client %d: %s", sender_fd, msg);

	// Send the prepared message to every client, other than the sender
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != 0 && client_sockets[i] != sender_fd) {
            int bytes_written = write(client_sockets[i], client_message, strlen(client_message));
            if (bytes_written < 0) {
                perror("Error sending message to client");
            }
        }
    }

    pthread_mutex_unlock(&client_lock);
}

// Broadcast a server message to all clients
void broadcast_server_message(const char *msg) {
    pthread_mutex_lock(&client_lock);  // Lock to avoid race conditions

	// Prepare the message from the server
    char server_message[1100];
    snprintf(server_message, sizeof(server_message), "Server: %s", msg);
	printf("Broadcast: %s\n", msg);

	// Send the prepared message to every client
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != 0) {
            int bytes_written = write(client_sockets[i], server_message, strlen(server_message));
            if (bytes_written < 0) {
                perror("Error sending message to client");
            }
        }
    }

    pthread_mutex_unlock(&client_lock);
}

// Handle each client connection
void *handle_client_input(void *arg) {
    int clientfd = *(int *)arg;
    char buf[1000];

    while (1) {
        int bytes_read = read(clientfd, buf, sizeof(buf) - 1);

        // Connection closed by client
        if (bytes_read <= 0) {
            break;
        }

        buf[bytes_read] = '\0';
        
        // Check the message for exit signal
        if (strncmp(buf, "/exit", 5) == 0) {
            printf("Client %d sent EXIT signal. Closing connection...\n", clientfd);
            close(clientfd);

            // Remove client from active clients list
            pthread_mutex_lock(&client_lock);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == clientfd) {
                    client_sockets[i] = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&client_lock);
            pthread_exit(NULL);
        }

        // If the message starts with "/say", broadcast it to all other clients
        if (strncmp(buf, "/say ", 5) == 0) {
            broadcast_client_message(buf + 5, clientfd);  // Skip "/say " and broadcast the message
        }

		char client_message[1100];
    	snprintf(client_message, sizeof(client_message), "Client %d: %s", clientfd, buf);
		printf("%s\n", client_message);
    }

    printf("Client %d disconnected.\n", clientfd);
    close(clientfd);
    pthread_exit(NULL);
}

// Handle server stdin input
void *handle_server_input(void *arg) {
    char input[1000];

    while (1) {
        fgets(input, sizeof(input), stdin);
		input[strcspn(input, "\n")] = 0;
        if (strncmp(input, "/bc ", 4) == 0) {
            broadcast_server_message(input + 4);  // Skip "/bc " and broadcast the message
        }
    }
}


int main() {
    int sockfd, clientfd;
    struct sockaddr_in addr, con_addr;
    socklen_t sockaddr_size = sizeof(con_addr);

    pthread_mutex_init(&client_lock, NULL);
    memset(client_sockets, 0, sizeof(client_sockets));

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(1);
    }
    printf("Server socket BOUND to port %d\n", PORT);

    listen(sockfd, BACKLOG);
    printf("Server is listening...\n");

    // Start a thread to handle server stdin input for broadcasting
    pthread_t stdin_thread;
    pthread_create(&stdin_thread, NULL, handle_server_input, NULL);

    while (1) {
		// Wait for a connection to the server and open a new socket for the client
        clientfd = accept(sockfd, (struct sockaddr *)&con_addr, &sockaddr_size);
        if (clientfd < 0) {
            perror("accept");
            continue;
        }
        printf("Client %d established connection\n", clientfd);

 		// Add the client to the list
        pthread_mutex_lock(&client_lock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] == 0) {
                client_sockets[i] = clientfd;
                break;
            }
        }
        pthread_mutex_unlock(&client_lock);

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client_input, &clientfd);
        pthread_detach(client_thread); // Detach thread to prevent memory leaks
    }

    close(sockfd);
    pthread_mutex_destroy(&client_lock);
    return 0;
}
