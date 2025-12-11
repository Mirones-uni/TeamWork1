#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <fstream>
#include <string>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;
namespace fs = boost::filesystem;

enum Command {
    CMD_SEND_FILE = 1,     // Клиент хочет отправить файл на сервер
    CMD_REQUEST_FILE = 2   // Клиент запрашивает файл с сервера
};

// Функция для извлечения имени файла из пути
std::string extract_filename(const std::string& file_path) {
    // Находим последний разделитель пути
    size_t last_slash = file_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return file_path.substr(last_slash + 1);
    }
    return file_path; // Если нет разделителей, возвращаем весь путь
}

void send_file_to_server(tcp::socket& socket, const std::string& file_path) {
    // Проверяем существование файла
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open file: " << file_path << std::endl;
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    // Получаем размер файла
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (file_size > 4294967295) {
        throw std::runtime_error("File is too large (max 4GB)");
    }

    // Извлекаем имя файла из пути
    std::string filename = extract_filename(file_path);

    if (filename.size() > 64) {
        throw std::runtime_error("Filename is too long (max 64 bytes)");
    }

    std::cout << "Preparing to send file: " << filename
        << " (" << file_size << " bytes)" << std::endl;

    // Читаем содержимое файла
    std::vector<char> file_data(file_size);
    if (!file.read(file_data.data(), file_size)) {
        throw std::runtime_error("Cannot read file");
    }

    // Отправляем размер файла (в сетевом порядке байт)
    uint32_t net_file_size = htonl(static_cast<uint32_t>(file_size));
    asio::write(socket, asio::buffer(&net_file_size, sizeof(net_file_size)));

    // Отправляем имя файла (дополняем до 64 байт)
    char filename_buffer[64] = { 0 };
    std::copy(filename.begin(), filename.end(), filename_buffer);
    asio::write(socket, asio::buffer(filename_buffer, 64));

    // Отправляем содержимое файла
    asio::write(socket, asio::buffer(file_data));

    std::cout << "File sent. Waiting for confirmation..." << std::endl;

    // Получаем подтверждение от сервера
    char confirmation[128] = { 0 };
    size_t len = socket.read_some(asio::buffer(confirmation));
    std::cout << "Server response: " << std::string(confirmation, len) << std::endl;
}

void request_file_from_server(tcp::socket& socket, const std::string& filename) {
    // Отправляем имя запрашиваемого файла
    char filename_buffer[64] = { 0 };
    std::copy(filename.begin(), filename.end(), filename_buffer);
    asio::write(socket, asio::buffer(filename_buffer, 64));

    std::cout << "Requesting file: " << filename << std::endl;

    // Получаем размер файла
    uint32_t file_size;
    asio::read(socket, asio::buffer(&file_size, sizeof(file_size)));
    file_size = ntohl(file_size);
    std::cout << "File size: " << file_size << " bytes" << std::endl;

    // Получаем имя файла (64 байта)
    char received_filename_buffer[65] = { 0 };
    asio::read(socket, asio::buffer(received_filename_buffer, 64));
    std::string received_filename(received_filename_buffer);
    std::cout << "File name: " << received_filename << std::endl;

    // Фиксированный путь для сохранения файлов на клиенте
    fs::path save_directory = "C:/Users/karas/OneDrive/Desktop/test2";

    // Создаем директорию, если она не существует
    if (!fs::exists(save_directory)) {
        fs::create_directories(save_directory);
        std::cout << "Created directory: " << save_directory << std::endl;
    }

    // Создаем полный путь для сохранения файла
    fs::path file_path = save_directory / received_filename;

    // Проверяем, не существует ли уже файл с таким именем
    int counter = 1;
    std::string base_name = received_filename;
    std::string extension = "";

    size_t dot_pos = received_filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        base_name = received_filename.substr(0, dot_pos);
        extension = received_filename.substr(dot_pos);
    }

    while (fs::exists(file_path)) {
        std::string new_filename = base_name + "_" + std::to_string(counter) + extension;
        file_path = save_directory / new_filename;
        counter++;
    }

    std::cout << "Saving to: " << file_path.string() << std::endl;

    // Создаем файл для записи
    std::ofstream file(file_path.string(), std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + file_path.string());
    }

    // Получаем содержимое файла
    std::vector<char> file_data(file_size);
    asio::read(socket, asio::buffer(file_data));

    file.write(file_data.data(), file_data.size());
    file.close();

    std::cout << "File received and saved successfully!" << std::endl;
    std::cout << "Saved as: " << file_path.filename().string() << std::endl;

    // Отправляем подтверждение серверу
    std::string confirmation = "File received successfully: " + file_path.filename().string();
    asio::write(socket, asio::buffer(confirmation));
}

int main() 
{
    while (true)
    {
        // Фиксированный IP-адрес сервера
        std::string server_ip = "127.0.0.1";

        // Запрашиваем у пользователя, что он хочет сделать
        std::cout << "Select mode:" << std::endl;
        std::cout << "1. Send file to server" << std::endl;
        std::cout << "2. Request file from server" << std::endl;
        std::cout << "Enter choice (1 or 2): ";

        int choice;
        std::cin >> choice;
        std::cin.ignore(); // Очищаем буфер ввода

        if (choice != 1 && choice != 2) {
            std::cerr << "Invalid choice. Must be 1 or 2." << std::endl;
            return 1;
        }

        try {
            // Подключаемся к серверу
            asio::io_context io_context;
            tcp::socket socket(io_context);
            tcp::resolver resolver(io_context);

            std::cout << "Connecting to server " << server_ip << "..." << std::endl;
            asio::connect(socket, resolver.resolve(server_ip, "12345"));
            std::cout << "Connected to server." << std::endl;

            // Отправляем команду серверу
            uint8_t command = (choice == 1) ? CMD_SEND_FILE : CMD_REQUEST_FILE;
            asio::write(socket, asio::buffer(&command, sizeof(command)));

            if (choice == 1) {
                // Режим отправки файла на сервер
                std::string file_path;
                std::cout << "Enter file path to send: ";
                std::getline(std::cin, file_path);

                // Удаляем кавычки, если пользователь ввел путь с кавычками
                if (!file_path.empty() && file_path.front() == '"' && file_path.back() == '"') {
                    file_path = file_path.substr(1, file_path.size() - 2);
                }

                send_file_to_server(socket, file_path);

            }
            else {
                // Режим запроса файла с сервера
                std::string filename;
                std::cout << "Enter filename to request from server: ";
                std::getline(std::cin, filename);

                request_file_from_server(socket, filename);
            }

            socket.close();
            std::cout << "Disconnected from server." << std::endl;
            std::cout << "" << std::endl;

        }
        catch (const std::exception& e) {
            std::cerr << "Client error: " << e.what() << std::endl;
            return 1;
        }
    }
    return 0;
}