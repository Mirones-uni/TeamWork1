#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

const size_t max_length = 1024;

// Убираем конфликт с макросами Windows
#undef min
#undef max

void send_file(SOCKET socket, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open file");
    }

    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    uint32_t size_network_order = htonl(static_cast<uint32_t>(file_size));
    int bytes_sent = send(socket, reinterpret_cast<const char*>(&size_network_order), 4, 0);
    if (bytes_sent != 4) {
        throw std::runtime_error("Failed to send file size");
    }

    char filename_buffer[64] = {0};
    strncpy_s(filename_buffer, filename.c_str(), 63); // Безопасная версия strncpy
    bytes_sent = send(socket, filename_buffer, 64, 0);
    if (bytes_sent != 64) {
        throw std::runtime_error("Failed to send filename");
    }

    char data[max_length];
    while (size_t bytes_read = file.readsome(data, max_length)) {
        bytes_sent = send(socket, data, static_cast<int>(bytes_read), 0);
        if (bytes_sent != static_cast<int>(bytes_read)) {
            throw std::runtime_error("Failed to send file data");
        }
    }

    std::cout << "Sent file: " << filename << " (" << file_size << " bytes)" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: client <host> <filename>\n";
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket\n";
        WSACleanup();
        return 1;
    }

    // Устанавливаем сокет в неблокирующий режим
    u_long mode = 1;
    if (ioctlsocket(client_socket, FIONBIO, &mode) != 0) {
        std::cerr << "Failed to set non-blocking mode\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(12345);

    if (inet_pton(AF_INET, argv[1], &server_address.sin_addr) <= 0) {
        std::cerr << "Invalid address/Address not supported\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // Пытаемся подключиться
    int result = connect(client_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address));
    if (result == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            std::cerr << "Connection failed\n";
            closesocket(client_socket);
            WSACleanup();
            return 1;
        }
    }

    // Используем select для ожидания подключения
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(client_socket, &write_fds);

    timeval timeout;
    timeout.tv_sec = 5; // 5 секунд таймаут
    timeout.tv_usec = 0;

    int select_result = select(0, nullptr, &write_fds, nullptr, &timeout);
    if (select_result == SOCKET_ERROR) {
        std::cerr << "Select failed\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    } else if (select_result == 0) {
        std::cerr << "Connection timed out\n";
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    // Подключение успешно
    std::cout << "Connected to server\n";

    try {
        send_file(client_socket, argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}