#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <signal.h>
#include <mutex>
#include <termios.h>

#define MAX_LEN 512
#define NUM_COLORS 6
#define PORT 10000

using namespace std;

bool exit_flag = false;
thread t_send, t_recv;
int client_socket;
string def_col = "\033[0m";
string colors[] = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};
string bold = "\033[1m";
string clear_line = "\033[2K\r";
mutex cout_mtx;

void catch_ctrl_c(int signal);
string color(int code);
void send_message(int client_socket);
void recv_message(int client_socket);
void display_help();
void display_welcome();

int main() {
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(-1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    bzero(&server.sin_zero, 8);

    if (connect(client_socket, (struct sockaddr*)&server, sizeof(server)) == -1) {
        perror("connect");
        exit(-1);
    }

    signal(SIGINT, catch_ctrl_c);

    display_welcome();

    char name[MAX_LEN];
    cout << bold + colors[4] << "Enter your name: " << def_col;
    cin.getline(name, MAX_LEN);

    if (strlen(name) == 0) {
        strcpy(name, "Anonymous");
    }

    send(client_socket, name, strlen(name), 0);

    cout << bold + colors[NUM_COLORS-1] << "\n\t====== Welcome to Chat ======\n" << def_col;
    display_help();

    thread t1(send_message, client_socket);
    thread t2(recv_message, client_socket);

    t_send = move(t1);
    t_recv = move(t2);

    if (t_send.joinable())
        t_send.join();
    if (t_recv.joinable())
        t_recv.join();

    return 0;
}

void display_welcome() {
    cout << bold + colors[NUM_COLORS-1];
    cout << "\n";
    cout << "╔════════════════════════════════════╗\n";
    cout << "║       WELCOME TO CHAT ROOM         ║\n";
    cout << "║     Connecting to server...        ║\n";
    cout << "╚════════════════════════════════════╝\n";
    cout << def_col;
}

void display_help() {
    cout << bold + "\n--- Quick Help ---" << def_col << "\n";
    cout << "  #help   - Show all commands\n";
    cout << "  #users  - List active users\n";
    cout << "  #exit   - Leave chat\n";
    cout << "---\n\n";
}

void catch_ctrl_c(int signal) {
    char str[MAX_LEN] = "#exit";
    send(client_socket, str, strlen(str), 0);
    exit_flag = true;
    sleep(1);
    t_send.detach();
    t_recv.detach();
    close(client_socket);
    
    cout << "\n" << bold + colors[2] << "Goodbye!\n" << def_col;
    exit(0);
}

string color(int code) {
    return colors[code % NUM_COLORS];
}

void send_message(int client_socket) {
    while (!exit_flag) {
        cout << bold + colors[1] << "You: " << def_col;
        cout.flush();

        char str[MAX_LEN];
        if (!cin.getline(str, MAX_LEN)) {
            break;
        }

        if (strlen(str) == 0) continue;

        send(client_socket, str, strlen(str), 0);

        if (strcmp(str, "#exit") == 0) {
            exit_flag = true;
            sleep(1);
            t_recv.detach();
            close(client_socket);
            break;
        }
    }
}

void recv_message(int client_socket) {
    while (!exit_flag) {
        char buffer[MAX_LEN * 2];
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0) {
            if (!exit_flag) {
                cout << "\n" << bold + colors[2] << "Server disconnected.\n" << def_col;
                exit_flag = true;
            }
            break;
        }

        buffer[bytes_received] = '\0';
        string packet(buffer);

        // Check if this is a command response (contains newlines and no pipes, or starts with ---)
        if (packet.find('\n') != string::npos && packet.find('|') == string::npos) {
            // This is a server response (like #help or #users output)
            cout << clear_line;
            cout << bold + colors[NUM_COLORS-1] << packet << def_col;
            cout << bold + colors[1] << "You: " << def_col;
            cout.flush();
            continue;
        }

        // Parse packet: sender_id|sender_name|type|content
        size_t pos1 = packet.find('|');
        if (pos1 == string::npos) {
            // Treat as plain message if no pipes found
            cout << clear_line << packet << "\n";
            cout << bold + colors[1] << "You: " << def_col;
            cout.flush();
            continue;
        }

        size_t pos2 = packet.find('|', pos1 + 1);
        if (pos2 == string::npos) continue;

        size_t pos3 = packet.find('|', pos2 + 1);
        if (pos3 == string::npos) continue;

        string sender_id_str = packet.substr(0, pos1);
        string sender_name = packet.substr(pos1 + 1, pos2 - pos1 - 1);
        string msg_type = packet.substr(pos2 + 1, pos3 - pos2 - 1);
        string content = packet.substr(pos3 + 1);

        int sender_id = stoi(sender_id_str);

        cout << clear_line;
        
        if (msg_type == "message") {
            cout << color(sender_id) << "[" << sender_name << "] " << def_col << content << "\n";
        } else if (msg_type == "join") {
            cout << bold + colors[NUM_COLORS-1] << ">> " << sender_name << " joined the chat\n" << def_col;
        } else if (msg_type == "leave") {
            cout << bold + colors[2] << "<< " << sender_name << " left the chat\n" << def_col;
        }

        cout << bold + colors[1] << "You: " << def_col;
        cout.flush();
    }
}