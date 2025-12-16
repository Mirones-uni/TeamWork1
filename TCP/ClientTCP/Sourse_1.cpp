#define BOOST_DISABLE_CURRENT_LOCATION
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

namespace asio = boost::asio;
using asio::ip::tcp;
namespace fs = boost::filesystem;
using boost::system::error_code;
using std::placeholders::_1;
using std::placeholders::_2;

enum Command : uint8_t {
    CMD_SEND_FILE = 1,
    CMD_REQUEST_FILE = 2
};

class async_client : public std::enable_shared_from_this<async_client> {
public:
    async_client(asio::io_context& ioc, const std::string& host, const std::string& port,
        Command cmd, std::string arg)
        : ioc_(ioc), resolver_(ioc), socket_(ioc),
        cmd_(cmd), arg_(std::move(arg)),
        buffer_(65536)  // 64 KiB chunk
    {
    }

    void start() {
        auto self = shared_from_this();
        resolver_.async_resolve(host_, port_,
            [this, self](error_code ec, tcp::resolver::results_type res) {
                if (!ec) asio::async_connect(socket_, res,
                    [this, self](error_code ec) {
                        if (!ec) send_command();
                    });
            });
    }

private:
    void send_command() {
        cmd_buf_[0] = static_cast<uint8_t>(cmd_);
        asio::async_write(socket_, asio::buffer(cmd_buf_, 1),
            [this](error_code ec, std::size_t) {
                if (!ec) {
                    if (cmd_ == CMD_SEND_FILE) start_send_file();
                    else if (cmd_ == CMD_REQUEST_FILE) start_request_file();
                }
            });
    }

    //SEND FILE
    void start_send_file() {
        fs::path p(arg_);
        filename_ = p.filename().string();

        if (filename_.size() > 64) {
            std::cerr << "Filename too long (>64)\n";
            return;
        }

        file_.open(arg_, std::ios::binary | std::ios::ate);
        if (!file_) { std::cerr << "Cannot open file\n"; return; }

        file_size_ = file_.tellg();
        if (file_size_ > UINT32_MAX) {
            std::cerr << "File too large (>4GB)\n";
            return;
        }
        file_.seekg(0);

        // send size (4B, BE)
        uint32_t sz = boost::endian::native_to_big(static_cast<uint32_t>(file_size_));
        memcpy(size_buf_, &sz, 4);

        // send filename (64B, zero-padded)
        memset(filename_buf_, 0, 64);
        memcpy(filename_buf_, filename_.data(), filename_.size());

        std::vector<asio::const_buffer> meta = {
            asio::buffer(size_buf_, 4),
            asio::buffer(filename_buf_, 64)
        };

        asio::async_write(socket_, meta,
            [this](error_code ec, std::size_t) {
                if (!ec) send_file_chunks();
            });
    }

