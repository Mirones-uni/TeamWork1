#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>

#pragma comment(lib, "ws2_32.lib")

const size_t max_length = 1024;

// Убираем конфликт с макросами Windows
#undef min
#undef max

std::mutex clients_mutex;
std::vector<SOCKET> clients;

void handle_session(SOCKET client_socket) {
    try {
        char size_buffer[4];
        int bytes_received = recv(client_socket, size_buffer, 4, 0);
        if (bytes_received != 4) {
            throw std::runtime_error("Failed to read file size");
        }
        uint32_t file_size = ntohl(*reinterpret_cast<uint32_t*>(size_buffer));

        char filename_buffer[64];
        bytes_received = recv(client_socket, filename_buffer, 64, 0);
        if (bytes_received != 64) {
            throw std::runtime_error("Failed to read filename");
        }
        std::string filename(filename_buffer, strnlen(filename_buffer, 64));

        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open file");
        }

        size_t total_bytes_received = 0;
        while (total_bytes_received < file_size) {
            char data[max_length];
            size_t chunk_size = (max_length < file_size - total_bytes_received) ? max_length : (file_size - total_bytes_received);
            bytes_received = recv(client_socket, data, static_cast<int>(chunk_size), 0);
            if (bytes_received <= 0) {
                throw std::runtime_error("Failed to read file data");
            }
            file.write(data, bytes_received);
            total_bytes_received += bytes_received;
        }

        file.close();
        std::cout << "Received file: " << filename << " (" << file_size << " bytes)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
    closesocket(client_socket);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        WSACleanup();
        return 1;
    }

    // Устанавливаем сокет в неблокирующий режим
    u_long mode = 1;
    if (ioctlsocket(server_socket, FIONBIO, &mode) != 0) {
        std::cerr << "Failed to set non-blocking mode\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(12345);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind socket\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Failed to listen on socket\n";
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port 12345\n";

    fd_set read_fds;
    timeval timeout;

    for (;;) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);

        timeout.tv_sec = 1; // 1 секунда таймаут
        timeout.tv_usec = 0;

        int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (select_result == SOCKET_ERROR) {
            std::cerr << "Select failed\n";
            break;
        } else if (select_result > 0) {
            if (FD_ISSET(server_socket, &read_fds)) {
                sockaddr_in client_address;
                int client_address_len = sizeof(client_address);
                SOCKET client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_address), &client_address_len);
                if (client_socket != INVALID_SOCKET) {
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        clients.push_back(client_socket);
                    }
                    std::thread(handle_session, client_socket).detach();
                }
            }
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}