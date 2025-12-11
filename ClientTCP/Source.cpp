#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <boost/asio.hpp>
#include <boost/array.hpp>
using namespace boost::asio;

int main()
{
    // Создаем объект io_context - центральный диспетчер ввода/вывода в Boost.Asio
    boost::asio::io_context io_context;

    // Создаем TCP-сокет, используя созданный io_context
    ip::tcp::socket socket(io_context);

    // Подключаемся к серверу по адресу 127.0.0.1 (localhost) на порту 8080
    socket.connect(ip::tcp::endpoint(ip::make_address("127.0.0.1"), 8080));

    // Создаем строку с сообщением для отправки на сервер
    std::string message = "Hello from client!";
    // Отправляем сообщение на сервер через сокет
    write(socket, buffer(message));

    // Создаем буфер фиксированного размера (128 байт) для приема данных от сервера
    boost::array<char, 128> buf;
    // Создаем объект для хранения кода ошибки
    boost::system::error_code error;
    // Читаем данные из сокета в буфер, возвращаем количество прочитанных байт
    size_t len = socket.read_some(buffer(buf), error);

    // Проверяем, закрыл ли сервер соединение (конец файла)
    if (error == boost::asio::error::eof)
        std::cout << "Connection closed by server." << std::endl;
    // Если произошла другая ошибка
    else if (error)
        // Генерируем исключение с информацией об ошибке
        throw boost::system::system_error(error);

    // Выводим префикс перед полученными данными
    std::cout << "Received: ";
    // Выводим полученные от сервера данные (только фактически прочитанные байты)
    std::cout.write(buf.data(), len);
    // Завершаем строку вывода
    std::cout << std::endl;

    // Закрываем сокет
    socket.close();

    // Возвращаем 0 - успешное завершение программы
    return 0;
}