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
#include <thread>
#include <atomic>
#include <boost/asio.hpp>

#pragma comment(lib, "ws2_32.lib")

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SERVER_IP = "127.0.0.1";
const char* DOWNLOAD_FOLDER = "C:\\UDP_Downloads";

// Атомарные флаги для управления потоками
std::atomic<bool> transferInProgress(false);
std::atomic<bool> downloadInProgress(false);

// Функция для отправки команды и получения ответа
std::string SendCommand(SOCKET socket, const std::string& command, sockaddr_in& serverAddr) {
    // Отправляем команду
    sendto(socket, command.c_str(), command.length(), 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr));

    // Ждем ответ с таймаутом
    char buffer[BUFFER_SIZE];
    int serverSize = sizeof(serverAddr);

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);

    struct timeval timeout;
    timeout.tv_sec = 10;  // 10 секунд таймаут
    timeout.tv_usec = 0;

    if (select(0, &readSet, NULL, NULL, &timeout) > 0) {
        int bytes = recvfrom(socket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&serverAddr, &serverSize);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            return std::string(buffer);
        }
    }

    return "ERROR:NO_RESPONSE";
}

// Асинхронная отправка файла
void AsyncSendFileToServer(SOCKET socket, sockaddr_in& serverAddr) {
    if (transferInProgress) {
        std::cout << "ERROR: Another transfer is in progress\n";
        return;
    }

    transferInProgress = true;

    std::thread([socket, &serverAddr]() {
        std::string filepath;
        std::cout << "\nEnter file path: ";
        std::getline(std::cin, filepath);

        // Проверяем существование файла
        DWORD attrib = GetFileAttributesA(filepath.c_str());
        if (attrib == INVALID_FILE_ATTRIBUTES || (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            std::cout << "ERROR: File not found\n";
            transferInProgress = false;
            return;
        }

        // Получаем имя файла
        std::string filename;
        size_t lastSlash = filepath.find_last_of("\\/");
        filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

        std::cout << "\nSending file: " << filename << " (async)" << std::endl;

        // Читаем файл
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cout << "ERROR: Cannot open file\n";
            transferInProgress = false;
            return;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Читаем файл в память
        std::vector<char> buffer(fileSize);
        if (!file.read(buffer.data(), fileSize)) {
            std::cout << "ERROR: Cannot read file\n";
            transferInProgress = false;
            return;
        }
        file.close();

        // Формируем команду с данными файла
        std::string filedata(buffer.begin(), buffer.end());
        std::string command = "SEND_FILE:" + filename + ":" + filedata;

        // Отправляем команду
        std::string response = SendCommand(socket, command, serverAddr);
        if (response != "READY") {
            std::cout << "ERROR: Server not ready: " << response << std::endl;
            transferInProgress = false;
            return;
        }

        std::cout << "File sent to server queue. Waiting for confirmation...\n";

        // Ждем финальный ответ
        while (true) {
            char responseBuffer[BUFFER_SIZE];
            int serverSize = sizeof(serverAddr);

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            if (select(0, &readSet, NULL, NULL, &timeout) > 0) {
                int bytes = recvfrom(socket, responseBuffer, BUFFER_SIZE, 0,
                    (sockaddr*)&serverAddr, &serverSize);

                if (bytes > 0) {
                    responseBuffer[bytes] = '\0';
                    std::string finalResponse(responseBuffer);

                    if (finalResponse.substr(0, 5) == "DONE:") {
                        std::cout << "\n" << finalResponse << std::endl;
                        break;
                    }
                    else if (finalResponse.substr(0, 5) == "ERROR") {
                        std::cout << "\n" << finalResponse << std::endl;
                        break;
                    }
                }
            }

            std::cout << "." << std::flush;
        }

        transferInProgress = false;
        std::cout << "File transfer completed.\n";
        }).detach();
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

// Асинхронное скачивание файла
void AsyncDownloadFile(SOCKET socket, sockaddr_in& serverAddr) {
    if (downloadInProgress) {
        std::cout << "ERROR: Another download is in progress\n";
        return;
    }

    downloadInProgress = true;

    std::thread([socket, &serverAddr]() {
        std::string filename;
        std::cout << "\nEnter filename to download: ";
        std::getline(std::cin, filename);

        if (filename.empty()) {
            std::cout << "ERROR: Filename cannot be empty\n";
            downloadInProgress = false;
            return;
        }

        // Отправляем команду GET
        std::string response = SendCommand(socket, "GET " + filename, serverAddr);

        if (response.substr(0, 5) == "ERROR") {
            std::cout << response << std::endl;
            downloadInProgress = false;
            return;
        }

        if (response.substr(0, 10) != "FILE_INFO:") {
            std::cout << "ERROR: Invalid response: " << response << std::endl;
            downloadInProgress = false;
            return;
        }

        // Получаем размер файла
        long long fileSize = std::stoll(response.substr(10));
        std::cout << "\nDownloading: " << filename << " (" << fileSize << " bytes) (async)" << std::endl;

        // Создаем папку для загрузок
        CreateDirectoryA(DOWNLOAD_FOLDER, NULL);
        std::string savePath = std::string(DOWNLOAD_FOLDER) + "\\" + filename;

        // Открываем файл для записи
        std::ofstream file(savePath, std::ios::binary);
        if (!file) {
            std::cout << "ERROR: Cannot create file\n";
            downloadInProgress = false;
            return;
        }

        // Получаем файл
        char buffer[BUFFER_SIZE];
        int serverSize = sizeof(serverAddr);
        long long totalReceived = 0;
        bool fileComplete = false;

        std::cout << "Waiting for file data...\n";

        while (totalReceived < fileSize && !fileComplete) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);

            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;

            if (select(0, &readSet, NULL, NULL, &timeout) > 0) {
                int bytes = recvfrom(socket, buffer, BUFFER_SIZE, 0,
                    (sockaddr*)&serverAddr, &serverSize);

                if (bytes <= 0) continue;

                // Проверяем конец файла
                if (bytes == 11 && strncmp(buffer, "END_OF_FILE", 11) == 0) {
                    fileComplete = true;
                    break;
                }

                file.write(buffer, bytes);
                totalReceived += bytes;

                // Показываем прогресс каждые 10%
                if (fileSize > 0 && totalReceived % (fileSize / 10 + 1) == 0) {
                    int percent = static_cast<int>((totalReceived * 100) / fileSize);
                    std::cout << "\rProgress: " << percent << "% ("
                        << totalReceived << "/" << fileSize << " bytes)";
                }
            }
            else {
                // Таймаут - проверяем, не завершилась ли передача
                if (totalReceived >= fileSize) {
                    fileComplete = true;
                }
            }
        }

        file.close();

        if (fileComplete) {
            std::cout << "\nFile saved: " << savePath << " ("
                << totalReceived << " bytes)" << std::endl;
        }
        else {
            std::cout << "\nDownload incomplete. Received: "
                << totalReceived << "/" << fileSize << " bytes" << std::endl;
        }

        downloadInProgress = false;
        }).detach();
}

