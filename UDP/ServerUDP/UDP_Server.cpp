#define _WIN32_WINNT 0x0601
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <vector>
#include <algorithm>


#pragma comment(lib, "ws2_32.lib")

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SAVE_FOLDER = "C:\\UDP_Files";

// Функция для отправки ответа клиенту
void SendResponse(SOCKET socket, const std::string& response, sockaddr_in clientAddr) {
    sendto(socket, response.c_str(), response.length(), 0,
        (sockaddr*)&clientAddr, sizeof(clientAddr));
}

// Функция для сохранения файла
void SaveFile(SOCKET socket, const std::string& filename, sockaddr_in clientAddr) {
    // Создаем папку если её нет
    CreateDirectoryA(SAVE_FOLDER, NULL);

    // Создаем уникальное имя файла
    std::string filepath = std::string(SAVE_FOLDER) + "\\" + filename;
    int counter = 1;

    while (GetFileAttributesA(filepath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        size_t dot = filename.find_last_of('.');
        std::string name = filename.substr(0, dot);
        std::string ext = (dot != std::string::npos) ? filename.substr(dot) : "";
        filepath = std::string(SAVE_FOLDER) + "\\" + name + "_" + std::to_string(counter) + ext;
        counter++;
    }

    // Отправляем подтверждение
    SendResponse(socket, "READY", clientAddr);

    // Открываем файл для записи
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        SendResponse(socket, "ERROR:CANNOT_CREATE_FILE", clientAddr);
        return;
    }

    std::cout << "Receiving file: " << filename << " -> " << filepath << std::endl;

    // Принимаем данные
    char buffer[BUFFER_SIZE];
    int clientSize = sizeof(clientAddr);
    int totalBytes = 0;

    while (true) {
        int bytes = recvfrom(socket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&clientAddr, &clientSize);

        if (bytes <= 0) break;

        // Проверяем конец передачи
        if (bytes == 3 && strncmp(buffer, "END", 3) == 0) {
            break;
        }

        // Записываем данные
        file.write(buffer, bytes);
        totalBytes += bytes;

        // Отправляем подтверждение
        SendResponse(socket, "OK", clientAddr);

        // Прогресс
        std::cout << "\rReceived: " << totalBytes << " bytes";
    }

    file.close();
    std::cout << "\nFile saved: " << totalBytes << " bytes" << std::endl;
    SendResponse(socket, "DONE:" + std::to_string(totalBytes), clientAddr);
}

// Функция для отправки файла клиенту
void SendFile(SOCKET socket, const std::string& filename, sockaddr_in clientAddr) {
    std::string filepath = std::string(SAVE_FOLDER) + "\\" + filename;

    // Проверяем существование файла
    if (GetFileAttributesA(filepath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SendResponse(socket, "ERROR:FILE_NOT_FOUND", clientAddr);
        return;
    }

    // Открываем файл для чтения
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        SendResponse(socket, "ERROR:CANNOT_OPEN_FILE", clientAddr);
        return;
    }

    // Получаем размер файла
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Отправляем информацию о файле
    SendResponse(socket, "FILE_INFO:" + std::to_string(fileSize), clientAddr);

    // Ждем подтверждение
    char ack[10];
    int clientSize = sizeof(clientAddr);
    recvfrom(socket, ack, sizeof(ack), 0, (sockaddr*)&clientAddr, &clientSize);

    std::cout << "Sending file: " << filename << " (" << fileSize << " bytes)" << std::endl;

    // Отправляем файл по частям
    char buffer[BUFFER_SIZE];
    int totalSent = 0;

    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0) {
            sendto(socket, buffer, bytesRead, 0,
                (sockaddr*)&clientAddr, sizeof(clientAddr));
            totalSent += bytesRead;

            // Ждем подтверждение
            recvfrom(socket, ack, sizeof(ack), 0, (sockaddr*)&clientAddr, &clientSize);

            // Прогресс
            std::cout << "\rSent: " << totalSent << "/" << fileSize << " bytes";
        }
    }

    file.close();

    // Отправляем сигнал конца
    SendResponse(socket, "END_OF_FILE", clientAddr);
    std::cout << "\nFile sent successfully" << std::endl;
}

// Функция для получения списка файлов
std::vector<std::string> GetFileList() {
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    std::string searchPath = std::string(SAVE_FOLDER) + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    return files;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   UDP FILE SERVER\n";
    std::cout << "========================================\n\n";

    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "ERROR: Cannot initialize Winsock\n";
        system("pause");
        return 1;
    }

    // Создание сокета
    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "ERROR: Cannot create socket\n";
        WSACleanup();
        system("pause");
        return 1;
    }

    // Настройка адреса
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Привязка сокета
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "ERROR: Cannot bind to port " << PORT << "\n";
        closesocket(serverSocket);
        WSACleanup();
        system("pause");
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;
    std::cout << "Files folder: " << SAVE_FOLDER << std::endl;
    std::cout << "\nWaiting for connections...\n";
    std::cout << "========================================\n\n";

    // Основной цикл
    char buffer[BUFFER_SIZE];
    sockaddr_in clientAddr;
    int clientSize = sizeof(clientAddr);

    while (true) {
        // Ждем команду от клиента
        int bytes = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&clientAddr, &clientSize);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            std::string command(buffer);

            // Определяем IP клиента
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

            std::cout << "Client " << clientIP << ": " << command << std::endl;

            // Обработка команд
            if (command == "TEST") {
                // Тест соединения
                SendResponse(serverSocket, "SERVER_READY", clientAddr);

            }
            else if (command == "LIST") {
                // Список файлов
                auto files = GetFileList();
                std::string response = "FILES_COUNT:" + std::to_string(files.size());
                for (const auto& f : files) {
                    response += ":" + f;
                }
                SendResponse(serverSocket, response, clientAddr);

            }
            else if (command.substr(0, 4) == "SEND") {
                // Отправка файла на сервер
                std::string filename = command.substr(5); // Пропускаем "SEND "
                SaveFile(serverSocket, filename, clientAddr);

            }
            else if (command.substr(0, 3) == "GET") {
                // Получение файла с сервера
                std::string filename = command.substr(4); // Пропускаем "GET "
                SendFile(serverSocket, filename, clientAddr);

            }
            else {
                SendResponse(serverSocket, "ERROR:UNKNOWN_COMMAND", clientAddr);
            }

            std::cout << std::endl;
        }
    }

    // Закрытие сокета (никогда не выполнится в бесконечном цикле)
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}