// UDP_UI_CLIENT.cpp : Определяет точку входа для приложения.
//

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _CRT_SECURE_NO_WARNINGS

#include "framework.h"
#include "UDP_UI_CLIENT.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

#define MAX_LOADSTRING 100

// Идентификаторы элементов управления
#define IDC_EDIT_INPUT      1001
#define IDC_EDIT_OUTPUT     1002
#define IDC_BTN_SEND        1003
#define IDC_BTN_LIST        1004
#define IDC_BTN_DOWNLOAD    1005
#define IDC_BTN_EXIT        1006

// Глобальные переменные:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

HWND hEditInput = NULL;
HWND hEditOutput = NULL;

SOCKET clientSocket = INVALID_SOCKET;
sockaddr_in serverAddr;

const int BUFFER_SIZE = 65000;
const int PORT = 12345;
const char* SERVER_IP = "127.0.0.1";
const char* DOWNLOAD_FOLDER = "C:\\UDP_Downloads";

// Функции
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

std::string SendCommand(SOCKET socket, const std::string& command, sockaddr_in& serverAddr);
void SendFileToServer(HWND hWnd);
void GetFileList(HWND hWnd);
void DownloadFile(HWND hWnd);
void AppendOutput(HWND hEdit, const std::string& text);
std::string GetInputText(HWND hEdit);

// ========================
// Вспомогательные функции
// ========================

void AppendOutput(HWND hEdit, const std::string& text) {
    if (!hEdit) return;
    int len = GetWindowTextLengthA(hEdit);
    SendMessageA(hEdit, EM_SETSEL, len, len);
    SendMessageA(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageA(hEdit, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
    SendMessageA(hEdit, EM_SCROLLCARET, 0, 0);
}

std::string GetInputText(HWND hEdit) {
    if (!hEdit) return "";
    int len = GetWindowTextLengthW(hEdit);
    std::wstring wstr(len, L'\0');
    GetWindowTextW(hEdit, &wstr[0], len + 1);
    return std::string(wstr.begin(), wstr.end());
}

std::string SendCommand(SOCKET socket, const std::string& command, sockaddr_in& serverAddr) {
    sendto(socket, command.c_str(), (int)command.length(), 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr));

    char buffer[BUFFER_SIZE];
    int serverSize = sizeof(serverAddr);
    int timeout = 5000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    int bytes = recvfrom(socket, buffer, BUFFER_SIZE - 1, 0,
        (sockaddr*)&serverAddr, &serverSize);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        return std::string(buffer);
    }
    return "ERROR:NO_RESPONSE";
}

