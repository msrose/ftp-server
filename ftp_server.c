#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

#define BUFFER_SIZE 1024
#define COMMAND_PORT 2121
#define DATA_PORT 2120

typedef struct {
    int control_socket;
    int data_socket;
    char current_dir[256];
    char root_dir[256];
} ftp_session;

void send_response(int socket, const char *response) {
    write(socket, response, strlen(response));
}

void handle_list(ftp_session *session) {
    if (session->data_socket <= 0) {
        send_response(session->control_socket, "425 Use PASV first.\r\n");
        return;
    }
    
    // Use existing data socket from PASV
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int data_socket = accept(session->data_socket, (struct sockaddr*)&client_addr, &client_len);
    close(session->data_socket);
    session->data_socket = -1;
    
    if (data_socket < 0) {
        send_response(session->control_socket, "425 Can't open data connection.\r\n");
        return;
    }
    
    send_response(session->control_socket, "150 File status okay; about to open data connection.\r\n");
    
    DIR *dir = opendir(session->current_dir);
    if (!dir) {
        send_response(session->control_socket, "550 Failed to open directory.\r\n");
        close(data_socket);
        return;
    }
    
    struct dirent *entry;
    char buffer[BUFFER_SIZE];
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", session->current_dir, entry->d_name);
        
        if (stat(full_path, &st) == 0) {
            char *type = S_ISDIR(st.st_mode) ? "d" : "-";
            
            // Calculate actual permissions
            char perms[10];
            perms[0] = (st.st_mode & S_IRUSR) ? 'r' : '-';
            perms[1] = (st.st_mode & S_IWUSR) ? 'w' : '-';
            perms[2] = (st.st_mode & S_IXUSR) ? 'x' : '-';
            perms[3] = (st.st_mode & S_IRGRP) ? 'r' : '-';
            perms[4] = (st.st_mode & S_IWGRP) ? 'w' : '-';
            perms[5] = (st.st_mode & S_IXGRP) ? 'x' : '-';
            perms[6] = (st.st_mode & S_IROTH) ? 'r' : '-';
            perms[7] = (st.st_mode & S_IWOTH) ? 'w' : '-';
            perms[8] = (st.st_mode & S_IXOTH) ? 'x' : '-';
            perms[9] = '\0';
            
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%b %d %H:%M", localtime(&st.st_mtime));
            
            snprintf(buffer, sizeof(buffer), "%s%s 1 %s %s %8ld %s %s\r\n",
                    type, perms, 
                    getpwuid(st.st_uid) ? getpwuid(st.st_uid)->pw_name : "unknown",
                    getgrgid(st.st_gid) ? getgrgid(st.st_gid)->gr_name : "unknown",
                    (long)st.st_size, time_str, entry->d_name);
            write(data_socket, buffer, strlen(buffer));
        }
    }
    
    closedir(dir);
    close(data_socket);
    send_response(session->control_socket, "226 Transfer complete.\r\n");
}

void handle_retr(ftp_session *session, const char *filename) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", session->current_dir, filename);
    
    int file_fd = open(full_path, O_RDONLY);
    if (file_fd < 0) {
        send_response(session->control_socket, "550 File not found.\r\n");
        return;
    }
    
    if (session->data_socket <= 0) {
        close(file_fd);
        send_response(session->control_socket, "425 Use PASV first.\r\n");
        return;
    }
    
    // Use existing data socket from PASV
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int data_socket = accept(session->data_socket, (struct sockaddr*)&client_addr, &client_len);
    close(session->data_socket);
    session->data_socket = -1;
    
    if (data_socket < 0) {
        close(file_fd);
        send_response(session->control_socket, "425 Can't open data connection.\r\n");
        return;
    }
    
    send_response(session->control_socket, "150 File status okay; about to open data connection.\r\n");
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        write(data_socket, buffer, bytes_read);
    }
    
    close(file_fd);
    close(data_socket);
    send_response(session->control_socket, "226 Transfer complete.\r\n");
}

void handle_cwd(ftp_session *session, const char *path) {
    char new_path[512];
    if (path[0] == '/') {
        snprintf(new_path, sizeof(new_path), "%s%s", session->root_dir, path);
    } else {
        snprintf(new_path, sizeof(new_path), "%s/%s", session->current_dir, path);
    }
    
    // Get the resolved root directory for comparison
    char resolved_root[512];
    if (realpath(session->root_dir, resolved_root) == NULL) {
        send_response(session->control_socket, "550 Internal error.\r\n");
        return;
    }
    
    // Now resolve the path to prevent directory traversal
    char resolved_path[512];
    if (realpath(new_path, resolved_path) != NULL) {
        send_response(session->control_socket, "550 Directory not found.\r\n");
        return;
    }
    
    // Check if the resolved path is within the resolved root directory
    if (strncmp(resolved_path, resolved_root, strlen(resolved_root)) != 0) {
        send_response(session->control_socket, "550 Access denied.\r\n");
        return;
    }
    
    // First check if the directory exists
    DIR *dir = opendir(new_path);
    if (!dir) {
        send_response(session->control_socket, "550 Directory not found.\r\n");
        return;
    }
    closedir(dir);
    
    strcpy(session->current_dir, resolved_path);
    send_response(session->control_socket, "250 Directory changed.\r\n");
}

