// ===== SERVER.CPP =====
#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <ctime>
#include <chrono>

#define MAX_LEN 512
#define NUM_COLORS 6
#define MAX_CLIENTS 50
#define PORT 10000

using namespace std;

struct Message {
    int sender_id;
    string sender_name;
    string content;
    chrono::system_clock::time_point timestamp;
    string type; // "message", "join", "leave", "admin"
};

struct Client {
    int id;
    string name;
    string ip;
    int socket;
    thread th;
    chrono::system_clock::time_point join_time;
};

vector<Client> clients;
vector<Message> message_history;
string def_col = "\033[0m";
string colors[] = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};
string bold = "\033[1m";
int seed = 0;
mutex cout_mtx, clients_mtx, history_mtx;
const int MAX_HISTORY = 100;

string color(int code);
string get_timestamp();
void set_name(int id, const char name[]);
void shared_print(string str, bool endLine = true);
void broadcast_message(const Message& msg);
void end_connection(int id);
void handle_client(int client_socket, int id, const string& ip);
void display_user_list();
void save_message(const Message& msg);
void send_history(int client_socket, int limit = 20);

int main() {
    int server_socket;
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(-1);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(-1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;
    bzero(&server.sin_zero, 8);

    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == -1) {
        perror("bind");
        exit(-1);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("listen");
        exit(-1);
    }

    shared_print(bold + colors[NUM_COLORS-1] + "\n\t  ====== Chat Server Started ====== " + def_col);
    shared_print("Server listening on port " + to_string(PORT));

    struct sockaddr_in client;
    int client_socket;
    unsigned int len = sizeof(sockaddr_in);

    while (1) {
        if ((client_socket = accept(server_socket, (struct sockaddr*)&client, &len)) == -1) {
            perror("accept");
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, ip_str, INET_ADDRSTRLEN);
        
        seed++;
        thread t(handle_client, client_socket, seed, string(ip_str));
        
        {
            lock_guard<mutex> guard(clients_mtx);
            if (clients.size() < MAX_CLIENTS) {
                clients.push_back({seed, "Anonymous", string(ip_str), client_socket, move(t), chrono::system_clock::now()});
            }
        }
    }

    close(server_socket);
    return 0;
}

string color(int code) {
    return colors[code % NUM_COLORS];
}

string get_timestamp() {
    auto now = chrono::system_clock::now();
    time_t time = chrono::system_clock::to_time_t(now);
    stringstream ss;
    ss << put_time(localtime(&time), "%H:%M:%S");
    return ss.str();
}

void set_name(int id, const char name[]) {
    lock_guard<mutex> guard(clients_mtx);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i].id == id) {
            clients[i].name = string(name);
            break;
        }
    }
}

void shared_print(string str, bool endLine) {
    lock_guard<mutex> guard(cout_mtx);
    cout << str;
    if (endLine) cout << endl;
}

void display_user_list() {
    lock_guard<mutex> guard(clients_mtx);
    shared_print(bold + "\n--- Active Users (" + to_string(clients.size()) + ") ---" + def_col);
    for (int i = 0; i < clients.size(); i++) {
        shared_print("  " + to_string(i+1) + ". " + clients[i].name + " (" + clients[i].ip + ")");
    }
    shared_print("---\n");
}

void save_message(const Message& msg) {
    lock_guard<mutex> guard(history_mtx);
    message_history.push_back(msg);
    if (message_history.size() > MAX_HISTORY) {
        message_history.erase(message_history.begin());
    }
}

void send_history(int client_socket, int limit) {
    lock_guard<mutex> guard(history_mtx);
    int start = max(0, (int)message_history.size() - limit);
    
    for (int i = start; i < message_history.size(); i++) {
        const Message& msg = message_history[i];
        string line = "[" + get_timestamp() + "] " + msg.sender_name + ": " + msg.content + "\n";
        send(client_socket, line.c_str(), line.length(), 0);
    }
}