void SendFileToServer(HWND hWnd) {
    std::string filepath = GetInputText(hEditInput);
    if (filepath.empty()) {
        AppendOutput(hEditOutput, "ERROR: Please enter file path");
        return;
    }

    // Проверяем существование файла
    DWORD attrib = GetFileAttributesA(filepath.c_str());
    if (attrib == INVALID_FILE_ATTRIBUTES || (attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        AppendOutput(hEditOutput, "ERROR: File not found or is a directory");
        return;
    }

    size_t lastSlash = filepath.find_last_of("\\/");
    std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

    AppendOutput(hEditOutput, "Sending file: " + filename);

    std::string response = SendCommand(clientSocket, "SEND " + filename, serverAddr);
    if (response != "READY") {
        AppendOutput(hEditOutput, "ERROR: Server not ready: " + response);
        return;
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        AppendOutput(hEditOutput, "ERROR: Cannot open file");
        return;
    }

    char buffer[BUFFER_SIZE];
    int totalSent = 0;

    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            sendto(clientSocket, buffer, (int)bytesRead, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            totalSent += (int)bytesRead;

            // Ожидаем ACK
            char ack[10];
            int serverSize = sizeof(serverAddr);
            recvfrom(clientSocket, ack, sizeof(ack) - 1, 0,
                (sockaddr*)&serverAddr, &serverSize);
        }
    }
    file.close();

    sendto(clientSocket, "END", 3, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    response = SendCommand(clientSocket, "", serverAddr);
    AppendOutput(hEditOutput, response);
}

void GetFileList(HWND hWnd) {
    std::string response = SendCommand(clientSocket, "LIST", serverAddr);

    if (response.substr(0, 12) == "FILES_COUNT:") {
        std::stringstream ss(response.substr(12));
        std::string token;
        std::getline(ss, token, ':');
        int fileCount = std::stoi(token);

        std::string msg = "Files on server (" + std::to_string(fileCount) + "):";
        AppendOutput(hEditOutput, msg);
        AppendOutput(hEditOutput, "====================");

        for (int i = 1; i <= fileCount && std::getline(ss, token, ':'); i++) {
            AppendOutput(hEditOutput, std::to_string(i) + ". " + token);
        }
        if (fileCount == 0) {
            AppendOutput(hEditOutput, "No files found");
        }
    }
    else {
        AppendOutput(hEditOutput, "ERROR: " + response);
    }
}

void DownloadFile(HWND hWnd) {
    std::string filename = GetInputText(hEditInput);
    if (filename.empty()) {
        AppendOutput(hEditOutput, "ERROR: Filename cannot be empty");
        return;
    }

    std::string response = SendCommand(clientSocket, "GET " + filename, serverAddr);
    if (response.substr(0, 5) == "ERROR") {
        AppendOutput(hEditOutput, response);
        return;
    }
    if (response.substr(0, 10) != "FILE_INFO:") {
        AppendOutput(hEditOutput, "ERROR: Invalid response: " + response);
        return;
    }

    long long fileSize = std::stoll(response.substr(10));
    AppendOutput(hEditOutput, "Downloading: " + filename + " (" + std::to_string(fileSize) + " bytes)");

    sendto(clientSocket, "READY", 5, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

    CreateDirectoryA(DOWNLOAD_FOLDER, NULL);
    std::string savePath = std::string(DOWNLOAD_FOLDER) + "\\" + filename;

    std::ofstream file(savePath, std::ios::binary);
    if (!file) {
        AppendOutput(hEditOutput, "ERROR: Cannot create file");
        return;
    }

    char buffer[BUFFER_SIZE];
    int serverSize = sizeof(serverAddr);
    long long totalReceived = 0;

    while (totalReceived < fileSize) {
        int bytes = recvfrom(clientSocket, buffer, BUFFER_SIZE - 1, 0,
            (sockaddr*)&serverAddr, &serverSize);
        if (bytes <= 0) break;

        if (bytes == 11 && strncmp(buffer, "END_OF_FILE", 11) == 0) {
            break;
        }

        file.write(buffer, bytes);
        totalReceived += bytes;

        sendto(clientSocket, "OK", 2, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    }

    file.close();
    AppendOutput(hEditOutput, "File saved: " + savePath);
}

// ========================
// WinMain и инициализация
// ========================

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_UDPUICLIENT, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow)) {
        return FALSE;
    }

    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBoxA(NULL, "Cannot initialize Winsock", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        MessageBoxA(NULL, "Cannot create socket", "Error", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    int timeout = 5000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_UDPUICLIENT));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
    }
    WSACleanup();
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
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_UDPUICLIENT));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_UDPUICLIENT);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 700, 500, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    // Создаём элементы
    CreateWindowW(L"STATIC", L"Input (file path or name):", WS_CHILD | WS_VISIBLE,
        10, 10, 200, 20, hWnd, NULL, hInstance, NULL);

    hEditInput = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        10, 35, 670, 25, hWnd, (HMENU)IDC_EDIT_INPUT, hInstance, NULL);

    hEditOutput = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
        10, 70, 670, 320, hWnd, (HMENU)IDC_EDIT_OUTPUT, hInstance, NULL);

    CreateWindowW(L"BUTTON", L"Send File", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 400, 100, 30, hWnd, (HMENU)IDC_BTN_SEND, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"List Files", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 400, 100, 30, hWnd, (HMENU)IDC_BTN_LIST, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Download File", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        230, 400, 120, 30, hWnd, (HMENU)IDC_BTN_DOWNLOAD, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 400, 100, 30, hWnd, (HMENU)IDC_BTN_EXIT, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
        case IDC_BTN_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDC_BTN_SEND:
            SendFileToServer(hWnd);
            break;
        case IDC_BTN_LIST:
            GetFileList(hWnd);
            break;
        case IDC_BTN_DOWNLOAD:
            DownloadFile(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
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