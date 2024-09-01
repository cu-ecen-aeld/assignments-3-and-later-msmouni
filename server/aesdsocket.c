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
#include <pthread.h>
#include "queue.h"
#include <time.h>

#define SERVER_PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 100

int server_sockfd = -1;

pthread_mutex_t text_file_lock = PTHREAD_MUTEX_INITIALIZER;

bool is_running = true;

timer_t timer_id;

//////////////// SLIST
struct slist_element
{
    pthread_t thread_id;
    bool thread_completed;

    int socket_fd;

    int text_fd;

    SLIST_ENTRY(slist_element)
    slist_elements;
};

SLIST_HEAD(slist_head, slist_element);

struct slist_head head;
/////////////////////////////

void close_threads_res()
{
    struct slist_element *thread_in_list;

    while (!SLIST_EMPTY(&head))
    {
        thread_in_list = SLIST_FIRST(&head);

        thread_in_list->thread_completed = true;

        pthread_join(thread_in_list->thread_id, NULL);

        close(thread_in_list->text_fd);
        close(thread_in_list->socket_fd);

        SLIST_REMOVE_HEAD(&head, slist_elements);
        free(thread_in_list);
    }

    pthread_mutex_destroy(&text_file_lock);
}

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal %d, exiting...", signo);

        is_running = false;
    }
}

void *handle_connection(void *arg)
{
    struct slist_element *element = (struct slist_element *)arg;

    char buffer[BUFFER_SIZE] = {0};

    ssize_t bytes_received = {0};

    element->text_fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0644);
    if (element->text_fd < 0)
    {
        syslog(LOG_ERR, "Error opening file: %s", strerror(errno));
        return arg;
    }

    while (is_running && (bytes_received = recv(element->socket_fd, buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0';

        char *newline = strchr(buffer, '\n');
        if (newline)
        {
            pthread_mutex_lock(&text_file_lock);

            // Write data up to and including the newline
            if (write(element->text_fd, buffer, newline - buffer + 1) < 0)
            {
                syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
            }

            pthread_mutex_unlock(&text_file_lock);

            break; // Exit loop after handling one packet
        }
        else
        {
            pthread_mutex_lock(&text_file_lock);

            // Write the entire buffer if no newline is found
            if (write(element->text_fd, buffer, bytes_received) < 0)
            {
                syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
            }

            pthread_mutex_unlock(&text_file_lock);
        }
    }

    if (bytes_received < 0)
    {
        syslog(LOG_ERR, "Error receiving data: %s", strerror(errno));
    }

    // Send the contents of the file back to the client
    lseek(element->text_fd, 0, SEEK_SET);
    while (!element->thread_completed && (bytes_received = read(element->text_fd, buffer, BUFFER_SIZE)) > 0)
    {
        if (send(element->socket_fd, buffer, bytes_received, 0) < 0)
        {
            syslog(LOG_ERR, "Error sending data: %s", strerror(errno));
            break;
        }
    }

    if (element->text_fd != -1)
    {

        close(element->text_fd);
    }

    if (element->socket_fd != -1)
    {
        close(element->socket_fd);
    }

    element->thread_completed = true;

    return arg;
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

void timer_handler()
{
    time_t now;
    char timestamp[100];

    time(&now);
    struct tm *time_info = localtime(&now);

    // Format the timestamp according to RFC 2822
    strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);

    // Write the timestamp to the file
    pthread_mutex_lock(&text_file_lock);
    FILE *file = fopen(FILE_PATH, "a");
    if (file == NULL)
    {
        perror("Failed to open file for timestamp");
        pthread_mutex_unlock(&text_file_lock);
        return;
    }
    fputs(timestamp, file);
    fclose(file);
    pthread_mutex_unlock(&text_file_lock);
}

int main(int argc, char *argv[])
{
    openlog("aesdsocket", LOG_PID, LOG_USER);

    SLIST_INIT(&head);

    struct sigaction sa = {0};

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

    // Remove the file if exists
    remove(FILE_PATH);

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
    socklen_t client_len = 0;
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

    struct itimerspec ts = {0};
    struct sigevent se = {0};
    /*
     * Set the sigevent structure to cause the signal to be
     * delivered by creating a new thread.
     */
    se.sigev_notify = SIGEV_THREAD;
    se.sigev_value.sival_ptr = &timer_id;
    se.sigev_notify_function = timer_handler;
    se.sigev_notify_attributes = NULL;

    ts.it_value.tv_sec = 10;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 10;
    ts.it_interval.tv_nsec = 0;

    // spawn timestamp logger thread? or timer
    if (timer_create(CLOCK_REALTIME, &se, &timer_id) < 0)
        perror("Create timer");

    if (timer_settime(timer_id, 0, &ts, 0) < 0)
        perror("Set timer");

    if (listen(server_sockfd, 5) == -1)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Listening on port %d\n", SERVER_PORT);

    while (is_running)
    {
        client_len = sizeof(client_address);
        int client_sockfd = accept(
            server_sockfd,
            (struct sockaddr *)&client_address,
            &client_len);

        char *client_add = inet_ntoa(client_address.sin_addr);
        syslog(LOG_DEBUG, "Accepted connection from %s", client_add);
        printf("server: got connection from %s\n", client_add);

        struct slist_element *thread_element;
        thread_element = calloc(sizeof(struct slist_element), 1);
        thread_element->thread_completed = false;
        thread_element->socket_fd = client_sockfd;
        thread_element->text_fd = -1;
        SLIST_INSERT_HEAD(&head, thread_element, slist_elements);

        pthread_create(&thread_element->thread_id, NULL, handle_connection, thread_element);
    }

    timer_delete(timer_id);

    close_threads_res();

    close(server_sockfd);

    closelog();

    return EXIT_SUCCESS;
}