// Функция для проверки состояния асинхронных операций
void CheckAsyncStatus() {
    std::cout << "\n=== Async Operations Status ===\n";
    std::cout << "File transfer in progress: " << (transferInProgress ? "YES" : "NO") << std::endl;
    std::cout << "File download in progress: " << (downloadInProgress ? "YES" : "NO") << std::endl;
    std::cout << "================================\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   ASYNC UDP FILE CLIENT\n";
    std::cout << "   Boost.Asio available for networking\n";
    std::cout << "========================================\n\n";

    std::cout << "Features:\n";
    std::cout << "- Async file transfers\n";
    std::cout << "- Non-blocking operations\n";
    std::cout << "- Progress tracking\n";
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

    // Делаем сокет неблокирующим
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

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
        std::cout << "1. Send file to server (async)\n";
        std::cout << "2. View files on server\n";
        std::cout << "3. Download file from server (async)\n";
        std::cout << "4. Check async status\n";
        std::cout << "5. Exit\n";
        std::cout << "Choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
        case 1:
            AsyncSendFileToServer(clientSocket, serverAddr);
            break;
        case 2:
            GetFileList(clientSocket, serverAddr);
            break;
        case 3:
            AsyncDownloadFile(clientSocket, serverAddr);
            break;
        case 4:
            CheckAsyncStatus();
            break;
        case 5:
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

        if (choice != 4) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
        }
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}