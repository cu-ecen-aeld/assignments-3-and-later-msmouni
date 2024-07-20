#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Invalid Arguments.\nUsage: %s <file_name> <content_to_write>", argv[0]);
        return 1;
    }

    openlog("c_writer", LOG_PID, LOG_USER);

    const char *file_path = argv[1];
    const char *file_content = argv[2];

    FILE *file = fopen(file_path, "w");

    if (!file)
    {
        char *err = strerror(errno);
        syslog(LOG_ERR, "Error opening file %s: %s", file_path, err);
        fprintf(stderr, "Error opening file %s: %s", file_path, err);
        closelog();
        return 1;
    }

    if (fprintf(file, "%s", file_content) < 0)
    {
        char *err = strerror(errno);
        syslog(LOG_ERR, "Error writting to file %s: %s", file_path, err);
        fprintf(stderr, "Error writting to file %s: %s", file_path, err);
        fclose(file);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing \"%s\" to \"%s\"", file_content, file_path);

    if (fclose(file) != 0)
    {
        char *err = strerror(errno);
        syslog(LOG_ERR, "Error closing the file %s: %s", file_path, err);
        fprintf(stderr, "Error closing the file %s: %s", file_path, err);
        closelog();
        return 1;
    }

    return 0;
}