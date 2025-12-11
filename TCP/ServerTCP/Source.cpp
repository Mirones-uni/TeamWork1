#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;
namespace fs = boost::filesystem;

int main() {
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
        std::cout << "Files will be saved to: " << save_directory << std::endl;

        while (true) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "\nClient connected." << std::endl;

            try {
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