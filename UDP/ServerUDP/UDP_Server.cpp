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
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <map>
#include <atomic>
#include <boost/asio.hpp>

#pragma comment(lib, "ws2_32.lib")

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SAVE_FOLDER = "C:\\UDP_Files";
const int THREAD_POOL_SIZE = 10;  // Количество потоков для обработки

// Глобальные переменные для асинхронной обработки
std::atomic<bool> serverRunning(true);
std::mutex socketMutex;
SOCKET globalServerSocket;

// Структура для хранения запроса
struct ClientRequest {
    sockaddr_in clientAddr;
    std::string command;
    std::string data;
    time_t timestamp;
};

// Очередь запросов
std::queue<ClientRequest> requestQueue;
std::mutex queueMutex;
std::condition_variable queueCV;

// Map для хранения состояний передачи файлов
std::map<std::string, std::pair<std::ofstream*, int>> fileTransfers;
std::mutex transfersMutex;

// Функция для отправки ответа клиенту
void SendResponse(SOCKET socket, const std::string& response, sockaddr_in clientAddr) {
    std::lock_guard<std::mutex> lock(socketMutex);
    sendto(socket, response.c_str(), response.length(), 0,
        (sockaddr*)&clientAddr, sizeof(clientAddr));
}

// Генерация уникального ID клиента
std::string GetClientID(sockaddr_in clientAddr) {
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    return std::string(clientIP) + ":" + std::to_string(ntohs(clientAddr.sin_port));
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

// Асинхронное сохранение файла
void AsyncSaveFile(sockaddr_in clientAddr, const std::string& filename, const std::string& filedata) {
    std::string clientID = GetClientID(clientAddr);
    std::cout << "[" << clientID << "] Starting async file save: " << filename << std::endl;

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

    // Сохраняем файл асинхронно
    std::thread([clientAddr, clientID, filepath, filedata]() {
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            SendResponse(globalServerSocket, "ERROR:CANNOT_CREATE_FILE", clientAddr);
            std::cout << "[" << clientID << "] Error creating file: " << filepath << std::endl;
            return;
        }

        file.write(filedata.c_str(), filedata.size());
        file.close();

        std::string response = "DONE:" + std::to_string(filedata.size());
        SendResponse(globalServerSocket, response, clientAddr);

        std::cout << "[" << clientID << "] File saved: " << filepath
            << " (" << filedata.size() << " bytes)" << std::endl;
        }).detach();
}

// Асинхронная отправка файла
void AsyncSendFile(sockaddr_in clientAddr, const std::string& filename) {
    std::string clientID = GetClientID(clientAddr);

    std::thread([clientAddr, clientID, filename]() {
        std::string filepath = std::string(SAVE_FOLDER) + "\\" + filename;

        // Проверяем существование файла
        if (GetFileAttributesA(filepath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            SendResponse(globalServerSocket, "ERROR:FILE_NOT_FOUND", clientAddr);
            std::cout << "[" << clientID << "] File not found: " << filename << std::endl;
            return;
        }

        // Открываем файл для чтения
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            SendResponse(globalServerSocket, "ERROR:CANNOT_OPEN_FILE", clientAddr);
            std::cout << "[" << clientID << "] Cannot open file: " << filename << std::endl;
            return;
        }

        // Получаем размер файла
        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Отправляем информацию о файле
        SendResponse(globalServerSocket, "FILE_INFO:" + std::to_string(fileSize), clientAddr);

        std::cout << "[" << clientID << "] Sending file: " << filename
            << " (" << fileSize << " bytes)" << std::endl;

        // Отправляем файл по частям
        char buffer[BUFFER_SIZE];
        int totalSent = 0;
        int clientSize = sizeof(clientAddr);

        while (!file.eof()) {
            file.read(buffer, sizeof(buffer));
            std::streamsize bytesRead = file.gcount();

            if (bytesRead > 0) {
                sendto(globalServerSocket, buffer, bytesRead, 0,
                    (sockaddr*)&clientAddr, sizeof(clientAddr));
                totalSent += bytesRead;

                // Ждем подтверждение с таймаутом
                char ack[10];
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(globalServerSocket, &readSet);

                struct timeval timeout;
                timeout.tv_sec = 5;  // 5 секунд таймаут
                timeout.tv_usec = 0;

                if (select(0, &readSet, NULL, NULL, &timeout) > 0) {
                    recvfrom(globalServerSocket, ack, sizeof(ack), 0,
                        (sockaddr*)&clientAddr, &clientSize);
                }

                if (totalSent % (BUFFER_SIZE * 10) == 0) {
                    std::cout << "[" << clientID << "] Progress: " << totalSent
                        << "/" << fileSize << " bytes" << std::endl;
                }
            }
        }

        file.close();

        // Отправляем сигнал конца
        SendResponse(globalServerSocket, "END_OF_FILE", clientAddr);
        std::cout << "[" << clientID << "] File sent successfully" << std::endl;
        }).detach();
}