void broadcast_message(const Message& msg) {
    save_message(msg);
    
    string display;
    if (msg.type == "message") {
        display = color(msg.sender_id) + "[" + get_timestamp() + "] " + bold + msg.sender_name + def_col + ": " + msg.content;
    } else if (msg.type == "join") {
        display = bold + colors[NUM_COLORS-1] + "[" + get_timestamp() + "] >> " + msg.sender_name + " has joined the chat" + def_col;
    } else if (msg.type == "leave") {
        display = bold + colors[2] + "[" + get_timestamp() + "] << " + msg.sender_name + " has left the chat" + def_col;
    } else {
        display = color(msg.sender_id) + "[" + get_timestamp() + "] " + msg.content;
    }
    
    shared_print(display);

    lock_guard<mutex> guard(clients_mtx);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i].id != msg.sender_id) {
            string packet = to_string(msg.sender_id) + "|" + msg.sender_name + "|" + msg.type + "|" + msg.content;
            send(clients[i].socket, packet.c_str(), packet.length() + 1, 0);
        }
    }
}

void end_connection(int id) {
    lock_guard<mutex> guard(clients_mtx);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i].id == id) {
            if (clients[i].th.joinable()) {
                clients[i].th.detach();
            }
            close(clients[i].socket);
            clients.erase(clients.begin() + i);
            break;
        }
    }
}

void handle_client(int client_socket, int id, const string& ip) {
    char name[MAX_LEN];
    int bytes = recv(client_socket, name, MAX_LEN - 1, 0);
    if (bytes <= 0) {
        end_connection(id);
        return;
    }
    name[bytes] = '\0';
    
    // Sanitize name - remove newlines
    for (int i = 0; i < strlen(name); i++) {
        if (name[i] == '\n' || name[i] == '\r') {
            name[i] = '\0';
            break;
        }
    }
    
    if (strlen(name) == 0) {
        strcpy(name, "User");
    }
    
    set_name(id, name);

    Message join_msg;
    join_msg.sender_id = id;
    join_msg.sender_name = string(name);
    join_msg.type = "join";
    join_msg.content = "";
    broadcast_message(join_msg);
    
    display_user_list();

    while (1) {
        char buffer[MAX_LEN];
        int bytes_received = recv(client_socket, buffer, MAX_LEN - 1, 0);
        
        if (bytes_received <= 0) break;
        
        buffer[bytes_received] = '\0';
        string input(buffer);
        
        // Remove newlines and carriage returns
        input.erase(remove(input.begin(), input.end(), '\n'), input.end());
        input.erase(remove(input.begin(), input.end(), '\r'), input.end());
        
        // Trim whitespace
        input.erase(0, input.find_first_not_of(" \t"));
        if (input.find_last_not_of(" \t") != string::npos) {
            input.erase(input.find_last_not_of(" \t") + 1);
        }

        if (input == "#exit") {
            Message leave_msg;
            leave_msg.sender_id = id;
            leave_msg.sender_name = string(name);
            leave_msg.type = "leave";
            leave_msg.content = "";
            broadcast_message(leave_msg);
            
            end_connection(id);
            break;
        } else if (input == "#users") {
            display_user_list();
            // Send user list to client
            string response = "--- Active Users (" + to_string(clients.size()) + ") ---\n";
            {
                lock_guard<mutex> guard(clients_mtx);
                for (int i = 0; i < clients.size(); i++) {
                    response += "  " + to_string(i+1) + ". " + clients[i].name + " (" + clients[i].ip + ")\n";
                }
            }
            response += "---\n";
            send(client_socket, response.c_str(), response.length(), 0);
        } else if (input == "#help") {
            string response = "\n--- Commands ---\n";
            response += "  #users - Show active users\n";
            response += "  #help - Show this help\n";
            response += "  #exit - Disconnect from chat\n";
            response += "---\n";
            send(client_socket, response.c_str(), response.length(), 0);
            shared_print(bold + response + def_col);
        } else if (input.length() > 0) {
            Message msg;
            msg.sender_id = id;
            msg.sender_name = string(name);
            msg.type = "message";
            msg.content = input;
            broadcast_message(msg);
        }
    }
}