void handle_pwd(ftp_session *session) {
    // Get the resolved root directory for comparison
    char resolved_root[512];
    if (realpath(session->root_dir, resolved_root) == NULL) {
        send_response(session->control_socket, "550 Internal error.\r\n");
        return;
    }
    
    char relative_path[512];
    if (strncmp(session->current_dir, resolved_root, strlen(resolved_root)) == 0) {
        strcpy(relative_path, session->current_dir + strlen(resolved_root));
        if (strlen(relative_path) == 0) strcpy(relative_path, "/");
    } else {
        strcpy(relative_path, "/");
    }
    
    char response[512];
    snprintf(response, sizeof(response), "257 \"%s\" is current directory.\r\n", relative_path);
    send_response(session->control_socket, response);
}

void handle_command(ftp_session *session, const char *command) {
    char cmd[64], arg[256];
    sscanf(command, "%63s %255s", cmd, arg);
    
    if (strcmp(cmd, "QUIT") == 0) {
        send_response(session->control_socket, "221 Goodbye.\r\n");
        return;
    } else if (strcmp(cmd, "USER") == 0) {
        send_response(session->control_socket, "331 User name okay, need password.\r\n");
    } else if (strcmp(cmd, "PASS") == 0) {
        send_response(session->control_socket, "230 User logged in.\r\n");
    } else if (strcmp(cmd, "SYST") == 0) {
        send_response(session->control_socket, "215 UNIX Type: L8\r\n");
    } else if (strcmp(cmd, "TYPE") == 0) {
        send_response(session->control_socket, "200 Type set to I.\r\n");
    } else if (strcmp(cmd, "PASV") == 0) {
        // Create data socket and listen immediately
        int data_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (data_socket < 0) {
            send_response(session->control_socket, "425 Can't open data connection.\r\n");
            return;
        }
        
        int opt = 1;
        setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(DATA_PORT);
        
        if (bind(data_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(data_socket);
            send_response(session->control_socket, "425 Can't open data connection.\r\n");
            return;
        }
        
        if (listen(data_socket, 1) < 0) {
            close(data_socket);
            send_response(session->control_socket, "425 Can't open data connection.\r\n");
            return;
        }
        
        session->data_socket = data_socket;
        send_response(session->control_socket, "227 Entering Passive Mode (127,0,0,1,8,72).\r\n");
    } else if (strcmp(cmd, "LIST") == 0) {
        handle_list(session);
    } else if (strcmp(cmd, "RETR") == 0) {
        handle_retr(session, arg);
    } else if (strcmp(cmd, "CWD") == 0) {
        handle_cwd(session, arg);
    } else if (strcmp(cmd, "PWD") == 0) {
        handle_pwd(session);
    } else if (strcmp(cmd, "CDUP") == 0) {
        handle_cwd(session, "..");
    } else {
        printf("Unknown command: %s\n", cmd);
        send_response(session->control_socket, "500 Unknown command.\r\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }
    
    DIR *root_dir = opendir(argv[1]);
    if (!root_dir) {
        fprintf(stderr, "Error: Cannot open directory %s\n", argv[1]);
        return 1;
    }
    closedir(root_dir);
    
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(COMMAND_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("FTP server started on port %d, serving directory: %s\n", COMMAND_PORT, argv[1]);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        ftp_session session;
        session.control_socket = client_socket;
        session.data_socket = -1;
        strcpy(session.root_dir, argv[1]);
        strcpy(session.current_dir, argv[1]);
        
        send_response(client_socket, "220 FTP server ready.\r\n");
        
        char buffer[BUFFER_SIZE];
        while (1) {
            ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0) break;
            
            buffer[bytes_read] = '\0';
            char *newline = strchr(buffer, '\r');
            if (newline) *newline = '\0';
            newline = strchr(buffer, '\n');
            if (newline) *newline = '\0';
            
            printf("Command: %s\n", buffer);
            handle_command(&session, buffer);
        }
        
        close(client_socket);
        printf("Client disconnected\n");
    }
    
    close(server_socket);
    return 0;
} 