// Функция-обработчик (работает в отдельном потоке)
void RequestHandler(int threadId) {
    while (serverRunning) {
        ClientRequest request;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [] {
                return !requestQueue.empty() || !serverRunning;
                });

            if (!serverRunning && requestQueue.empty()) return;

            request = requestQueue.front();
            requestQueue.pop();
        }

        // Обрабатываем запрос
        std::string clientID = GetClientID(request.clientAddr);

        std::cout << "[Thread " << threadId << "] Processing from "
            << clientID << ": " << request.command << std::endl;

        // Обработка команд
        if (request.command == "TEST") {
            SendResponse(globalServerSocket, "SERVER_READY", request.clientAddr);
        }
        else if (request.command == "LIST") {
            auto files = GetFileList();
            std::string response = "FILES_COUNT:" + std::to_string(files.size());
            for (const auto& f : files) {
                response += ":" + f;
            }
            SendResponse(globalServerSocket, response, request.clientAddr);
        }
        else if (request.command.substr(0, 9) == "SEND_FILE") {
            // Формат: SEND_FILE:filename:data
            size_t firstColon = request.command.find(':', 9);
            size_t secondColon = request.command.find(':', firstColon + 1);

            if (firstColon != std::string::npos && secondColon != std::string::npos) {
                std::string filename = request.command.substr(firstColon + 1,
                    secondColon - firstColon - 1);
                std::string filedata = request.command.substr(secondColon + 1);

                // Отправляем подтверждение
                SendResponse(globalServerSocket, "READY", request.clientAddr);

                // Запускаем асинхронное сохранение
                AsyncSaveFile(request.clientAddr, filename, filedata);
            }
        }
        else if (request.command.substr(0, 3) == "GET") {
            // Получение файла с сервера
            std::string filename = request.command.substr(4); // Пропускаем "GET "
            AsyncSendFile(request.clientAddr, filename);
        }
        else {
            SendResponse(globalServerSocket, "ERROR:UNKNOWN_COMMAND", request.clientAddr);
        }
    }
}

// Функция для приема сообщений
void ReceiverThread() {
    char buffer[BUFFER_SIZE];
    sockaddr_in clientAddr;
    int clientSize = sizeof(clientAddr);

    std::cout << "Receiver thread started" << std::endl;

    while (serverRunning) {
        // Принимаем сообщение
        int bytes = recvfrom(globalServerSocket, buffer, BUFFER_SIZE, 0,
            (sockaddr*)&clientAddr, &clientSize);

        if (bytes > 0) {
            buffer[bytes] = '\0';
            std::string message(buffer);

            // Создаем запрос
            ClientRequest request;
            request.clientAddr = clientAddr;
            request.command = message;
            request.timestamp = time(nullptr);

            // Добавляем в очередь
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                requestQueue.push(request);
            }
            queueCV.notify_one();  // Будим один спящий поток-обработчик

            // Выводим статистику
            if (requestQueue.size() % 10 == 0) {
                std::cout << "[Receiver] Queue size: " << requestQueue.size() << std::endl;
            }
        }
        else if (WSAGetLastError() != WSAEWOULDBLOCK) {
            // Ошибка приема
            std::cout << "[Receiver] Receive error: " << WSAGetLastError() << std::endl;
        }
    }
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   ASYNC UDP FILE SERVER\n";
    std::cout << "   Boost.Asio available for networking\n";
    std::cout << "========================================\n\n";

    std::cout << "Features:\n";
    std::cout << "- Multi-threaded request processing\n";
    std::cout << "- Async file transfers\n";
    std::cout << "- Concurrent client support\n";
    std::cout << "- Thread pool: " << THREAD_POOL_SIZE << " threads\n";
    std::cout << "========================================\n\n";

    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "ERROR: Cannot initialize Winsock\n";
        system("pause");
        return 1;
    }

    // Создание сокета
    globalServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (globalServerSocket == INVALID_SOCKET) {
        std::cout << "ERROR: Cannot create socket\n";
        WSACleanup();
        system("pause");
        return 1;
    }

    // Делаем сокет неблокирующим
    u_long mode = 1;
    ioctlsocket(globalServerSocket, FIONBIO, &mode);

    // Настройка адреса
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Привязка сокета
    if (bind(globalServerSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "ERROR: Cannot bind to port " << PORT << "\n";
        closesocket(globalServerSocket);
        WSACleanup();
        system("pause");
        return 1;
    }

    // Создаем папку для файлов
    CreateDirectoryA(SAVE_FOLDER, NULL);

    std::cout << "Server started on port " << PORT << std::endl;
    std::cout << "Files folder: " << SAVE_FOLDER << std::endl;
    std::cout << "\nStarting thread pool...\n";

    // Запускаем пул потоков-обработчиков
    std::vector<std::thread> threadPool;
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        threadPool.emplace_back(RequestHandler, i + 1);
    }

    // Запускаем поток для приема сообщений
    std::thread receiver(ReceiverThread);

    std::cout << "\nServer is running. Press Ctrl+C to stop.\n";
    std::cout << "========================================\n\n";

    // Основной цикл управления
    while (true) {
        std::string cmd;
        std::cout << "\nServer commands: status | clients | stop\n";
        std::cout << "Command: ";
        std::getline(std::cin, cmd);

        if (cmd == "status") {
            std::lock_guard<std::mutex> lock(queueMutex);
            std::cout << "Queue size: " << requestQueue.size() << std::endl;
            std::cout << "Active threads: " << threadPool.size() << std::endl;
        }
        else if (cmd == "clients") {
            // Можно добавить логику отслеживания клиентов
            std::cout << "Client tracking not implemented in basic version\n";
        }
        else if (cmd == "stop") {
            std::cout << "Shutting down server...\n";
            serverRunning = false;
            queueCV.notify_all();
            break;
        }
    }

    // Ожидаем завершения потоков
    for (auto& t : threadPool) {
        if (t.joinable()) t.join();
    }

    if (receiver.joinable()) receiver.join();

    // Закрытие сокета
    closesocket(globalServerSocket);
    WSACleanup();

    std::cout << "Server stopped.\n";
    system("pause");
    return 0;
}