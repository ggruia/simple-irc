#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
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

// Client info
typedef struct {
    int sockfd;
    char name[1000];
} client;

client clients[MAX_CLIENTS];
pthread_mutex_t client_lock;

// Send a message to a specific client
void send_message_to_client(const char *msg, client *sender, int receiver_fd) {
    pthread_mutex_lock(&client_lock);  // Lock to avoid race conditions

    // Prepare the message with the client ID
    char client_message[1100];
    snprintf(client_message, sizeof(client_message), "%s: %s", sender->name, msg);

    // Send the prepared message to the receiver client
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = &clients[i];
        if (c->sockfd != 0 && c->sockfd == receiver_fd) {
            int bytes_written = write(c->sockfd, client_message, strlen(client_message));
            if (bytes_written < 0) {
                perror("Error sending message to client");
            }
            break;
        }
    }

    pthread_mutex_unlock(&client_lock);
}

// Broadcast a client's message to all other clients
void broadcast_client_message(const char *msg, client *sender) {
    pthread_mutex_lock(&client_lock);  // Lock to avoid race conditions

    // Prepare the message with the client ID
    char client_message[1100];
    snprintf(client_message, sizeof(client_message), "%s: %s", sender->name, msg);

	// Send the prepared message to every client, other than the sender
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = &clients[i];
        if (c->sockfd != 0 && c->sockfd != sender->sockfd) {
            int bytes_written = write(c->sockfd, client_message, strlen(client_message));
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
        client *c = &clients[i];
        if (c->sockfd != 0) {
            int bytes_written = write(c->sockfd, server_message, strlen(server_message));
            if (bytes_written < 0) {
                perror("Error sending message to client");
            }
        }
    }

    pthread_mutex_unlock(&client_lock);
}

// Handle each client connection
void *handle_client_input(void *arg) {
    client *c = (client *)arg;
    char buf[1000];
    char client_name[1000];

    // Assign a name to the client
    write(c->sockfd, "Enter your name: ", 17);
    int bytes_read = read(c->sockfd, buf, sizeof(buf) - 1);
    buf[bytes_read] = '\0';
    strcpy(client_name, buf);

    pthread_mutex_lock(&client_lock);
    strncpy(c->name, client_name, sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
    pthread_mutex_unlock(&client_lock);

    // Notify others that a new client has joined
    char join_msg[1100];
    snprintf(join_msg, sizeof(join_msg), "%s has joined the chat.", c->name);
    broadcast_server_message(join_msg);

    while (1) {
        int bytes_read = read(c->sockfd, buf, sizeof(buf) - 1);

        // Connection closed by client
        if (bytes_read <= 0) {
            break;
        }

        buf[bytes_read] = '\0';
        
        // Check the message for exit signal
        if (strncmp(buf, "/exit", 5) == 0) {
            printf("Client %d sent EXIT signal. Closing connection...\n", c->sockfd);

            // Notify all clients that this client is leaving
            char leave_msg[1100];
            snprintf(leave_msg, sizeof(leave_msg), "%s has left the chat.", c->name);
            broadcast_server_message(leave_msg);  // Broadcast that the client has left

            close(c->sockfd);

            // Remove client from active clients list
            pthread_mutex_lock(&client_lock);
            c->sockfd = 0;
            memset(c->name, 0, sizeof(c->name));
            pthread_mutex_unlock(&client_lock);
            
            pthread_exit(NULL);
        }

        // If the message starts with "/say @", find the target client and send the message
        if (strncmp(buf, "/say @", 6) == 0) { // Skip "/say @"
            char *original_msg = buf;  // Preserve the original buffer
            char temp_buf[1000];       // Temporary buffer for processing
            strncpy(temp_buf, buf, sizeof(temp_buf) - 1);
            temp_buf[sizeof(temp_buf) - 1] = '\0';

            char *client_name = temp_buf + 6; // Get the client name after "/say @"
            char *message = strchr(client_name, ' '); // Find the space after the client name

            if (message) {
                *message = '\0';  // Null-terminate the client name
                message++;  // Skip the space to get the message
                int receiverfd = -1;

                // Search for the target client in the client list
                pthread_mutex_lock(&client_lock);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].sockfd != 0 && strcmp(clients[i].name, client_name) == 0) {
                        receiverfd = clients[i].sockfd;  // Found the client, get their sockfd
                        break;
                    }
                }
                pthread_mutex_unlock(&client_lock);

                if (receiverfd != -1) {
                    send_message_to_client(message, c, receiverfd);  // Send the message to the found client
                } else {
                    // If client is not found, inform the sender
                    char not_found_msg[1100];
                    snprintf(not_found_msg, sizeof(not_found_msg), "Client %s not found.\n", client_name);
                    write(c->sockfd, not_found_msg, strlen(not_found_msg));
                }
            } else {
                write(c->sockfd, "Invalid /say command format. Usage: /say @<clientname> <message>", 65);
            }
        } else {
            // If the message is not a /say, broadcast it to all other clients
            broadcast_client_message(buf, c);
        }

        char client_message[2100];
        snprintf(client_message, sizeof(client_message), "%s: %s", c->name, buf);
        printf("%s\n", client_message);
    }

    printf("%s disconnected.\n", client_name);
    close(c->sockfd);
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
    memset(clients, 0, sizeof(clients));

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
        client *c;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sockfd == 0) {
                c = &clients[i];
                c->sockfd = clientfd;
                break;
            }
        }
        pthread_mutex_unlock(&client_lock);

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client_input, c);
        pthread_detach(client_thread); // Detach thread to prevent memory leaks
    }

    close(sockfd);
    pthread_mutex_destroy(&client_lock);
    return 0;
}
