// WindowsProject1.cpp : Определяет точку входа для приложения.
#ifdef _MSC_VER
#define BOOST_ASIO_DISABLE_CONCEPTS
#endif
#include "framework.h"
#include "WindowsProject1.h"
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#define MAX_LOADSTRING 100
#define IDC_INPUT_EDIT      1001
#define IDC_OUTPUT_EDIT     1002
#define IDC_BUTTON_SEND     1003
#define IDC_BUTTON_REQUEST  1004
#define IDC_BUTTON_LIST     1005

namespace asio = boost::asio;
using asio::ip::tcp;
namespace fs = boost::filesystem;

// Глобальные переменные:
HINSTANCE hInst;                                // текущий экземпляр
WCHAR szTitle[MAX_LOADSTRING];                  // Текст строки заголовка
WCHAR szWindowClass[MAX_LOADSTRING];            // имя класса главного окна

HWND hInputEdit, hOutputEdit, hButtonSend, hButtonRequest;

// Отправить объявления функций, включенных в этот модуль кода:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                CreateControls(HWND hWnd);
void                AppendTextToOutput(const std::wstring& text);
void                SendFileToServer();
void                RequestFileFromServer();
void                ListFilesOnServer();

// Вспомогательные функции
std::string ExtractFilename(const std::string& file_path) {
    size_t last_slash = file_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return file_path.substr(last_slash + 1);
    }
    return file_path;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void AppendTextToOutput(const std::wstring& text) {
    std::wstring newText = text + L"\r\n";
    int len = GetWindowTextLength(hOutputEdit);
    SendMessage(hOutputEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hOutputEdit, EM_REPLACESEL, 0, (LPARAM)newText.c_str());
}

void SendFileToServer() {
    try {
        // Получаем путь из поля ввода
        wchar_t buffer[1024];
        GetWindowText(hInputEdit, buffer, 1024);
        std::string file_path = WStringToString(buffer);

        if (file_path.empty()) {
            AppendTextToOutput(L"Ошибка: Путь к файлу не указан");
            return;
        }

        AppendTextToOutput(L"Подключение к серверу...");

        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);

        asio::connect(socket, resolver.resolve("127.0.0.1", "12345"));
        AppendTextToOutput(L"Подключено к серверу");

        // Отправляем команду (1 = отправка файла)
        uint8_t command = 1;
        asio::write(socket, asio::buffer(&command, sizeof(command)));

        // Проверяем существование файла
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file) {
            AppendTextToOutput(L"Ошибка: Не удается открыть файл");
            return;
        }

        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string filename = ExtractFilename(file_path);
        if (filename.size() > 64) {
            AppendTextToOutput(L"Ошибка: Имя файла слишком длинное");
            return;
        }

        std::wstringstream ws;
        ws << L"Отправка файла: " << StringToWString(filename) << L" (" << file_size << L" байт)";
        AppendTextToOutput(ws.str());

        // Читаем файл
        std::vector<char> file_data(file_size);
        file.read(file_data.data(), file_size);

        // Отправляем размер файла
        uint32_t net_file_size = htonl(static_cast<uint32_t>(file_size));
        asio::write(socket, asio::buffer(&net_file_size, sizeof(net_file_size)));

        // Отправляем имя файла
        char filename_buffer[64] = { 0 };
        std::copy(filename.begin(), filename.end(), filename_buffer);
        asio::write(socket, asio::buffer(filename_buffer, 64));

        // Отправляем содержимое
        asio::write(socket, asio::buffer(file_data));
        AppendTextToOutput(L"Файл отправлен. Ожидание подтверждения...");

        // Получаем ответ
        char confirmation[128] = { 0 };
        size_t len = socket.read_some(asio::buffer(confirmation));
        std::string response(confirmation, len);
        AppendTextToOutput(L"Ответ сервера: ");
        AppendTextToOutput(StringToWString(response));

        socket.close();
        AppendTextToOutput(L"Отключено от сервера");
        AppendTextToOutput(L" ");
    }
    catch (const std::exception& e) {
        AppendTextToOutput(StringToWString("Ошибка: " + std::string(e.what())));
    }
}

