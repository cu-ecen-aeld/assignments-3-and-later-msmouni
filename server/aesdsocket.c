#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SERVER_PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 100

int server_sockfd = -1;
int client_sockfd = -1;
char buffer[BUFFER_SIZE];

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal %d, exiting...", signo);

        // Close the client socket if it's open
        if (client_sockfd != -1)
        {
            close(client_sockfd);
        }

        // Close the server socket if it's open
        if (server_sockfd != -1)
        {
            close(server_sockfd);
        }

        // Remove the temporary file
        if (remove(FILE_PATH) == 0)
        {
            syslog(LOG_INFO, "Removed file %s", FILE_PATH);
        }
        else
        {
            syslog(LOG_ERR, "Failed to remove file %s: %s", FILE_PATH, strerror(errno));
        }

        closelog(); // Close syslog
        exit(0);    // Exit the program
    }
}

void handle_connection(int client_fd)
{

    ssize_t bytes_received;
    int fd;

    fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0644);
    if (fd < 0)
    {
        syslog(LOG_ERR, "Error opening file: %s", strerror(errno));
        return;
    }

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';

        char *newline = strchr(buffer, '\n');
        if (newline)
        {
            // Write data up to and including the newline
            if (write(fd, buffer, newline - buffer + 1) < 0)
            {
                syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
            }
            break; // Exit loop after handling one packet
        }
        else
        {
            // Write the entire buffer if no newline is found
            if (write(fd, buffer, bytes_received) < 0)
            {
                syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
            }
        }
    }

    if (bytes_received < 0)
    {
        syslog(LOG_ERR, "Error receiving data: %s", strerror(errno));
    }

    // Send the contents of the file back to the client
    lseek(fd, 0, SEEK_SET);
    while ((bytes_received = read(fd, buffer, BUFFER_SIZE)) > 0)
    {
        if (send(client_fd, buffer, bytes_received, 0) < 0)
        {
            syslog(LOG_ERR, "Error sending data: %s", strerror(errno));
            break;
        }
    }

    close(fd);
}

/*
Double Fork Steps

Let’s first look at the “Double Fork” steps before diving into the code. Note, to understand this you need to be comfortable with the concepts of the session, parent process group, process IDs and hierarchy of processes. I’m going to take the steps from a man 7 daemon

    Call fork() so the process can run in the background
    Call setsid() so once we exit from our shell the shell’s session isn’t killed, which would remove our daemon.
    Call fork() again so the process isn’t the process group leader and cannot take a controlling terminal.

Here is a little more details about these 3 steps above for a “Double Fork”.

    Before a fork call the process will be the process group leader in the shell session. Thus, the parent process will be the shell’s process ID and session ID.
    After our first call to fork the parent process will be killed, thus, the child orphaned and the child will be adopted by the init process and the pgid will be 1. The process group and session will remain the same. The child is no longer the process group leader.
    Call setsid which will put us in a new session and make our process the process group leader, session leader and give us no terminal.
*/
void daemonize()
{
    pid_t pid;

    // Fork the first time
    pid = fork();

    if (pid < 0)
    {
        syslog(LOG_ERR, "Error forking: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }

    // Child process continues
    if (setsid() < 0)
    {
        syslog(LOG_ERR, "Error creating new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Fork a second time to ensure the daemon can't acquire a terminal
    pid = fork();

    if (pid < 0)
    {
        syslog(LOG_ERR, "Error forking: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        // Parent process exits
        exit(EXIT_SUCCESS);
    }

    // Set the file permissions mask to 0
    umask(0);

    // Change the working directory to root
    if (chdir("/") < 0)
    {
        syslog(LOG_ERR, "Error changing directory to /: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Close all open file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char *argv[])
{
    struct sigaction sa;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Register the signal handler for SIGINT and SIGTERM
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "Error registering SIGINT handler: %s", strerror(errno));
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "Error registering SIGTERM handler: %s", strerror(errno));
        return -1;
    }

    //////////////////////////////////////////////////////////////////////////////

    bool run_as_deamon = false;

    if (argc == 2)
    {
        if (strcmp(argv[1], "-d") == 0)
        {
            run_as_deamon = true;
        }
    }

    /////////////////////////////////////////
    socklen_t client_len;
    struct sockaddr_in client_address = {0};
    struct sockaddr_in server_address = {0};
    ////////////////////////////////////////

    server_sockfd = socket(
        AF_INET,
        SOCK_STREAM, 0);

    if (server_sockfd == -1)
    {
        char *err = strerror(errno);
        syslog(LOG_ERR, "Error creating server socket: %s", err);
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(SERVER_PORT);
    if (bind(
            server_sockfd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) == -1)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (run_as_deamon)
    {
        daemonize();
    }

    if (listen(server_sockfd, 5) == -1)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Listening on port %d\n", SERVER_PORT);

    while (1)
    {
        client_len = sizeof(client_address);
        client_sockfd = accept(
            server_sockfd,
            (struct sockaddr *)&client_address,
            &client_len);

        char *client_add = inet_ntoa(client_address.sin_addr);
        syslog(LOG_DEBUG, "Accepted connection from %s", client_add);
        printf("server: got connection from %s\n", client_add);

        handle_connection(client_sockfd);

        close(client_sockfd);
    }

    closelog();
    return EXIT_SUCCESS;
}
