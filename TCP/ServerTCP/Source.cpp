#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <cstring>

namespace asio = boost::asio;
using asio::ip::tcp;
namespace fs = boost::filesystem;

enum Command {
    CMD_SEND_FILE = 1,     // Клиент хочет отправить файл на сервер
    CMD_REQUEST_FILE = 2   // Клиент запрашивает файл с сервера
};

int main() 
{
    // Фиксированный путь для сохранения файлов
    fs::path save_directory = "C:/Users/karas/OneDrive/Desktop/test";
        try {
            // Создаем директорию, если она не существует
            if (!fs::exists(save_directory)) {
                if (fs::create_directories(save_directory)) {
                    std::cout << "Created directory: " << save_directory << std::endl;
                }
                else {
                    std::cerr << "Failed to create directory: " << save_directory << std::endl;
                    return 1;
                }
            }

            // Проверяем, что директория доступна для записи
            if (!fs::is_directory(save_directory)) {
                std::cerr << "Path is not a directory: " << save_directory << std::endl;
                return 1;
            }

            asio::io_context io_context;
            tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 12345));

            std::cout << "Server started. Waiting for connections..." << std::endl;
            std::cout << "Save_directory: " << save_directory << std::endl;

            while (true) {
                tcp::socket socket(io_context);
                acceptor.accept(socket);
                std::cout << "\nClient connected." << std::endl;

                try {
                    // Получаем команду от клиента
                    uint8_t command;
                    asio::read(socket, asio::buffer(&command, sizeof(command)));
                    std::cout << "Received command: " << (int)command << std::endl;

                    if (command == CMD_SEND_FILE) {
                        // Клиент хочет отправить файл на сервер

                        // Получаем размер файла (4 байта)
                        uint32_t file_size;
                        asio::read(socket, asio::buffer(&file_size, sizeof(file_size)));
                        file_size = ntohl(file_size);
                        std::cout << "File size: " << file_size << " bytes" << std::endl;

                        // Получаем имя файла (64 байта)
                        char filename_buffer[65] = { 0 };
                        asio::read(socket, asio::buffer(filename_buffer, 64));
                        std::string filename(filename_buffer);
                        std::cout << "File name: " << filename << std::endl;

                        // Создаем полный путь для сохранения файла
                        fs::path file_path = save_directory / filename;

                        // Проверяем, не существует ли уже файл с таким именем
                        // Если существует, добавляем номер к имени файла
                        int counter = 1;
                        std::string base_name = filename;
                        std::string extension = "";

                        // Разделяем имя файла и расширение
                        size_t dot_pos = filename.find_last_of('.');
                        if (dot_pos != std::string::npos) {
                            base_name = filename.substr(0, dot_pos);
                            extension = filename.substr(dot_pos);
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

                        // Отправляем подтверждение
                        std::string confirmation = "File received successfully: " + file_path.filename().string();
                        asio::write(socket, asio::buffer(confirmation));

                    }
                    else if (command == CMD_REQUEST_FILE) {
                        // Клиент запрашивает файл с сервера

                        // Получаем имя запрашиваемого файла
                        char filename_buffer[65] = { 0 };
                        asio::read(socket, asio::buffer(filename_buffer, 64));
                        std::string filename(filename_buffer);
                        std::cout << "Client requested file: " << filename << std::endl;

                        // Создаем полный путь к файлу
                        fs::path file_path = save_directory / filename;

                        // Проверяем существование файла
                        if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
                            std::string error_msg = "Error: File not found or not accessible: " + filename;
                            asio::write(socket, asio::buffer(error_msg));
                            throw std::runtime_error("File not found: " + file_path.string());
                        }

                        // Получаем размер файла
                        uint64_t file_size = fs::file_size(file_path);
                        if (file_size > 4294967295) {
                            throw std::runtime_error("File is too large (max 4GB)");
                        }

                        std::cout << "File size: " << file_size << " bytes" << std::endl;

                        // Отправляем размер файла
                        uint32_t net_file_size = htonl(static_cast<uint32_t>(file_size));
                        asio::write(socket, asio::buffer(&net_file_size, sizeof(net_file_size)));

                        // Отправляем имя файла (дополняем до 64 байт)
                        char send_filename_buffer[64] = { 0 };
                        std::copy(filename.begin(), filename.end(), send_filename_buffer);
                        asio::write(socket, asio::buffer(send_filename_buffer, 64));

                        // Читаем и отправляем содержимое файла
                        std::ifstream file(file_path.string(), std::ios::binary);
                        if (!file) {
                            throw std::runtime_error("Cannot open file for reading: " + file_path.string());
                        }

                        std::vector<char> file_data(file_size);
                        file.read(file_data.data(), file_size);
                        file.close();

                        asio::write(socket, asio::buffer(file_data));

                        std::cout << "File sent successfully: " << filename << std::endl;

                        // Получаем подтверждение от клиента
                        char confirmation[128] = { 0 };
                        size_t len = socket.read_some(asio::buffer(confirmation));
                        std::cout << "Client response: " << std::string(confirmation, len) << std::endl;

                    }
                    else {
                        std::string error_msg = "Error: Unknown command: " + std::to_string((int)command);
                        asio::write(socket, asio::buffer(error_msg));
                        throw std::runtime_error("Unknown command from client");
                    }

                }
                catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    // Отправляем сообщение об ошибке клиенту
                    std::string error_msg = "Error: " + std::string(e.what());
                    asio::write(socket, asio::buffer(error_msg));
                }

                socket.close();
                std::cout << "Connection closed." << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }

    return 0;
}