void RequestFileFromServer() {
    try {
        // Получаем имя файла из поля ввода
        wchar_t buffer[1024];
        GetWindowText(hInputEdit, buffer, 1024);
        std::string filename = WStringToString(buffer);

        if (filename.empty()) {
            AppendTextToOutput(L"Ошибка: Имя файла не указано");
            return;
        }

        AppendTextToOutput(L"Подключение к серверу...");

        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);

        asio::connect(socket, resolver.resolve("127.0.0.1", "12345"));
        AppendTextToOutput(L"Подключено к серверу");

        // Отправляем команду (2 = запрос файла)
        uint8_t command = 2;
        asio::write(socket, asio::buffer(&command, sizeof(command)));

        // Отправляем имя файла
        char filename_buffer[64] = { 0 };
        std::copy(filename.begin(), filename.end(), filename_buffer);
        asio::write(socket, asio::buffer(filename_buffer, 64));

        std::wstringstream ws;
        ws << L"Запрос файла: " << StringToWString(filename);
        AppendTextToOutput(ws.str());

        // Получаем размер файла
        uint32_t file_size;
        asio::read(socket, asio::buffer(&file_size, sizeof(file_size)));
        file_size = ntohl(file_size);

        ws.str(L"");
        ws << L"Размер файла: " << file_size << L" байт";
        AppendTextToOutput(ws.str());

        // Получаем имя файла
        char received_filename_buffer[65] = { 0 };
        asio::read(socket, asio::buffer(received_filename_buffer, 64));
        std::string received_filename(received_filename_buffer);

        // Создаем директорию для сохранения
        fs::path save_directory = "C:/Client_directory";
        if (!fs::exists(save_directory)) {
            fs::create_directories(save_directory);
        }

        // Создаем путь для сохранения
        fs::path file_path = save_directory / received_filename;

        // Проверяем существование файла
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

        // Получаем содержимое файла
        std::vector<char> file_data(file_size);
        asio::read(socket, asio::buffer(file_data));

        // Сохраняем файл
        std::ofstream out_file(file_path.string(), std::ios::binary);
        out_file.write(file_data.data(), file_data.size());
        out_file.close();

        ws.str(L"");
        ws << L"Файл сохранен как: " << StringToWString(file_path.filename().string());
        AppendTextToOutput(ws.str());

        // Отправляем подтверждение
        std::string confirmation = "File received successfully: " + file_path.filename().string();
        asio::write(socket, asio::buffer(confirmation));

        socket.close();
        AppendTextToOutput(L"Файл успешно получен и сохранен");
        AppendTextToOutput(L"Отключено от сервера");
        AppendTextToOutput(L" ");

    }
    catch (const std::exception& e) {
        AppendTextToOutput(StringToWString("Ошибка: " + std::string(e.what())));
    }
}
void ListFilesOnServer() {
    try {
        AppendTextToOutput(L"Подключение к серверу...");

        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);

        asio::connect(socket, resolver.resolve("127.0.0.1", "12345"));
        AppendTextToOutput(L"Подключено к серверу");

        // Отправляем команду: запрос списка файлов
        uint8_t command = 3;
        asio::write(socket, asio::buffer(&command, sizeof(command)));

        AppendTextToOutput(L"Запрос списка файлов...");

        // Получаем ответ (до 4096 байт — достаточно для списка имён)
        char buffer[4096] = { 0 };
        size_t len = socket.read_some(asio::buffer(buffer));
        std::string response(buffer, len);

        AppendTextToOutput(L"Файлы на сервере: ");
        AppendTextToOutput(StringToWString(response));
        socket.close();
        AppendTextToOutput(L"Отключено от сервера");
        AppendTextToOutput(L" ");
    }
    catch (const std::exception& e) {
        AppendTextToOutput(StringToWString("Ошибка: " + std::string(e.what())));
    }
}
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Инициализация глобальных строк
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WINDOWSPROJECT1, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Выполнить инициализацию приложения:
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));

    MSG msg;

    // Цикл основного сообщения:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void CreateControls(HWND hWnd)
{
    // Поле для ввода
    hInputEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        50, 50, 500, 30,
        hWnd, (HMENU)IDC_INPUT_EDIT, hInst, NULL);

    // Поле для вывода (многострочное, только чтение)
    hOutputEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        50, 150, 500, 300,
        hWnd, (HMENU)IDC_OUTPUT_EDIT, hInst, NULL);

    // Кнопка "Отправить файл"
    hButtonSend = CreateWindowW(L"BUTTON", L"Отправить файл на сервер",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        570, 50, 180, 30,
        hWnd, (HMENU)IDC_BUTTON_SEND, hInst, NULL);

    // Кнопка "Запросить файл"
    hButtonRequest = CreateWindowW(L"BUTTON", L"Запросить файл с сервера",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        570, 90, 180, 30,
        hWnd, (HMENU)IDC_BUTTON_REQUEST, hInst, NULL);
    // Кнопка "Показать файлы на сервере"
    CreateWindowW(L"BUTTON", L"Список файлов на сервере",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        570, 130, 180, 30,
        hWnd, (HMENU)IDC_BUTTON_LIST, hInst, NULL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls(hWnd);
        break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDC_BUTTON_SEND:
            SendFileToServer();
            break;
        case IDC_BUTTON_REQUEST:
            RequestFileFromServer();
            break;
        case IDC_BUTTON_LIST:
            ListFilesOnServer();
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rectInput = { 50, 30, 200, 50 };
        DrawTextW(hdc, L"Введите путь/имя файла:", -1, &rectInput, DT_LEFT);

        RECT rectOutput = { 50, 130, 200, 150 };
        DrawTextW(hdc, L"Вывод:", -1, &rectOutput, DT_LEFT);

        EndPaint(hWnd, &ps);
    }
    break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}