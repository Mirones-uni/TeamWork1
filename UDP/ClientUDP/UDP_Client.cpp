#define _WIN32_WINNT 0x0601
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <vector>
#include <sstream>
#include <boost/asio.hpp>


#pragma comment(lib, "ws2_32.lib")

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SERVER_IP = "127.0.0.1";
const char* DOWNLOAD_FOLDER = "C:\\UDP_Downloads";

// Функция для отправки команды и получения ответа
std::string SendCommand(SOCKET socket, const std::string& command, sockaddr_in& serverAddr) {
    // Отправляем команду
    sendto(socket, command.c_str(), command.length(), 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr));

    // Ждем ответ
    char buffer[BUFFER_SIZE];
    int serverSize = sizeof(serverAddr);
    int bytes = recvfrom(socket, buffer, BUFFER_SIZE, 0,
        (sockaddr*)&serverAddr, &serverSize);

    if (bytes > 0) {
        buffer[bytes] = '\0';
        return std::string(buffer);
    }

    return "ERROR:NO_RESPONSE";
}

// Функция для отправки файла
void SendFileToServer(SOCKET socket, sockaddr_in& serverAddr) {
    std::string filepath;
    std::cout << "\nEnter file path: ";
    std::getline(std::cin, filepath);

    // Проверяем существование файла
    DWORD attrib = GetFileAttributesA(filepath.c_str());
    if (attrib == INVALID_FILE_ATTRIBUTES || (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::cout << "ERROR: File not found\n";
        return;
    }

    // Получаем имя файла
    std::string filename;
    size_t lastSlash = filepath.find_last_of("\\/");
    filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

    std::cout << "\nSending file: " << filename << std::endl;

    // Отправляем команду SEND
    std::string response = SendCommand(socket, "SEND " + filename, serverAddr);
    if (response != "READY") {
        std::cout << "ERROR: Server not ready: " << response << std::endl;
        return;
    }

    // Открываем файл
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cout << "ERROR: Cannot open file\n";
        return;
    }

    // Отправляем файл по частям
    char buffer[BUFFER_SIZE];
    int totalSent = 0;

    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0) {
            sendto(socket, buffer, bytesRead, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            totalSent += bytesRead;

            // Ждем подтверждение
            char ack[10];
            int serverSize = sizeof(serverAddr);
            recvfrom(socket, ack, sizeof(ack), 0,
                (sockaddr*)&serverAddr, &serverSize);

            std::cout << "\rSent: " << totalSent << " bytes";
        }
    }
    file.close();

    // Отправляем сигнал конца
    sendto(socket, "END", 3, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

    // Ждем финальный ответ
    response = SendCommand(socket, "", serverAddr);
    std::cout << "\n" << response << std::endl;
}

// Функция для получения списка файлов
void GetFileList(SOCKET socket, sockaddr_in& serverAddr) {
    std::string response = SendCommand(socket, "LIST", serverAddr);

    if (response.substr(0, 12) == "FILES_COUNT:") {
        std::stringstream ss(response.substr(12));
        std::string token;

        // Получаем количество файлов
        std::getline(ss, token, ':');
        int fileCount = std::stoi(token);

        std::cout << "\nFiles on server (" << fileCount << "):\n";
        std::cout << "====================\n";

        // Выводим список файлов
        for (int i = 1; i <= fileCount && std::getline(ss, token, ':'); i++) {
            std::cout << i << ". " << token << std::endl;
        }

        if (fileCount == 0) {
            std::cout << "No files found\n";
        }
    }
    else {
        std::cout << "ERROR: " << response << std::endl;
    }
}

// Функция для скачивания файла
void DownloadFile(SOCKET socket, sockaddr_in& serverAddr) {
    std::string filename;
    std::cout << "\nEnter filename to download: ";
    std::getline(std::cin, filename);

    if (filename.empty()) {
        std::cout << "ERROR: Filename cannot be empty\n";
        return;
    }

    // Отправляем команду GET
    std::string response = SendCommand(socket, "GET " + filename, serverAddr);

    if (response.substr(0, 5) == "ERROR") {
        std::cout << response << std::endl;
        return;
    }

    if (response.substr(0, 10) != "FILE_INFO:") {
        std::cout << "ERROR: Invalid response: " << response << std::endl;
        return;
    }

    // Получаем размер файла
    long long fileSize = std::stoll(response.substr(10));
    std::cout << "\nDownloading: " << filename << " (" << fileSize << " bytes)" << std::endl;

    // Отправляем подтверждение
    sendto(socket, "READY", 5, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

    // Создаем папку для загрузок
    CreateDirectoryA(DOWNLOAD_FOLDER, NULL);
    std::string savePath = std::string(DOWNLOAD_FOLDER) + "\\" + filename;

    // Открываем файл для записи
    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        std::cout << "ERROR: Cannot create file\n";
        return;
    }

    // Получаем файл
    char buffer[BUFFER_SIZE];
    int serverSize = sizeof(serverAddr);
    long long totalReceived = 0;

    while (totalReceived < fileSize) {
        int bytes = recvfrom(socket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&serverAddr, &serverSize);

        if (bytes <= 0) break;

        // Проверяем конец файла
        if (bytes == 11 && strncmp(buffer, "END_OF_FILE", 11) == 0) {
            break;
        }

        file.write(buffer, bytes);
        totalReceived += bytes;

        // Отправляем подтверждение
        sendto(socket, "OK", 2, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

        std::cout << "\rReceived: " << totalReceived << "/" << fileSize << " bytes";
    }

    file.close();
    std::cout << "\nFile saved: " << savePath << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   SIMPLE UDP FILE CLIENT\n";
    std::cout << "========================================\n\n";

    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "ERROR: Cannot initialize Winsock\n";
        system("pause");
        return 1;
    }

    // Создание сокета
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        std::cout << "ERROR: Cannot create socket\n";
        WSACleanup();
        system("pause");
        return 1;
    }

    // Устанавливаем таймаут
    int timeout = 5000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // Настройка адреса сервера
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    std::cout << "Connected to server: " << SERVER_IP << ":" << PORT << std::endl;

    // Тест соединения
    std::string response = SendCommand(clientSocket, "TEST", serverAddr);
    if (response != "SERVER_READY") {
        std::cout << "WARNING: Server not responding properly\n";
    }

    std::cout << "\n========================================\n";

    while (true) {
        std::cout << "\nMENU:\n";
        std::cout << "1. Send file to server\n";
        std::cout << "2. View files on server\n";
        std::cout << "3. Download file from server\n";
        std::cout << "4. Exit\n";
        std::cout << "Choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
        case 1:
            SendFileToServer(clientSocket, serverAddr);
            break;
        case 2:
            GetFileList(clientSocket, serverAddr);
            break;
        case 3:
            DownloadFile(clientSocket, serverAddr);
            break;
        case 4:
            std::cout << "\nGoodbye!\n";
            closesocket(clientSocket);
            WSACleanup();
            std::cout << "\nPress Enter to exit...";
            std::cin.ignore();
            std::cin.get();
            return 0;
        default:
            std::cout << "ERROR: Invalid choice\n";
        }

        std::cout << "\nPress Enter to continue...";
        std::cin.ignore();
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}