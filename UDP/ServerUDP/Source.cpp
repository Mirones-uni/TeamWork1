#define BOOST_DISABLE_CURRENT_LOCATION
#include <iostream>
#include <boost/asio.hpp>
#include <boost/array.hpp>

// ���������� ������������ ���� boost::asio ��� ��������� ����
using namespace boost::asio;

int main()

    // ������� io_context - ����������� ������ ��� ���������� �����/������ ����������
    boost::asio::io_context io_context;

    // ������� acceptor ��� ������������� �������� ����������
    // ������������ ��� IPv4 ������ �� ����� 8080
    ip::tcp::acceptor acceptor(io_context, ip::tcp::endpoint(ip::tcp::v4(), 8080));

    // �������� ���� ������� (�������� ����������)
    while (true) {
        // ������� ��������� � �������� �������
        std::cout << "Waiting for client ...\n";

        // ������� ����� ��� ������� � ��������
        ip::tcp::socket socket(io_context);

        // ��������� �������� ���������� (����������� �����)
        // ��������� ����� ����� �����, ���� ������ �� �����������
        acceptor.accept(socket);

        // ������� ����� ��� ������ ������ �� ������� (������ 128 ����)
        boost::array<char, 128> buf;

        // ���������� ��� �������� ����� ������
        boost::system::error_code error;

        // ������ ������ �� ������� � �����
        // read_some ������ ��������� ������ (�� ����������� ���� �����)
        size_t len = socket.read_some(buffer(buf), error);

        // ������������ ��������� ������
        if (error == boost::asio::error::eof)
            break; // ������ ������ ����������
        else if (error)
            throw boost::system::system_error(error); // ������ ������

        // ������� ���������� ������ �� �����
        std::string received_data(buf.data(), len);
        std::cout << "Received: " << received_data << std::endl;
        std::cout << std::endl;

        // ������� ��������� ��� �������� �������
        std::string replyMessage = "Hello from server!";

        // ���������� ��������� �������
        write(socket, buffer(replyMessage));


        // ��������� ����� (������ ���������� � ��������)
        socket.close();
        // ����� �������� ������ ���� ���������� ������, ������ ������ �������
    }

    return 0;
}