    void send_file_chunks() {
        file_.read(buffer_.data(), buffer_.size());
        std::size_t n = file_.gcount();
        if (n == 0) {
            wait_confirmation();
            return;
        }

        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(buffer_.data(), n),
            [this, self](error_code ec, std::size_t) {
                if (!ec) send_file_chunks();
            });
    }

    void wait_confirmation() {
        asio::async_read(socket_, asio::buffer(confirm_buf_),  // reads until buffer full or error; adjust if needed
            [this](error_code ec, std::size_t n) {
                if (!ec) {
                    std::cout << "Server: " << std::string(confirm_buf_, n) << "\n";
                }
                socket_.close();
            });
    }

    // REQUEST FILE 
    void start_request_file() {
        filename_ = arg_;

        if (filename_.size() > 64) {
            std::cerr << "Filename too long (>64)\n";
            return;
        }

        memset(filename_buf_, 0, 64);
        memcpy(filename_buf_, filename_.data(), filename_.size());

        asio::async_write(socket_, asio::buffer(filename_buf_, 64),
            [this](error_code ec, std::size_t) {
                if (!ec) read_file_size();
            });
    }

    void read_file_size() {
        asio::async_read(socket_, asio::buffer(size_buf_, 4),
            [this](error_code ec, std::size_t) {
                if (!ec) {
                    uint32_t sz;
                    memcpy(&sz, size_buf_, 4);
                    file_size_ = boost::endian::big_to_native(sz);
                    read_filename();
                }
            });
    }

    void read_filename() {
        asio::async_read(socket_, asio::buffer(filename_buf_, 64),
            [this](error_code ec, std::size_t) {
                if (!ec) {
                    filename_received_ = std::string(filename_buf_);
                    filename_received_.resize(strlen(filename_received_.c_str())); // trim nulls

                    // resolve save path
                    fs::path dir = "C:/Users/karas/OneDrive/Desktop/test2";
                    if (!fs::exists(dir)) fs::create_directories(dir);

                    fs::path base = dir / filename_received_;
                    fs::path out_path = base;
                    int cnt = 1;
                    while (fs::exists(out_path)) {
                        auto dot = filename_received_.find_last_of('.');
                        std::string name = (dot == std::string::npos)
                            ? filename_received_ : filename_received_.substr(0, dot);
                        std::string ext = (dot == std::string::npos)
                            ? "" : filename_received_.substr(dot);
                        out_path = dir / (name + "_" + std::to_string(cnt++) + ext);
                    }
                    save_path_ = out_path;

                    outfile_.open(save_path_.string(), std::ios::binary);
                    if (!outfile_) { std::cerr << "Cannot create file\n"; return; }

                    read_file_data();
                }
            });
    }

    void read_file_data() {
        std::size_t to_read = std::min<std::size_t>(buffer_.size(), file_size_ - bytes_received_);
        if (to_read == 0) {
            outfile_.close();
            std::cout << "Saved as: " << save_path_.filename().string() << "\n";

            std::string msg = "File received successfully: " + save_path_.filename().string();
            asio::async_write(socket_, asio::buffer(msg),
                [this](error_code, std::size_t) { socket_.close(); });
            return;
        }

        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(buffer_.data(), to_read),
            [this, self, to_read](error_code ec, std::size_t n) {
                if (!ec && n == to_read) {
                    outfile_.write(buffer_.data(), n);
                    bytes_received_ += n;
                    read_file_data();
                }
            });
    }

private:
    asio::io_context& ioc_;
    tcp::resolver resolver_;
    tcp::socket socket_;

    const std::string host_ = "127.0.0.1";
    const std::string port_ = "12345";

    Command cmd_;
    std::string arg_;
    std::string filename_, filename_received_;

    std::ifstream file_;
    std::ofstream outfile_;
    fs::path save_path_;

    std::vector<char> buffer_;
    uint8_t cmd_buf_[1];
    char size_buf_[4];
    char filename_buf_[64];
    char confirm_buf_[256] = { 0 };

    uint64_t file_size_ = 0;
    uint64_t bytes_received_ = 0;
};

//MAIN
int main() {
    asio::io_context ioc;

    while (true) {
        std::cout << "1. Send file\n2. Request file\nChoice: ";
        int choice; std::cin >> choice; std::cin.ignore();
        if (choice != 1 && choice != 2) { std::cerr << "Invalid\n"; continue; }

        Command cmd = (choice == 1) ? CMD_SEND_FILE : CMD_REQUEST_FILE;
        std::string arg;
        std::cout << "Enter " << (choice == 1 ? "file path" : "filename") << ": ";
        std::getline(std::cin, arg);
        if (choice == 1 && !arg.empty() && arg.front() == '"' && arg.back() == '"')
            arg = arg.substr(1, arg.size() - 2);

        try {
            auto client = std::make_shared<async_client>(ioc, "127.0.0.1", "12345", cmd, arg);
            client->start();
            ioc.run();
            ioc.restart();  // для нового цикла
            std::cout << "Done.\n\n";
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    return 0;
}