#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <fstream>
#include <string>
#include <boost/asio.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

// Функция для извлечения имени файла из пути
std::string extract_filename(const std::string& file_path) {
    // Находим последний разделитель пути
    size_t last_slash = file_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return file_path.substr(last_slash + 1);
    }
    return file_path; // Если нет разделителей, возвращаем весь путь
}

int main() {
    // Фиксированный IP-адрес сервера
    std::string server_ip = "127.0.0.1";

    // Запрашиваем путь к файлу у пользователя
    std::string file_path;
    std::cout << "Enter file path to send: ";
    std::getline(std::cin, file_path);

    // Удаляем кавычки, если пользователь ввел путь с кавычками (при drag&drop в некоторых системах)
    if (!file_path.empty() && file_path.front() == '"' && file_path.back() == '"') {
        file_path = file_path.substr(1, file_path.size() - 2);
    }

    if (file_path.empty()) {
        std::cerr << "File path cannot be empty" << std::endl;
        return 1;
    }

    try {
        // Проверяем существование файла
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Cannot open file: " << file_path << std::endl;
            return 1;
        }

        // Получаем размер файла
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file_size > 4294967295) {
            std::cerr << "File is too large (max 4GB)" << std::endl;
            return 1;
        }

        // Извлекаем имя файла из пути
        std::string filename = extract_filename(file_path);

        if (filename.size() > 64) {
            std::cerr << "Filename is too long (max 64 bytes)" << std::endl;
            return 1;
        }

        std::cout << "Preparing to send file: " << filename
            << " (" << file_size << " bytes)" << std::endl;

        // Читаем содержимое файла
        std::vector<char> file_data(file_size);
        if (!file.read(file_data.data(), file_size)) {
            std::cerr << "Cannot read file" << std::endl;
            return 1;
        }

        // Подключаемся к серверу
        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);

        std::cout << "Connecting to server " << server_ip << "..." << std::endl;
        asio::connect(socket, resolver.resolve(server_ip, "12345"));

        std::cout << "Connected to server. Sending file..." << std::endl;

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

        socket.close();
        std::cout << "Disconnected from server." << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}