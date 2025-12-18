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
#include <map>
#include <conio.h>  // Добавил для _kbhit() и _getch()
#include <boost/asio.hpp>

#pragma comment(lib, "ws2_32.lib")

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SAVE_FOLDER = "C:\\UDP_Files";

// Структура для хранения состояния клиента
struct ClientState {
    sockaddr_in addr;
    std::string filename;
    std::ofstream* fileStream;
    int bytesReceived;
    time_t lastActivity;
    bool receivingFile;
};

// Map для отслеживания клиентов
std::map<std::string, ClientState> activeClients;

// Функция для генерации ID клиента
std::string GetClientID(sockaddr_in addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

// Функция для отправки ответа клиенту
void SendResponse(SOCKET socket, const std::string& response, sockaddr_in clientAddr) {
    sendto(socket, response.c_str(), response.length(), 0,
        (sockaddr*)&clientAddr, sizeof(clientAddr));
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

// Обработка команд
void HandleCommand(SOCKET socket, sockaddr_in clientAddr, const std::string& command) {
    std::string clientID = GetClientID(clientAddr);
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

    std::cout << "[" << clientID << "] Command: " << command << std::endl;

    if (command == "TEST") {
        SendResponse(socket, "SERVER_READY", clientAddr);
    }
    else if (command == "LIST") {
        auto files = GetFileList();
        std::string response = "FILES_COUNT:" + std::to_string(files.size());
        for (const auto& f : files) {
            response += ":" + f;
        }
        SendResponse(socket, response, clientAddr);
    }
    else if (command.substr(0, 4) == "SEND") {
        // Проверяем, не занят ли клиент уже передачей файла
        auto it = activeClients.find(clientID);
        if (it != activeClients.end() && it->second.receivingFile) {
            SendResponse(socket, "ERROR:ALREADY_RECEIVING", clientAddr);
            return;
        }

        std::string filename = command.substr(5);

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

        // Открываем файл для записи
        std::ofstream* file = new std::ofstream(filepath, std::ios::binary);
        if (!file->is_open()) {
            SendResponse(socket, "ERROR:CANNOT_CREATE_FILE", clientAddr);
            delete file;
            return;
        }

        // Сохраняем состояние клиента
        ClientState state;
        state.addr = clientAddr;
        state.filename = filename;
        state.fileStream = file;
        state.bytesReceived = 0;
        state.lastActivity = time(nullptr);
        state.receivingFile = true;

        activeClients[clientID] = state;

        // Отправляем подтверждение
        SendResponse(socket, "READY", clientAddr);
        std::cout << "[" << clientID << "] Ready to receive file: " << filename << std::endl;
    }
    else if (command.substr(0, 3) == "GET") {
        std::string filename = command.substr(4);
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

        std::cout << "[" << clientID << "] Sending file info: " << filename
            << " (" << fileSize << " bytes)" << std::endl;

        // Закрываем файл - дальше будем читать в отдельной функции
        file.close();
    }
    else if (command == "END") {
        // Завершение передачи файла
        auto it = activeClients.find(clientID);
        if (it != activeClients.end() && it->second.receivingFile) {
            it->second.fileStream->close();
            delete it->second.fileStream;

            std::string response = "DONE:" + std::to_string(it->second.bytesReceived);
            SendResponse(socket, response, clientAddr);

            std::cout << "[" << clientID << "] File received: " << it->second.filename
                << " (" << it->second.bytesReceived << " bytes)" << std::endl;

            activeClients.erase(it);
        }
    }
    else {
        SendResponse(socket, "ERROR:UNKNOWN_COMMAND", clientAddr);
    }
}

// Обработка данных файла
void HandleFileData(SOCKET socket, sockaddr_in clientAddr, const char* data, int dataSize) {
    std::string clientID = GetClientID(clientAddr);

    auto it = activeClients.find(clientID);
    if (it != activeClients.end() && it->second.receivingFile) {
        // Записываем данные в файл
        it->second.fileStream->write(data, dataSize);
        it->second.bytesReceived += dataSize;
        it->second.lastActivity = time(nullptr);

        // Отправляем подтверждение
        SendResponse(socket, "OK", clientAddr);

        // Показываем прогресс каждые 1MB
        if (it->second.bytesReceived % (1024 * 1024) < dataSize) {
            std::cout << "[" << clientID << "] Received: "
                << it->second.bytesReceived << " bytes" << std::endl;
        }
    }
    else {
        // Клиент не инициировал передачу файла
        SendResponse(socket, "ERROR:NOT_READY", clientAddr);
    }
}

// Очистка неактивных клиентов
void CleanupInactiveClients() {
    time_t now = time(nullptr);
    std::vector<std::string> toRemove;

    for (auto& pair : activeClients) {
        if (now - pair.second.lastActivity > 30) { // 30 секунд бездействия
            if (pair.second.receivingFile) {
                pair.second.fileStream->close();
                delete pair.second.fileStream;
                std::cout << "[" << pair.first << "] Connection timeout, file transfer aborted" << std::endl;
            }
            toRemove.push_back(pair.first);
        }
    }

    for (const auto& clientID : toRemove) {
        activeClients.erase(clientID);
    }
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   ASYNC UDP FILE SERVER (Single-threaded)\n";
    std::cout << "   Boost.Asio included\n";
    std::cout << "========================================\n\n";

    std::cout << "Features:\n";
    std::cout << "- Single-threaded async processing\n";
    std::cout << "- Multiple concurrent clients\n";
    std::cout << "- Non-blocking I/O\n";
    std::cout << "- Automatic client cleanup\n";
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

    // Делаем сокет неблокирующим
    u_long mode = 1;
    ioctlsocket(serverSocket, FIONBIO, &mode);

    // Создаем папку для файлов
    CreateDirectoryA(SAVE_FOLDER, NULL);

    std::cout << "Server started on port " << PORT << std::endl;
    std::cout << "Files folder: " << SAVE_FOLDER << std::endl;
    std::cout << "\nWaiting for connections...\n";
    std::cout << "========================================\n\n";

    // Основной цикл
    char buffer[BUFFER_SIZE];
    sockaddr_in clientAddr;
    int clientSize = sizeof(clientAddr);

    time_t lastCleanup = time(nullptr);

    while (true) {
        // Принимаем сообщение (неблокирующий вызов)
        int bytes = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&clientAddr, &clientSize);

        if (bytes == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                // Нет данных для чтения
                Sleep(10); // Небольшая пауза
            }
            else {
                std::cout << "Receive error: " << error << std::endl;
            }
        }
        else if (bytes > 0) {
            buffer[bytes] = '\0';

            // Проверяем, это команда или данные файла
            std::string clientID = GetClientID(clientAddr);
            auto it = activeClients.find(clientID);

            if (it != activeClients.end() && it->second.receivingFile) {
                // Это данные файла
                if (bytes == 3 && strncmp(buffer, "END", 3) == 0) {
                    HandleCommand(serverSocket, clientAddr, "END");
                }
                else {
                    HandleFileData(serverSocket, clientAddr, buffer, bytes);
                }
            }
            else {
                // Это команда
                std::string command(buffer);
                HandleCommand(serverSocket, clientAddr, command);
            }
        }

        // Периодически очищаем неактивных клиентов
        time_t now = time(nullptr);
        if (now - lastCleanup > 10) { // Каждые 10 секунд
            CleanupInactiveClients();
            lastCleanup = now;
        }

        // Проверяем, не нажал ли пользователь клавишу для выхода
        if (_kbhit()) {
            char ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                std::cout << "\nShutting down server...\n";
                break;
            }
        }
    }

    // Очистка
    for (auto& pair : activeClients) {
        if (pair.second.receivingFile && pair.second.fileStream) {
            pair.second.fileStream->close();
            delete pair.second.fileStream;
        }
    }

    closesocket(serverSocket);
    WSACleanup();

    std::cout << "Server stopped.\n";
    system("pause");
    return 0;
}